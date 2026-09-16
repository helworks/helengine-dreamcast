#include "platform/dreamcast/DreamcastAuthoredFrame.hpp"

#include "platform/dreamcast/DreamcastCubeMesh.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>

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
    bool present(const GeometryFrame& geometry, const ColorRgba& clearColor) noexcept override {
        ++presentCount;
        lastTriangleCount = geometry.triangleCount;
        lastClear = clearColor;
        if (geometry.triangleCount != 0) {
            lastFirstColor = geometry.triangles[0].vertices[0].color;
        }
        return accept;
    }

    bool accept = true;
    int presentCount = 0;
    std::uint16_t lastTriangleCount = 0;
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
    };
}

AuthoredDrawState validDrawable(float z = -6.0f) {
    // The generated camera looks along -Z, so a model at negative Z is in front.
    return AuthoredDrawState{translation(0.0f, 0.0f, z), cubeMesh(), true,
                             ColorRgba{0.8f, 0.1f, 0.1f, 1.0f}};
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
                               ColorRgba{1.0f, 1.0f, 1.0f, 1.0f}};
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
                               ColorRgba{1.0f, 1.0f, 1.0f, 1.0f}};
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(camera) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);
    const float focal = 240.0f / std::tan(kAuthoredVerticalFovRadians * 0.5f);
    // Moving the camera one unit right shifts the model one unit left on screen.
    EXPECT(near(assembler.geometry().triangles[0].vertices[0].x, 320.0f - focal / 6.0f, 1.0e-2f));
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
        EXPECT(assembler.setCamera(camera) == AuthoredFrameStatus::InvalidClear);
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

} // namespace

int main() {
    OneCameraOneDrawableDeliversAuthoredStateExactlyOnce();
    ProjectionUsesFortyFiveDegreeVerticalFovAndPositiveDepth();
    CameraTranslationMovesProjectionOpposite();
    CardinalityViolationsFailBeforeAnyPresentation();
    InvalidCameraMaterialAndTransformsFailBeforePackets();
    FullyClippedDrawableIsAValidClearOnlyFrame();
    SinkRejectionIsReportedAndFrameIsNotRepeated();
    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return 1;
    }
    std::cout << "DreamcastAuthoredSubmissionTests: all tests passed\n";
    return 0;
}
