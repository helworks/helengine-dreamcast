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
    AuthoredSpriteState cases[6] = {validSprite(), validSprite(), validSprite(),
                                    validSprite(), validSprite(), validSprite()};
    cases[0].destinationX = nan;
    cases[1].destinationWidth = 0.0f;   // zero extent draws nothing; reject rather than emit
    cases[2].texture = nullptr;
    cases[3].sourceU1 = 1.5f;           // outside the used sub-rectangle
    cases[4].sourceU1 = cases[4].sourceU0; // degenerate source
    cases[5].tint = ColorRgba{nan, 0.0f, 0.0f, 1.0f};
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

} // namespace

int main() {
    OneSpriteIsAcceptedAndCounted();
    SixtyFifthSpriteFails();
    InvalidSpritesFailBeforeAnyPresentation();
    ASpritelessFrameStillPresents();
    SpritesKeepSubmissionOrder();
    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return 1;
    }
    std::cout << "DreamcastSpriteSubmissionTests: all tests passed\n";
    return 0;
}
