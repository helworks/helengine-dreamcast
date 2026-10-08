#include "platform/dreamcast/DreamcastAuthoredFrame.hpp"

#include "platform/dreamcast/DreamcastCubeMesh.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {

static_assert(sizeof(helengine::dreamcast::AuthoredFrameAssembler) <= 4096,
              "authored frame assembler must keep large overlay buffers off the KOS stack");

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

AuthoredLightState whiteLightFromPositiveZ(float ambientLevel = 0.0f) {
    // Travels along -Z, so a +Z facing normal is fully lit.
    AuthoredLightState lights{};
    lights.ambient = ColorRgba{ambientLevel, ambientLevel, ambientLevel, 1.0f};
    lights.directionalCount = 1;
    lights.directional[0] = AuthoredDirectionalLight{{0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};
    return lights;
}

/// One quad facing +Z and one facing -Z, both with explicit normals.
AuthoredDrawState twoFacingDrawable(const SourceVertex* vertices, const TriangleIndex* triangles, bool lit) {
    AuthoredDrawState drawable{};
    drawable.worldFromModel = translation(0.0f, 0.0f, -6.0f);
    drawable.mesh = MeshView{vertices, 6, triangles, 2, true};
    drawable.hasMaterial = true;
    drawable.materialColor = ColorRgba{1.0f, 0.5f, 0.25f, 1.0f};
    drawable.lit = lit;
    return drawable;
}

void OneCameraOneDrawableDeliversAuthoredStateExactlyOnce() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);

    EXPECT(sink.presentCount == 1);
    EXPECT(sink.lastTriangleCount == 12);
    EXPECT(near(sink.lastClear.r, 0.1f) && near(sink.lastClear.g, 0.2f) && near(sink.lastClear.b, 0.3f));
    EXPECT(near(sink.lastFirstColor.r, 0.8f) && near(sink.lastFirstColor.g, 0.1f) &&
           near(sink.lastFirstColor.b, 0.1f));
    EXPECT(assembler.summary().cameraCount == 1);
    EXPECT(assembler.summary().drawableCount == 1);
    EXPECT(assembler.summary().triangleCount == 12);

    // Every emitted vertex carries the material color, not the white mesh color.
    const GeometryFrame& frame = assembler.geometry();
    bool allMaterialColored = true;
    for (std::uint16_t i = 0; i < frame.triangleCount; ++i) {
        for (const ProjectedVertex& vertex : frame.triangles[i].vertices) {
            allMaterialColored = allMaterialColored && near(vertex.color.r, 0.8f) &&
                                 near(vertex.color.g, 0.1f) && near(vertex.color.b, 0.1f);
        }
    }
    EXPECT(allMaterialColored);
}

void ProjectionUsesFortyFiveDegreeVerticalFovAndPositiveDepth() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    // A single triangle with one vertex one unit above the axis, six units ahead.
    const SourceVertex vertices[3] = {
        {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {}},
        {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {}},
    };
    const TriangleIndex triangles[1] = {{0, 1, 2}};
    AuthoredDrawState drawable{translation(0.0f, 0.0f, -6.0f),
                               MeshView{vertices, 3, triangles, 1, false}, true,
                               ColorRgba{1.0f, 1.0f, 1.0f, 1.0f}, false};
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);
    EXPECT(sink.lastTriangleCount == 1);

    const ProjectedVertex& top = assembler.geometry().triangles[0].vertices[0];
    const float focal = 240.0f / std::tan(kAuthoredVerticalFovRadians * 0.5f);
    EXPECT(near(top.x, 320.0f));
    EXPECT(near(top.y, 240.0f - focal / 6.0f, 1.0e-2f));
    EXPECT(near(top.z, 1.0f / 6.0f));
}

void CameraTranslationMovesProjectionOpposite() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    AuthoredCameraState camera = validCamera();
    camera.worldFromCamera = translation(1.0f, 0.0f, 0.0f);
    const SourceVertex vertices[3] = {
        {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {}},
        {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {}},
    };
    const TriangleIndex triangles[1] = {{0, 1, 2}};
    AuthoredDrawState drawable{translation(0.0f, 0.0f, -6.0f),
                               MeshView{vertices, 3, triangles, 1, false}, true,
                               ColorRgba{1.0f, 1.0f, 1.0f, 1.0f}, false};
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(camera) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);
    const float focal = 240.0f / std::tan(kAuthoredVerticalFovRadians * 0.5f);
    // Moving the camera one unit right shifts the model one unit left on screen.
    EXPECT(near(assembler.geometry().triangles[0].vertices[0].x, 320.0f - focal / 6.0f, 1.0e-2f));
}

void ProjectionUsesTheAuthoredCameraFieldOfView() {
    // A camera authored with a narrower field of view magnifies the same model.
    // The focal length is derived from the camera's own value, not a constant.
    const float narrowFov = 0.5f;
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    AuthoredCameraState camera = validCamera();
    camera.verticalFovRadians = narrowFov;
    const SourceVertex vertices[3] = {
        {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {}},
        {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {}},
    };
    const TriangleIndex triangles[1] = {{0, 1, 2}};
    AuthoredDrawState drawable{translation(0.0f, 0.0f, -6.0f),
                               MeshView{vertices, 3, triangles, 1, false}, true,
                               ColorRgba{1.0f, 1.0f, 1.0f, 1.0f}, false};
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(camera) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);

    // This assertion is itself the guard. The expected y comes from the camera's
    // own field of view, so an assembler that ignored it and used the 45 degree
    // constant would place the vertex at 240 - 579.4/6 instead of 240 - 915.2/6,
    // roughly 56 pixels away and far outside the tolerance.
    const float narrowFocal = 240.0f / std::tan(narrowFov * 0.5f);
    const ProjectedVertex& top = assembler.geometry().triangles[0].vertices[0];
    EXPECT(near(top.y, 240.0f - narrowFocal / 6.0f, 1.0e-2f));
}

void OutOfRangeFieldOfViewFails() {
    RecordingSink sink;
    const float rejected[4] = {0.0f, -0.5f, 3.2f, std::numeric_limits<float>::quiet_NaN()};
    for (float value : rejected) {
        AuthoredFrameAssembler assembler;
        AuthoredCameraState camera = validCamera();
        camera.verticalFovRadians = value;
        assembler.begin(640.0f, 480.0f);
        EXPECT(assembler.setCamera(camera) == AuthoredFrameStatus::InvalidCamera);
        EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.present(sink) == AuthoredFrameStatus::InvalidCamera);
    }
    EXPECT(sink.presentCount == 0);
}

void FieldOfViewRangeBoundariesAreAccepted() {
    // Without these, widening kMinimum/kMaximum away from the engine's clamp
    // constants leaves every rejection case above still rejected, so the range
    // could drift silently. These pin both inclusive bounds and the first value
    // past the top, which also catches a < versus <= inversion.
    RecordingSink sink;
    // Pin the bounds to the engine's CameraProjectionUtils.MinimumFieldOfView and
    // MaximumFieldOfView literally. The boundary cases below are written in terms
    // of the constants, so on their own they would follow the constants if those
    // drifted; only these two assertions catch that.
    EXPECT(near(kMinimumVerticalFovRadians, 0.0174533f, 1.0e-7f));
    EXPECT(near(kMaximumVerticalFovRadians, 3.0543262f, 1.0e-7f));
    const float accepted[2] = {kMinimumVerticalFovRadians, kMaximumVerticalFovRadians};
    for (float value : accepted) {
        AuthoredFrameAssembler assembler;
        AuthoredCameraState camera = validCamera();
        camera.verticalFovRadians = value;
        assembler.begin(640.0f, 480.0f);
        EXPECT(assembler.setCamera(camera) == AuthoredFrameStatus::Ok);
    }
    {
        AuthoredFrameAssembler assembler;
        AuthoredCameraState camera = validCamera();
        camera.verticalFovRadians = std::nextafter(kMaximumVerticalFovRadians, 4.0f);
        assembler.begin(640.0f, 480.0f);
        EXPECT(assembler.setCamera(camera) == AuthoredFrameStatus::InvalidCamera);
    }
    {
        AuthoredFrameAssembler assembler;
        AuthoredCameraState camera = validCamera();
        camera.verticalFovRadians = std::nextafter(kMinimumVerticalFovRadians, 0.0f);
        assembler.begin(640.0f, 480.0f);
        EXPECT(assembler.setCamera(camera) == AuthoredFrameStatus::InvalidCamera);
    }
    EXPECT(sink.presentCount == 0);
}

void CardinalityViolationsFailBeforeAnyPresentation() {
    RecordingSink sink;
    {
        AuthoredFrameAssembler assembler;
        assembler.begin(640.0f, 480.0f);
        EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.present(sink) == AuthoredFrameStatus::NoCamera);
    }
    {
        AuthoredFrameAssembler assembler;
        assembler.begin(640.0f, 480.0f);
        EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::MultipleCameras);
        EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.present(sink) == AuthoredFrameStatus::MultipleCameras);
        EXPECT(assembler.summary().cameraCount == 2);
    }
    {
        AuthoredFrameAssembler assembler;
        assembler.begin(640.0f, 480.0f);
        EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.present(sink) == AuthoredFrameStatus::NoDrawable);
    }
    {
        AuthoredFrameAssembler assembler;
        assembler.begin(640.0f, 480.0f);
        EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::MultipleDrawables);
        EXPECT(assembler.present(sink) == AuthoredFrameStatus::MultipleDrawables);
    }
    {
        AuthoredFrameAssembler assembler;
        EXPECT(assembler.present(sink) == AuthoredFrameStatus::NotStarted);
    }
    EXPECT(sink.presentCount == 0);
}

void InvalidCameraMaterialAndTransformsFailBeforePackets() {
    RecordingSink sink;
    {
        AuthoredFrameAssembler assembler;
        assembler.begin(640.0f, 480.0f);
        AuthoredCameraState camera = validCamera();
        camera.hasRenderTarget = true;
        EXPECT(assembler.setCamera(camera) == AuthoredFrameStatus::InvalidCamera);
    }
    {
        AuthoredFrameAssembler assembler;
        assembler.begin(640.0f, 480.0f);
        AuthoredCameraState camera = validCamera();
        camera.viewport = AuthoredViewport{0.0f, 0.0f, 0.5f, 1.0f};
        EXPECT(assembler.setCamera(camera) == AuthoredFrameStatus::InvalidCamera);
    }
    {
        AuthoredFrameAssembler assembler;
        assembler.begin(640.0f, 480.0f);
        AuthoredCameraState camera = validCamera();
        camera.farPlane = camera.nearPlane;
        EXPECT(assembler.setCamera(camera) == AuthoredFrameStatus::InvalidCamera);
    }
    {
        AuthoredFrameAssembler assembler;
        assembler.begin(640.0f, 480.0f);
        AuthoredCameraState camera = validCamera();
        camera.clearStencilEnabled = true;
        EXPECT(assembler.setCamera(camera) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
        RecordingSink stencilSink;
        EXPECT(assembler.present(stencilSink) == AuthoredFrameStatus::Ok);
        EXPECT(stencilSink.presentCount == 1);
        camera = validCamera();
        camera.clearColorEnabled = false;
        AuthoredFrameAssembler second;
        second.begin(640.0f, 480.0f);
        EXPECT(second.setCamera(camera) == AuthoredFrameStatus::InvalidClear);
        camera = validCamera();
        camera.clearColor.g = std::nanf("");
        AuthoredFrameAssembler third;
        third.begin(640.0f, 480.0f);
        EXPECT(third.setCamera(camera) == AuthoredFrameStatus::InvalidClear);
    }
    {
        AuthoredFrameAssembler assembler;
        assembler.begin(640.0f, 480.0f);
        AuthoredCameraState camera = validCamera();
        camera.worldFromCamera.m[0][0] = 2.0f;
        EXPECT(assembler.setCamera(camera) == AuthoredFrameStatus::InvalidCameraTransform);
        EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.present(sink) == AuthoredFrameStatus::InvalidCameraTransform);
    }
    {
        AuthoredFrameAssembler assembler;
        assembler.begin(640.0f, 480.0f);
        EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
        AuthoredDrawState drawable = validDrawable();
        drawable.hasMaterial = false;
        EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::InvalidMaterial);
        EXPECT(assembler.present(sink) == AuthoredFrameStatus::InvalidMaterial);
    }
    {
        AuthoredFrameAssembler assembler;
        assembler.begin(640.0f, 480.0f);
        EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
        AuthoredDrawState drawable = validDrawable();
        drawable.worldFromModel.m[0][0] = -1.0f;
        EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::InvalidModelTransform);
        EXPECT(assembler.present(sink) == AuthoredFrameStatus::InvalidModelTransform);
    }
    {
        AuthoredFrameAssembler assembler;
        assembler.begin(640.0f, 480.0f);
        EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
        AuthoredDrawState drawable = validDrawable();
        drawable.mesh.vertexCount = 65;
        EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::InvalidMesh);
        AuthoredFrameAssembler second;
        second.begin(640.0f, 480.0f);
        EXPECT(second.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
        drawable = validDrawable();
        drawable.mesh.triangleCount = 129;
        EXPECT(second.addDrawable(drawable) == AuthoredFrameStatus::Ok);
        EXPECT(second.present(sink) == AuthoredFrameStatus::InvalidMesh);
    }
    EXPECT(sink.presentCount == 0);
}

void FullyClippedDrawableIsAValidClearOnlyFrame() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable(-500.0f)) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);
    EXPECT(sink.presentCount == 1);
    EXPECT(sink.lastTriangleCount == 0);
    EXPECT(assembler.summary().triangleCount == 0);
    EXPECT(assembler.summary().rejectedTriangleCount == 12);
}

void SinkRejectionIsReportedAndFrameIsNotRepeated() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    sink.accept = false;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::PresentFailed);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::NotStarted);
    EXPECT(sink.presentCount == 1);
}

void LitVertexFacingLightGetsFullLightColor() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    const SourceVertex vertices[3] = {
        {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
    };
    const TriangleIndex triangles[1] = {{0, 1, 2}};
    AuthoredDrawState drawable{};
    drawable.worldFromModel = translation(0.0f, 0.0f, -6.0f);
    drawable.mesh = MeshView{vertices, 3, triangles, 1, true};
    drawable.hasMaterial = true;
    drawable.materialColor = ColorRgba{1.0f, 0.5f, 0.25f, 1.0f};
    drawable.lit = true;

    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(whiteLightFromPositiveZ()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);

    const ProjectedVertex& vertex = assembler.geometry().triangles[0].vertices[0];
    EXPECT(near(vertex.color.r, 1.0f));
    EXPECT(near(vertex.color.g, 0.5f));
    EXPECT(near(vertex.color.b, 0.25f));
    EXPECT(assembler.summary().directionalLightCount == 1);
    EXPECT(assembler.summary().lit);
}

void LitVertexFacingAwayGetsAmbientOnly() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    // Normals point away from the light (-Z), so only ambient 0.25 applies.
    const SourceVertex vertices[3] = {
        {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
    };
    const TriangleIndex triangles[1] = {{0, 1, 2}};
    AuthoredDrawState drawable{};
    drawable.worldFromModel = translation(0.0f, 0.0f, -6.0f);
    drawable.mesh = MeshView{vertices, 3, triangles, 1, true};
    drawable.hasMaterial = true;
    drawable.materialColor = ColorRgba{1.0f, 0.5f, 0.25f, 1.0f};
    drawable.lit = true;

    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(whiteLightFromPositiveZ(0.25f)) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);

    const ProjectedVertex& vertex = assembler.geometry().triangles[0].vertices[0];
    EXPECT(near(vertex.color.r, 0.25f));
    EXPECT(near(vertex.color.g, 0.125f));
    EXPECT(near(vertex.color.b, 0.0625f));
}

void LitVertexFacingAwayWithoutAmbientIsBlack() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    const SourceVertex vertices[3] = {
        {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
    };
    const TriangleIndex triangles[1] = {{0, 1, 2}};
    AuthoredDrawState drawable{};
    drawable.worldFromModel = translation(0.0f, 0.0f, -6.0f);
    drawable.mesh = MeshView{vertices, 3, triangles, 1, true};
    drawable.hasMaterial = true;
    drawable.materialColor = ColorRgba{1.0f, 0.5f, 0.25f, 1.0f};
    drawable.lit = true;

    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(whiteLightFromPositiveZ()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);

    const ProjectedVertex& vertex = assembler.geometry().triangles[0].vertices[0];
    EXPECT(near(vertex.color.r, 0.0f));
    EXPECT(near(vertex.color.g, 0.0f));
    EXPECT(near(vertex.color.b, 0.0f));
}

void LitColorIsClampedToOne() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    const SourceVertex vertices[3] = {
        {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
    };
    const TriangleIndex triangles[1] = {{0, 1, 2}};
    AuthoredDrawState drawable{};
    drawable.worldFromModel = translation(0.0f, 0.0f, -6.0f);
    drawable.mesh = MeshView{vertices, 3, triangles, 1, true};
    drawable.hasMaterial = true;
    drawable.materialColor = ColorRgba{1.0f, 1.0f, 1.0f, 1.0f};
    drawable.lit = true;

    assembler.begin(640.0f, 480.0f);
    // Ambient 0.5 plus a full-strength light would reach 1.5 without clamping.
    EXPECT(assembler.setLights(whiteLightFromPositiveZ(0.5f)) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);

    const ProjectedVertex& vertex = assembler.geometry().triangles[0].vertices[0];
    EXPECT(near(vertex.color.r, 1.0f));
    EXPECT(near(vertex.color.a, 1.0f));
}

void UnlitMaterialIgnoresLights() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(whiteLightFromPositiveZ(0.25f)) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    // validDrawable() has no normals and lit defaults to false.
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);

    EXPECT(near(sink.lastFirstColor.r, 0.8f));
    EXPECT(near(sink.lastFirstColor.g, 0.1f));
    EXPECT(near(sink.lastFirstColor.b, 0.1f));
    EXPECT(!assembler.summary().lit);
}

void LitMeshWithoutNormalsFailsBeforePresentation() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    AuthoredDrawState drawable = validDrawable();
    drawable.lit = true;

    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(whiteLightFromPositiveZ()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::LitMeshWithoutNormals);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::LitMeshWithoutNormals);
    EXPECT(sink.presentCount == 0);
}

void FiveDirectionalLightsFail() {
    AuthoredFrameAssembler assembler;
    AuthoredLightState lights = whiteLightFromPositiveZ();
    lights.directionalCount = 5;

    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(lights) == AuthoredFrameStatus::TooManyDirectionalLights);

    RecordingSink sink;
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::TooManyDirectionalLights);
    EXPECT(sink.presentCount == 0);
}

void NonFiniteLightStateFails() {
    AuthoredFrameAssembler assembler;
    AuthoredLightState lights = whiteLightFromPositiveZ();
    lights.directional[0].color.g = std::nanf("");
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(lights) == AuthoredFrameStatus::InvalidLight);

    AuthoredFrameAssembler second;
    AuthoredLightState zeroDirection = whiteLightFromPositiveZ();
    zeroDirection.directional[0].direction = Vec3{0.0f, 0.0f, 0.0f};
    second.begin(640.0f, 480.0f);
    EXPECT(second.setLights(zeroDirection) == AuthoredFrameStatus::InvalidLight);
}

void LitShadingSurvivesNonuniformScale() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    // Two quads: the first faces +Z (lit), the second faces -Z (unlit).
    const SourceVertex vertices[6] = {
        {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{-1.0f, -1.0f, -0.5f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{1.0f, -1.0f, -0.5f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{0.0f, 1.0f, -0.5f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
    };
    const TriangleIndex triangles[2] = {{0, 1, 2}, {3, 4, 5}};
    AuthoredDrawState drawable = twoFacingDrawable(vertices, triangles, true);
    // Nonuniform scale must not tilt the face normals.
    drawable.worldFromModel.m[1][1] = 1.25f;

    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(whiteLightFromPositiveZ()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);

    bool sawLit = false;
    bool sawDark = false;
    const GeometryFrame& frame = assembler.geometry();
    for (std::uint16_t i = 0; i < frame.triangleCount; ++i) {
        const float red = frame.triangles[i].vertices[0].color.r;
        sawLit = sawLit || near(red, 1.0f);
        sawDark = sawDark || near(red, 0.0f);
    }
    EXPECT(sawLit);
    EXPECT(sawDark);
}

} // namespace

int main() {
    // Unload -> camera-free transition -> next scene still presents a complete
    // empty frame, while ordinary camera-free scenes remain an error.
    {
        AuthoredFrameAssembler assembler;
        RecordingSink sink;
        for(int cycle=0;cycle<3;++cycle) {
            assembler.begin(640,480);
            EXPECT(assembler.setCamera(validCamera())==AuthoredFrameStatus::Ok);
            EXPECT(assembler.addDrawable(validDrawable())==AuthoredFrameStatus::Ok);
            EXPECT(assembler.present(sink)==AuthoredFrameStatus::Ok);
            assembler.begin(640,480);
            EXPECT(assembler.presentEmptyTransition(sink,{0.1f,0.2f,0.3f,1})==AuthoredFrameStatus::Ok);
            EXPECT(sink.lastTriangleCount==0 && sink.lastSpriteCount==0);
            EXPECT(assembler.summary().cameraCount==0 && assembler.summary().glyphCount==0);
            EXPECT(assembler.presentEmptyTransition(sink,{0,0,0,1})==AuthoredFrameStatus::NotStarted);
            assembler.begin(640,480);
            EXPECT(assembler.present(sink)==AuthoredFrameStatus::NoCamera);
        }
        EXPECT(sink.presentCount==6);
        assembler.begin(640,480);sink.accept=false;
        EXPECT(assembler.presentEmptyTransition(sink,{0,0,0,1})==AuthoredFrameStatus::PresentFailed);
    }
    OneCameraOneDrawableDeliversAuthoredStateExactlyOnce();
    ProjectionUsesFortyFiveDegreeVerticalFovAndPositiveDepth();
    CameraTranslationMovesProjectionOpposite();
    ProjectionUsesTheAuthoredCameraFieldOfView();
    OutOfRangeFieldOfViewFails();
    FieldOfViewRangeBoundariesAreAccepted();
    CardinalityViolationsFailBeforeAnyPresentation();
    InvalidCameraMaterialAndTransformsFailBeforePackets();
    FullyClippedDrawableIsAValidClearOnlyFrame();
    SinkRejectionIsReportedAndFrameIsNotRepeated();
    LitVertexFacingLightGetsFullLightColor();
    LitVertexFacingAwayGetsAmbientOnly();
    LitVertexFacingAwayWithoutAmbientIsBlack();
    LitColorIsClampedToOne();
    UnlitMaterialIgnoresLights();
    LitMeshWithoutNormalsFailsBeforePresentation();
    FiveDirectionalLightsFail();
    NonFiniteLightStateFails();
    LitShadingSurvivesNonuniformScale();
    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return 1;
    }
    std::cout << "DreamcastAuthoredSubmissionTests: all tests passed\n";
    return 0;
}
