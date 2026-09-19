#include "platform/dreamcast/DreamcastAuthoredFrame.hpp"

#include "platform/dreamcast/DreamcastCubeMesh.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

using namespace helengine::dreamcast;

int failures = 0;

void expect(bool condition, const char* expression) {
    if (!condition) {
        std::cerr << "expected " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(value) expect((value), #value)

bool near(float left, float right, float epsilon = 1.0e-3f) {
    return std::fabs(left - right) <= epsilon;
}

/// Records every presentation so tests can prove the exactly-once contract.
class RecordingSink final : public IAuthoredFrameSink {
public:
    bool present(const GeometryFrame& geometry, const SpriteFrame& sprites,
                const ColorRgba& clearColor) noexcept override {
        ++presentCount;
        lastTriangleCount = geometry.triangleCount;
        lastSpriteCount = sprites.spriteCount;
        lastClear = clearColor;
        if (geometry.triangleCount != 0) {
            lastFirstColor = geometry.triangles[0].vertices[0].color;
        }
        return accept;
    }

    bool accept = true;
    int presentCount = 0;
    std::uint16_t lastTriangleCount = 0;
    std::uint16_t lastSpriteCount = 0;
    ColorRgba lastClear{};
    ColorRgba lastFirstColor{};
};

Matrix4x4 identity() {
    return Matrix4x4{{
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    }};
}

Matrix4x4 translation(float x, float y, float z) {
    Matrix4x4 result = identity();
    result.m[0][3] = x;
    result.m[1][3] = y;
    result.m[2][3] = z;
    return result;
}

AuthoredCameraState validCamera() {
    return AuthoredCameraState{
        identity(),
        0.1f,
        100.0f,
        AuthoredViewport{0.0f, 0.0f, 1.0f, 1.0f},
        false,
        true,
        true,
        false,
        ColorRgba{0.1f, 0.2f, 0.3f, 1.0f},
        kAuthoredVerticalFovRadians,
    };
}

AuthoredDrawState validDrawable(float z = -6.0f) {
    // The generated camera looks along -Z, so a model at negative Z is in front.
    return AuthoredDrawState{translation(0.0f, 0.0f, z), cubeMesh(), true,
                             ColorRgba{0.8f, 0.1f, 0.1f, 1.0f}, false};
}

int dummyTexture = 0;

AuthoredSpriteState validSprite() {
    return AuthoredSpriteState{32.0f, 48.0f, 64.0f, 64.0f,
                               0.0f, 0.0f, 0.75f, 0.5f,
                               &dummyTexture,
                               ColorRgba{1.0f, 1.0f, 1.0f, 1.0f}};
}

void OneSpriteIsAcceptedAndCounted() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addSprite(validSprite()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.sprites().spriteCount == 1);
    EXPECT(assembler.summary().spriteCount == 1);
    EXPECT(sink.presentCount == 1);
    EXPECT(sink.lastSpriteCount == 1);
}

void SixtyFifthSpriteFails() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    for (std::uint16_t i = 0; i < kMaxSprites; ++i) {
        EXPECT(assembler.addSprite(validSprite()) == AuthoredFrameStatus::Ok);
    }
    EXPECT(assembler.addSprite(validSprite()) == AuthoredFrameStatus::TooManySprites);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::TooManySprites);
    EXPECT(sink.presentCount == 0);
    EXPECT(assembler.summary().spriteCount == kMaxSprites + 1);
}

void InvalidSpritesFailBeforeAnyPresentation() {
    RecordingSink sink;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    AuthoredSpriteState cases[7] = {validSprite(), validSprite(), validSprite(),   validSprite(),
                                    validSprite(), validSprite(), validSprite()};
    cases[0].destinationX = nan;
    cases[1].destinationWidth = 0.0f;   // zero extent draws nothing; reject rather than emit
    cases[2].texture = nullptr;
    cases[3].sourceU1 = 1.5f;           // outside the used sub-rectangle
    cases[4].sourceU1 = cases[4].sourceU0; // degenerate source
    cases[5].tint = ColorRgba{nan, 0.0f, 0.0f, 1.0f};
    // Finite but absurd: cannot be a real screen coordinate, and left
    // unchecked would overflow to infinity once the raster stage adds
    // destinationWidth to destinationX.
    cases[6].destinationX = 1.0e30f;
    cases[6].destinationWidth = 3.0e38f;
    for (const AuthoredSpriteState& sprite : cases) {
        AuthoredFrameAssembler assembler;
        assembler.begin(640.0f, 480.0f);
        EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.addSprite(sprite) == AuthoredFrameStatus::InvalidSprite);
        EXPECT(assembler.present(sink) == AuthoredFrameStatus::InvalidSprite);
    }
    EXPECT(sink.presentCount == 0);
}

void ASpritelessFrameStillPresents() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);
    EXPECT(sink.presentCount == 1);
    EXPECT(sink.lastSpriteCount == 0);
}

void SpritesKeepSubmissionOrder() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    AuthoredSpriteState first = validSprite();
    first.destinationX = 10.0f;
    AuthoredSpriteState second = validSprite();
    second.destinationX = 200.0f;
    EXPECT(assembler.addSprite(first) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addSprite(second) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);
    EXPECT(near(assembler.sprites().sprites[0].destinationX, 10.0f));
    EXPECT(near(assembler.sprites().sprites[1].destinationX, 200.0f));
}

void ZeroViewportRejectsAnOtherwiseValidSprite() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(0.0f, 0.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addSprite(validSprite()) == AuthoredFrameStatus::InvalidViewport);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::InvalidViewport);
    EXPECT(sink.presentCount == 0);
}

void NegativeViewportRejectsAnOtherwiseValidSprite() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(-640.0f, -480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addSprite(validSprite()) == AuthoredFrameStatus::InvalidViewport);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::InvalidViewport);
    EXPECT(sink.presentCount == 0);
}

void NonFiniteViewportRejectsAnOtherwiseValidSprite() {
    // The case the destination bound exists to stop: without an explicit
    // viewport guard, a NaN viewport makes every bound comparison evaluate
    // to false (NaN compares unordered against anything), so the bound
    // check silently admits the sprite instead of rejecting it. This test
    // fails if that guard is ever removed or bypassed.
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    assembler.begin(nan, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addSprite(validSprite()) == AuthoredFrameStatus::InvalidViewport);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::InvalidViewport);
    EXPECT(sink.presentCount == 0);
}

void SourceSpanRescalesAnImageRectangleOntoThePaddedSquare() {
    // The whole image of a 3x2 picture cooked into an 8x8 square occupies
    // three eighths of U and two eighths of V, not the whole square. Getting
    // this wrong samples the padding and is invisible on a texture whose
    // image already fills its square.
    const SpriteSourceSpan whole =
        spriteSourceSpan(SpriteSourceRect{0.0f, 0.0f, 1.0f, 1.0f}, 3, 2, 8);
    EXPECT(near(whole.u0, 0.0f));
    EXPECT(near(whole.v0, 0.0f));
    EXPECT(near(whole.u1, 3.0f / 8.0f));
    EXPECT(near(whole.v1, 2.0f / 8.0f));

    // A sub-rectangle is normalized against the image too, so its edges scale
    // by the same factor rather than being taken as square coordinates.
    const SpriteSourceSpan half =
        spriteSourceSpan(SpriteSourceRect{0.5f, 0.0f, 0.5f, 1.0f}, 3, 2, 8);
    EXPECT(near(half.u0, 1.5f / 8.0f));
    EXPECT(near(half.u1, 3.0f / 8.0f));
    EXPECT(near(half.v0, 0.0f));
    EXPECT(near(half.v1, 2.0f / 8.0f));

    // An image that exactly fills its square is the degenerate case where a
    // missing rescale would go unnoticed; it must still come back unchanged.
    const SpriteSourceSpan exact =
        spriteSourceSpan(SpriteSourceRect{0.25f, 0.75f, 0.5f, 0.25f}, 64, 64, 64);
    EXPECT(near(exact.u0, 0.25f));
    EXPECT(near(exact.v0, 0.75f));
    EXPECT(near(exact.u1, 0.75f));
    EXPECT(near(exact.v1, 1.0f));
}

void SourceSpanFromADegenerateTextureIsRejectedByTheAssembler() {
    // spriteSourceSpan decides nothing; a zero padded side makes it produce a
    // non-finite span, and the assembler is what fails the frame for it. This
    // pins that division-by-zero stays IEEE rather than becoming a guard that
    // silently substitutes a plausible-looking span.
    const SpriteSourceSpan span =
        spriteSourceSpan(SpriteSourceRect{0.0f, 0.0f, 1.0f, 1.0f}, 8, 8, 0);
    EXPECT(!std::isfinite(span.u1));

    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    AuthoredSpriteState sprite = validSprite();
    sprite.sourceU0 = span.u0;
    sprite.sourceV0 = span.v0;
    sprite.sourceU1 = span.u1;
    sprite.sourceV1 = span.v1;
    EXPECT(assembler.addSprite(sprite) == AuthoredFrameStatus::InvalidSprite);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::InvalidSprite);
    EXPECT(sink.presentCount == 0);
}

} // namespace

int main() {
    OneSpriteIsAcceptedAndCounted();
    SixtyFifthSpriteFails();
    InvalidSpritesFailBeforeAnyPresentation();
    ASpritelessFrameStillPresents();
    SpritesKeepSubmissionOrder();
    ZeroViewportRejectsAnOtherwiseValidSprite();
    NegativeViewportRejectsAnOtherwiseValidSprite();
    NonFiniteViewportRejectsAnOtherwiseValidSprite();
    SourceSpanRescalesAnImageRectangleOntoThePaddedSquare();
    SourceSpanFromADegenerateTextureIsRejectedByTheAssembler();
    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return 1;
    }
    std::cout << "DreamcastSpriteSubmissionTests: all tests passed\n";
    return 0;
}
