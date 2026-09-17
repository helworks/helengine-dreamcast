#pragma once

#include "DreamcastAuthoredTransform.hpp"
#include "DreamcastGeometryPipeline.hpp"

#include <cstdint>
#include <memory>

namespace helengine::dreamcast {

/// The engine's CameraProjectionUtils.DefaultFieldOfView, 45 degrees, restated
/// here so host tests can build a camera carrying the value a never-authored
/// camera arrives with. Production code does not read it: the projection takes
/// the field of view from AuthoredCameraState, and nothing in this platform
/// substitutes a default. The diagnostic cube keeps its own 1.10 radian value;
/// the two are not shared.
constexpr float kAuthoredVerticalFovRadians = 0.78539816339744830962f;

/// Accepted vertical field-of-view range in radians, mirroring the engine's
/// CameraProjectionUtils clamp so both sides agree on what a camera may author.
constexpr float kMinimumVerticalFovRadians = 0.0174533f;
constexpr float kMaximumVerticalFovRadians = 3.0543262f;

/// Normalized camera viewport rectangle as authored (x, y, width, height).
struct AuthoredViewport {
    float x;
    float y;
    float width;
    float height;
};

/// Maximum simultaneously enabled directional lights for Dreamcast v0.3.
constexpr std::uint8_t kMaxDirectionalLights = 4;

/// One enabled directional light, resolved to world space by the caller.
struct AuthoredDirectionalLight {
    Vec3 direction;   // normalized world direction the light travels
    ColorRgba color;  // rgb premultiplied by intensity; alpha unused
};

/// Scene lighting for one frame. Carries no generated headers.
struct AuthoredLightState {
    ColorRgba ambient;
    std::uint8_t directionalCount;
    AuthoredDirectionalLight directional[kMaxDirectionalLights];
};

/// Camera state gathered from the generated camera for one frame. This type
/// carries no generated headers so the submission contract stays host-testable.
struct AuthoredCameraState {
    Matrix4x4 worldFromCamera;
    float nearPlane;
    float farPlane;
    AuthoredViewport viewport;
    bool hasRenderTarget;
    bool clearColorEnabled;
    bool clearDepthEnabled;
    bool clearStencilEnabled;
    ColorRgba clearColor;
    // Vertical field of view in radians, read from the authored camera so one
    // scene frames the same way on every platform.
    float verticalFovRadians;
};

/// One drawable submission: the entity world matrix, its converted mesh and
/// the bound material color. Mesh storage is borrowed for the frame only.
struct AuthoredDrawState {
    Matrix4x4 worldFromModel;
    MeshView mesh;
    bool hasMaterial;
    ColorRgba materialColor;
    // True when the bound material asks for lighting; requires mesh.hasNormals.
    bool lit;
};

enum class AuthoredFrameStatus : std::uint8_t {
    Ok,
    NotStarted,
    NoCamera,
    MultipleCameras,
    InvalidCamera,
    InvalidCameraTransform,
    InvalidClear,
    NoDrawable,
    MultipleDrawables,
    InvalidMesh,
    InvalidMaterial,
    InvalidModelTransform,
    TooManyDirectionalLights,
    InvalidLight,
    LitMeshWithoutNormals,
    GeometryFailed,
    PresentFailed,
};

const char* authoredFrameStatusName(AuthoredFrameStatus status) noexcept;

/// Counts recorded by the last successful or failed assembly.
struct AuthoredFrameSummary {
    std::uint8_t cameraCount;
    std::uint8_t drawableCount;
    std::uint8_t directionalLightCount;
    bool lit;
    std::uint16_t triangleCount;
    std::uint16_t clippedTriangleCount;
    std::uint16_t rejectedTriangleCount;
    ColorRgba clearColor;
};

/// Receives exactly one validated frame per successful assembly.
class IAuthoredFrameSink {
public:
    virtual ~IAuthoredFrameSink() = default;
    virtual bool present(const GeometryFrame& geometry,
                         const ColorRgba& clearColor) noexcept = 0;
};

/// Assembles one authored frame from exactly one camera and one drawable,
/// validates every input before any packet can be produced, and presents the
/// resulting geometry to a sink at most once. Storage is allocated once at
/// construction; frames reuse it.
class AuthoredFrameAssembler {
public:
    AuthoredFrameAssembler();

    void begin(float viewportWidth, float viewportHeight) noexcept;
    AuthoredFrameStatus setCamera(const AuthoredCameraState& camera) noexcept;
    AuthoredFrameStatus setLights(const AuthoredLightState& lights) noexcept;
    AuthoredFrameStatus addDrawable(const AuthoredDrawState& drawable) noexcept;
    AuthoredFrameStatus present(IAuthoredFrameSink& sink) noexcept;

    const AuthoredFrameSummary& summary() const noexcept;
    const GeometryFrame& geometry() const noexcept;

private:
    static constexpr std::uint16_t kMaxVertices = 64;

    std::unique_ptr<GeometryFrame> frame_;
    SourceVertex coloredVertices_[kMaxVertices]{};
    AuthoredCameraState camera_{};
    AuthoredLightState lights_{};
    AuthoredDrawState drawable_{};
    AuthoredFrameSummary summary_{};
    float viewportWidth_ = 0.0f;
    float viewportHeight_ = 0.0f;
    bool began_ = false;
    bool cameraValid_ = false;
    bool drawableValid_ = false;
    AuthoredFrameStatus firstFailure_ = AuthoredFrameStatus::Ok;
};

} // namespace helengine::dreamcast
