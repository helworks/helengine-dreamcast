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
    bool present(const GeometryFrame& geometry, const AuthoredOverlayFrame& overlay,
                const ColorRgba& clearColor) noexcept override {
        ++presentCount;
        lastTriangleCount = geometry.triangleCount;
        lastSpriteCount = overlay.sprites.spriteCount;
        lastRectCount = overlay.roundedRectCount;
        lastGlyphCount = overlay.glyphCount;
        lastOrderCount = overlay.orderCount;
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
    std::uint16_t lastRectCount = 0;
    std::uint16_t lastGlyphCount = 0;
    std::uint16_t lastOrderCount = 0;
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

AuthoredRoundedRectState validRect() {
    return AuthoredRoundedRectState{10.0f, 20.0f, 90.0f, 45.0f, 8.0f, 2.0f,
                                    kRoundedRectAllCorners,
                                    ColorRgba{0.1f, 0.2f, 0.3f, 0.8f},
                                    ColorRgba{0.8f, 0.7f, 0.6f, 1.0f}};
}

AuthoredGlyphState validGlyph() {
    return AuthoredGlyphState{24.0f, 30.0f, 12.0f, 18.0f,
                              0.1f, 0.2f, 0.4f, 0.7f, &dummyTexture,
                              ColorRgba{1.0f, 1.0f, 1.0f, 1.0f}};
}

void MixedOverlayKindsRetainPainterOrder() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addSprite(validSprite()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addRoundedRect(validRect()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addGlyph(validGlyph()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addSprite(validSprite()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);
    const AuthoredOverlayFrame& overlay = assembler.overlay();
    EXPECT(overlay.orderCount == 4);
    EXPECT(overlay.order[0].kind == AuthoredOverlayKind::Sprite);
    EXPECT(overlay.order[0].index == 0);
    EXPECT(overlay.order[1].kind == AuthoredOverlayKind::RoundedRect);
    EXPECT(overlay.order[1].index == 0);
    EXPECT(overlay.order[2].kind == AuthoredOverlayKind::Glyph);
    EXPECT(overlay.order[2].index == 0);
    EXPECT(overlay.order[3].kind == AuthoredOverlayKind::Sprite);
    EXPECT(overlay.order[3].index == 1);
    EXPECT(sink.lastRectCount == 1 && sink.lastGlyphCount == 1);
}

void OverlayCapacityAndInvalidInputFailBeforePresentation() {
    RecordingSink sink;
    AuthoredFrameAssembler assembler;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    for (std::uint16_t i = 0; i < kMaxRoundedRects; ++i)
        EXPECT(assembler.addRoundedRect(validRect()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addRoundedRect(validRect()) == AuthoredFrameStatus::TooManyRoundedRects);
    EXPECT(assembler.addGlyph(validGlyph()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::TooManyRoundedRects);
    EXPECT(sink.presentCount == 0);

    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    for (std::uint16_t i = 0; i < kMaxGlyphs; ++i)
        EXPECT(assembler.addGlyph(validGlyph()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addGlyph(validGlyph()) == AuthoredFrameStatus::TooManyGlyphs);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::TooManyGlyphs);

    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    for (std::uint16_t i = 0; i < kMaxSprites; ++i)
        EXPECT(assembler.addSprite(validSprite()) == AuthoredFrameStatus::Ok);
    for (std::uint16_t i = 0; i < kMaxRoundedRects; ++i)
        EXPECT(assembler.addRoundedRect(validRect()) == AuthoredFrameStatus::Ok);
    for (std::uint16_t i = 0; i < kMaxGlyphs; ++i)
        EXPECT(assembler.addGlyph(validGlyph()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addSprite(validSprite()) == AuthoredFrameStatus::TooManyOverlaySubmissions);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::TooManyOverlaySubmissions);
}

void InvalidRectAndGlyphLatchFirstFailureAndBeginClearsOverlay() {
    RecordingSink sink;
    AuthoredFrameAssembler assembler;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    AuthoredRoundedRectState rect = validRect();
    rect.destinationWidth = std::numeric_limits<float>::infinity();
    EXPECT(assembler.addRoundedRect(rect) == AuthoredFrameStatus::InvalidRoundedRect);
    AuthoredGlyphState glyph = validGlyph();
    glyph.texture = nullptr;
    EXPECT(assembler.addGlyph(glyph) == AuthoredFrameStatus::InvalidGlyph);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::InvalidRoundedRect);
    EXPECT(sink.presentCount == 0);

    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.overlay().orderCount == 0);
    EXPECT(assembler.overlay().roundedRectCount == 0);
    EXPECT(assembler.overlay().glyphCount == 0);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addGlyph(validGlyph()) == AuthoredFrameStatus::Ok);
}

void RoundedRectAndGlyphValidationRejectMalformedFields() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    AuthoredRoundedRectState invalidRects[] = {validRect(), validRect(), validRect()};
    invalidRects[0].destinationHeight = 0.0f;
    invalidRects[1].radius = nan;
    invalidRects[2].roundedCorners = 0x80;
    for (const AuthoredRoundedRectState& rect : invalidRects) {
        AuthoredFrameAssembler assembler;
        RecordingSink sink;
        assembler.begin(640.0f, 480.0f);
        EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.addRoundedRect(rect) == AuthoredFrameStatus::InvalidRoundedRect);
        EXPECT(assembler.present(sink) == AuthoredFrameStatus::InvalidRoundedRect);
        EXPECT(assembler.overlay().roundedRectCount == 0);
        EXPECT(sink.presentCount == 0);
    }
    AuthoredGlyphState invalidGlyphs[] = {validGlyph(), validGlyph(), validGlyph()};
    invalidGlyphs[0].destinationWidth = -1.0f;
    invalidGlyphs[1].sourceV1 = invalidGlyphs[1].sourceV0;
    invalidGlyphs[2].sourceU0 = nan;
    for (const AuthoredGlyphState& glyph : invalidGlyphs) {
        AuthoredFrameAssembler assembler;
        RecordingSink sink;
        assembler.begin(640.0f, 480.0f);
        EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.addGlyph(glyph) == AuthoredFrameStatus::InvalidGlyph);
        EXPECT(assembler.present(sink) == AuthoredFrameStatus::InvalidGlyph);
        EXPECT(assembler.overlay().glyphCount == 0);
        EXPECT(sink.presentCount == 0);
    }
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

/// Presents a frame that submits no sprite at all, so present()'s own viewport
/// guard is reached directly rather than through addSprite having failed first.
void expectSpritelessFrameRejectsViewport(float width, float height) {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(width, height);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::InvalidViewport);
    EXPECT(sink.presentCount == 0);
    EXPECT(assembler.summary().spriteCount == 0);
}

void ASpritelessFrameWithABadViewportIsRejectedByPresent() {
    // This is the one path in the sprite milestone that changes pre-existing
    // 3D behaviour: a frame carrying no sprites at all used to reach the
    // geometry projection whatever its viewport was, and now fails first.
    // Every other viewport test above reaches the guard only because
    // addSprite already latched the failure, so without this case the guard
    // inside present() is never exercised on its own. viewportWidth_ and
    // viewportHeight_ feed the focal length and clip math, where a zero
    // divides, a negative mirrors, and a NaN or infinity poisons every
    // comparison downstream.
    expectSpritelessFrameRejectsViewport(0.0f, 480.0f);
    expectSpritelessFrameRejectsViewport(640.0f, 0.0f);
    expectSpritelessFrameRejectsViewport(-640.0f, 480.0f);
    expectSpritelessFrameRejectsViewport(640.0f, -480.0f);
    expectSpritelessFrameRejectsViewport(std::numeric_limits<float>::quiet_NaN(), 480.0f);
    expectSpritelessFrameRejectsViewport(640.0f, std::numeric_limits<float>::infinity());
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

void UiOnlyFramePresentsWithoutMesh() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(640.0f, 480.0f);
    AuthoredCameraState camera = validCamera();
    camera.clearDepthEnabled = false;
    EXPECT(assembler.setCamera(camera) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addSprite(validSprite()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);
    EXPECT(sink.presentCount == 1);
    EXPECT(sink.lastTriangleCount == 0);
    EXPECT(sink.lastSpriteCount == 1);
}

int main() {
    // Resolved mirrored/sheared corners bypass rectangle reconstruction, retaining validation.
    {
        AuthoredFrameAssembler assembler;
        RecordingSink sink;
        assembler.begin(640, 480);
        auto sprite = validSprite();
        sprite.hasResolvedCorners = true;
        sprite.destinationWidth = sprite.destinationHeight = 0;
        const float x[4] = {30,10,38,18}, y[4] = {20,25,60,65};
        for (unsigned i = 0; i < 4; ++i) {
            sprite.cornerX[i] = x[i]; sprite.cornerY[i] = y[i];
        }
        EXPECT(assembler.addSprite(sprite) == AuthoredFrameStatus::Ok);
        sprite.cornerY[3] = std::numeric_limits<float>::quiet_NaN();
        EXPECT(assembler.addSprite(sprite) == AuthoredFrameStatus::InvalidSprite);
    }
    UiOnlyFramePresentsWithoutMesh();
    OneSpriteIsAcceptedAndCounted();
    SixtyFifthSpriteFails();
    InvalidSpritesFailBeforeAnyPresentation();
    ASpritelessFrameStillPresents();
    SpritesKeepSubmissionOrder();
    ZeroViewportRejectsAnOtherwiseValidSprite();
    NegativeViewportRejectsAnOtherwiseValidSprite();
    NonFiniteViewportRejectsAnOtherwiseValidSprite();
    ASpritelessFrameWithABadViewportIsRejectedByPresent();
    SourceSpanRescalesAnImageRectangleOntoThePaddedSquare();
    SourceSpanFromADegenerateTextureIsRejectedByTheAssembler();
    MixedOverlayKindsRetainPainterOrder();
    OverlayCapacityAndInvalidInputFailBeforePresentation();
    InvalidRectAndGlyphLatchFirstFailureAndBeginClearsOverlay();
    RoundedRectAndGlyphValidationRejectMalformedFields();
    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return 1;
    }
    std::cout << "DreamcastSpriteSubmissionTests: all tests passed\n";
    return 0;
}
