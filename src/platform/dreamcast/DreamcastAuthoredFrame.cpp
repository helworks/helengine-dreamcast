#include "DreamcastAuthoredFrame.hpp"

#include <cmath>

namespace helengine::dreamcast {
namespace {

constexpr float kViewportEpsilon = 1.0e-6f;

// A destination rectangle edge is rejected once it strays this many viewport
// widths/heights away from the viewport. This is a sanity bound, not a clip:
// it exists to exclude values that cannot be real screen coordinates (for
// example ones that overflow to infinity once the raster stage adds width to
// x), not to reject legitimate off-screen sprites.
constexpr float kSpriteDestinationBoundsMultiple = 16.0f;

bool finite(float value) noexcept {
    return std::isfinite(value);
}

bool finiteColor(const ColorRgba& color) noexcept {
    return finite(color.r) && finite(color.g) && finite(color.b) && finite(color.a);
}

bool approximately(float left, float right) noexcept {
    return std::fabs(left - right) <= kViewportEpsilon;
}

bool fullViewport(const AuthoredViewport& viewport) noexcept {
    return approximately(viewport.x, 0.0f) && approximately(viewport.y, 0.0f) &&
           approximately(viewport.width, 1.0f) && approximately(viewport.height, 1.0f);
}

bool finiteVec3(const Vec3& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

float clampUnit(float value) noexcept {
    if (!std::isfinite(value) || value <= 0.0f) {
        return 0.0f;
    }
    return value >= 1.0f ? 1.0f : value;
}

} // namespace

const char* authoredFrameStatusName(AuthoredFrameStatus status) noexcept {
    switch (status) {
    case AuthoredFrameStatus::Ok:
        return "ok";
    case AuthoredFrameStatus::NotStarted:
        return "frame-not-started";
    case AuthoredFrameStatus::NoCamera:
        return "no-camera";
    case AuthoredFrameStatus::MultipleCameras:
        return "multiple-cameras";
    case AuthoredFrameStatus::InvalidCamera:
        return "invalid-camera";
    case AuthoredFrameStatus::InvalidCameraTransform:
        return "invalid-camera-transform";
    case AuthoredFrameStatus::InvalidClear:
        return "invalid-clear-settings";
    case AuthoredFrameStatus::NoDrawable:
        return "no-drawable";
    case AuthoredFrameStatus::MultipleDrawables:
        return "multiple-drawables";
    case AuthoredFrameStatus::InvalidMesh:
        return "invalid-mesh";
    case AuthoredFrameStatus::InvalidMaterial:
        return "invalid-material";
    case AuthoredFrameStatus::InvalidModelTransform:
        return "invalid-model-transform";
    case AuthoredFrameStatus::TooManyDirectionalLights:
        return "too-many-directional-lights";
    case AuthoredFrameStatus::InvalidLight:
        return "invalid-light";
    case AuthoredFrameStatus::LitMeshWithoutNormals:
        return "lit-mesh-without-normals";
    case AuthoredFrameStatus::GeometryFailed:
        return "geometry-failed";
    case AuthoredFrameStatus::PresentFailed:
        return "present-failed";
    case AuthoredFrameStatus::TooManySprites:
        return "too-many-sprites";
    case AuthoredFrameStatus::InvalidSprite:
        return "invalid-sprite";
    }
    return "unknown";
}

AuthoredFrameAssembler::AuthoredFrameAssembler() : frame_(new GeometryFrame()) {
    resetGeometryFrame(*frame_);
}

void AuthoredFrameAssembler::begin(float viewportWidth, float viewportHeight) noexcept {
    viewportWidth_ = viewportWidth;
    viewportHeight_ = viewportHeight;
    began_ = true;
    cameraValid_ = false;
    drawableValid_ = false;
    firstFailure_ = AuthoredFrameStatus::Ok;
    summary_ = AuthoredFrameSummary{};
    lights_ = AuthoredLightState{};
    sprites_ = SpriteFrame{};
    resetGeometryFrame(*frame_);
}

AuthoredFrameStatus AuthoredFrameAssembler::setCamera(const AuthoredCameraState& camera) noexcept {
    const auto fail = [this](AuthoredFrameStatus status) noexcept {
        if (firstFailure_ == AuthoredFrameStatus::Ok) {
            firstFailure_ = status;
        }
        return status;
    };
    if (!began_) {
        return fail(AuthoredFrameStatus::NotStarted);
    }
    if (summary_.cameraCount != 0) {
        ++summary_.cameraCount;
        return fail(AuthoredFrameStatus::MultipleCameras);
    }
    summary_.cameraCount = 1;
    if (camera.hasRenderTarget || !finite(camera.nearPlane) || !finite(camera.farPlane) ||
        camera.nearPlane <= 0.0f || camera.farPlane <= camera.nearPlane ||
        !finite(camera.verticalFovRadians) ||
        camera.verticalFovRadians < kMinimumVerticalFovRadians ||
        camera.verticalFovRadians > kMaximumVerticalFovRadians ||
        !finite(camera.viewport.x) || !finite(camera.viewport.y) ||
        !finite(camera.viewport.width) || !finite(camera.viewport.height) ||
        !fullViewport(camera.viewport)) {
        return fail(AuthoredFrameStatus::InvalidCamera);
    }
    if (!camera.clearColorEnabled || !camera.clearDepthEnabled || camera.clearStencilEnabled ||
        !finiteColor(camera.clearColor)) {
        return fail(AuthoredFrameStatus::InvalidClear);
    }
    if (!validateRigidCameraTransform(camera.worldFromCamera)) {
        return fail(AuthoredFrameStatus::InvalidCameraTransform);
    }
    camera_ = camera;
    summary_.clearColor = camera.clearColor;
    cameraValid_ = true;
    return AuthoredFrameStatus::Ok;
}

AuthoredFrameStatus AuthoredFrameAssembler::setLights(const AuthoredLightState& lights) noexcept {
    const auto fail = [this](AuthoredFrameStatus status) noexcept {
        if (firstFailure_ == AuthoredFrameStatus::Ok) {
            firstFailure_ = status;
        }
        return status;
    };
    if (!began_) {
        return fail(AuthoredFrameStatus::NotStarted);
    }
    if (lights.directionalCount > kMaxDirectionalLights) {
        return fail(AuthoredFrameStatus::TooManyDirectionalLights);
    }
    if (!finiteColor(lights.ambient)) {
        return fail(AuthoredFrameStatus::InvalidLight);
    }
    for (std::uint8_t index = 0; index < lights.directionalCount; ++index) {
        const AuthoredDirectionalLight& light = lights.directional[index];
        const float lengthSquared = light.direction.x * light.direction.x +
                                    light.direction.y * light.direction.y +
                                    light.direction.z * light.direction.z;
        if (!finiteColor(light.color) || !finiteVec3(light.direction) || lengthSquared <= 1.0e-8f) {
            return fail(AuthoredFrameStatus::InvalidLight);
        }
    }
    lights_ = lights;
    summary_.directionalLightCount = lights.directionalCount;
    return AuthoredFrameStatus::Ok;
}

AuthoredFrameStatus AuthoredFrameAssembler::addDrawable(const AuthoredDrawState& drawable) noexcept {
    const auto fail = [this](AuthoredFrameStatus status) noexcept {
        if (firstFailure_ == AuthoredFrameStatus::Ok) {
            firstFailure_ = status;
        }
        return status;
    };
    if (!began_) {
        return fail(AuthoredFrameStatus::NotStarted);
    }
    if (summary_.drawableCount != 0) {
        ++summary_.drawableCount;
        return fail(AuthoredFrameStatus::MultipleDrawables);
    }
    summary_.drawableCount = 1;
    if (drawable.mesh.vertices == nullptr || drawable.mesh.triangles == nullptr ||
        drawable.mesh.vertexCount == 0 || drawable.mesh.vertexCount > kMaxVertices ||
        drawable.mesh.triangleCount == 0) {
        return fail(AuthoredFrameStatus::InvalidMesh);
    }
    if (!drawable.hasMaterial || !finiteColor(drawable.materialColor)) {
        return fail(AuthoredFrameStatus::InvalidMaterial);
    }
    if (!validateModelTransform(drawable.worldFromModel)) {
        return fail(AuthoredFrameStatus::InvalidModelTransform);
    }
    if (drawable.lit && !drawable.mesh.hasNormals) {
        return fail(AuthoredFrameStatus::LitMeshWithoutNormals);
    }
    for (std::uint16_t index = 0; index < drawable.mesh.vertexCount; ++index) {
        coloredVertices_[index] = SourceVertex{drawable.mesh.vertices[index].position,
                                               drawable.materialColor,
                                               drawable.mesh.vertices[index].normal};
    }
    summary_.lit = drawable.lit;
    drawable_ = drawable;
    drawable_.mesh.vertices = coloredVertices_;
    drawableValid_ = true;
    return AuthoredFrameStatus::Ok;
}

AuthoredFrameStatus AuthoredFrameAssembler::addSprite(const AuthoredSpriteState& sprite) noexcept {
    const auto fail = [this](AuthoredFrameStatus status) noexcept {
        if (firstFailure_ == AuthoredFrameStatus::Ok) {
            firstFailure_ = status;
        }
        return status;
    };
    if (!began_) {
        return fail(AuthoredFrameStatus::NotStarted);
    }
    // The bounds check happens before any write, as in setLights. Unlike
    // setLights, which tracks no count on overflow, this mirrors how
    // setCamera/addDrawable report a cardinality violation: the summary
    // count is pinned at exactly one past the maximum, not incremented again
    // on every further overflowing call.
    if (sprites_.spriteCount >= kMaxSprites) {
        summary_.spriteCount = kMaxSprites + 1;
        return fail(AuthoredFrameStatus::TooManySprites);
    }
    if (!finite(sprite.destinationX) || !finite(sprite.destinationY) ||
        !finite(sprite.destinationWidth) || !finite(sprite.destinationHeight) ||
        !finite(sprite.sourceU0) || !finite(sprite.sourceV0) || !finite(sprite.sourceU1) ||
        !finite(sprite.sourceV1) || !finiteColor(sprite.tint) ||
        sprite.destinationWidth <= 0.0f || sprite.destinationHeight <= 0.0f ||
        sprite.texture == nullptr ||
        sprite.sourceU0 < 0.0f || sprite.sourceU0 > 1.0f ||
        sprite.sourceV0 < 0.0f || sprite.sourceV0 > 1.0f ||
        sprite.sourceU1 < 0.0f || sprite.sourceU1 > 1.0f ||
        sprite.sourceV1 < 0.0f || sprite.sourceV1 > 1.0f ||
        sprite.sourceU1 <= sprite.sourceU0 || sprite.sourceV1 <= sprite.sourceV0) {
        return fail(AuthoredFrameStatus::InvalidSprite);
    }
    // The destination rectangle must lie within a generous multiple of the
    // viewport. A value out here cannot be a real screen coordinate, and
    // left unchecked it can overflow to infinity once the raster stage adds
    // width/height to x/y.
    const float boundX = kSpriteDestinationBoundsMultiple * viewportWidth_;
    const float boundY = kSpriteDestinationBoundsMultiple * viewportHeight_;
    const float destinationRight = sprite.destinationX + sprite.destinationWidth;
    const float destinationBottom = sprite.destinationY + sprite.destinationHeight;
    if (sprite.destinationX < -boundX || sprite.destinationX > boundX ||
        sprite.destinationY < -boundY || sprite.destinationY > boundY ||
        destinationRight < -boundX || destinationRight > boundX ||
        destinationBottom < -boundY || destinationBottom > boundY) {
        return fail(AuthoredFrameStatus::InvalidSprite);
    }
    sprites_.sprites[sprites_.spriteCount] = sprite;
    ++sprites_.spriteCount;
    summary_.spriteCount = sprites_.spriteCount;
    return AuthoredFrameStatus::Ok;
}

AuthoredFrameStatus AuthoredFrameAssembler::present(IAuthoredFrameSink& sink) noexcept {
    if (!began_) {
        return AuthoredFrameStatus::NotStarted;
    }
    began_ = false;
    if (firstFailure_ != AuthoredFrameStatus::Ok) {
        return firstFailure_;
    }
    if (summary_.cameraCount == 0 || !cameraValid_) {
        return AuthoredFrameStatus::NoCamera;
    }
    if (summary_.drawableCount == 0 || !drawableValid_) {
        return AuthoredFrameStatus::NoDrawable;
    }

    Matrix4x4 cameraFromModel{};
    if (!makeCameraFromModel(drawable_.worldFromModel, camera_.worldFromCamera, cameraFromModel)) {
        return AuthoredFrameStatus::InvalidModelTransform;
    }
    if (drawable_.lit) {
        Matrix4x4 normalMatrix{};
        if (!makeNormalMatrix(drawable_.worldFromModel, normalMatrix)) {
            return AuthoredFrameStatus::InvalidModelTransform;
        }
        for (std::uint16_t index = 0; index < drawable_.mesh.vertexCount; ++index) {
            const Vec3 worldNormal = transformDirection(normalMatrix, coloredVertices_[index].normal);
            const float lengthSquared = worldNormal.x * worldNormal.x +
                                        worldNormal.y * worldNormal.y +
                                        worldNormal.z * worldNormal.z;
            float diffuseRed = lights_.ambient.r;
            float diffuseGreen = lights_.ambient.g;
            float diffuseBlue = lights_.ambient.b;
            if (lengthSquared > 1.0e-12f) {
                const float inverseLength = 1.0f / std::sqrt(lengthSquared);
                const Vec3 unitNormal{worldNormal.x * inverseLength,
                                      worldNormal.y * inverseLength,
                                      worldNormal.z * inverseLength};
                for (std::uint8_t light = 0; light < lights_.directionalCount; ++light) {
                    const AuthoredDirectionalLight& source = lights_.directional[light];
                    const float lambert = -(unitNormal.x * source.direction.x +
                                            unitNormal.y * source.direction.y +
                                            unitNormal.z * source.direction.z);
                    if (lambert <= 0.0f) {
                        continue;
                    }
                    diffuseRed += source.color.r * lambert;
                    diffuseGreen += source.color.g * lambert;
                    diffuseBlue += source.color.b * lambert;
                }
            }
            ColorRgba& color = coloredVertices_[index].color;
            color = ColorRgba{clampUnit(drawable_.materialColor.r * diffuseRed),
                              clampUnit(drawable_.materialColor.g * diffuseGreen),
                              clampUnit(drawable_.materialColor.b * diffuseBlue),
                              1.0f};
        }
    }
    const GeometryConfig config{
        viewportWidth_,
        viewportHeight_,
        camera_.verticalFovRadians,
        camera_.nearPlane,
        camera_.farPlane,
    };
    const GeometryStatus status = processGeometry(drawable_.mesh, cameraFromModel, config, *frame_);
    if (status != GeometryStatus::Ok) {
        resetGeometryFrame(*frame_);
        return status == GeometryStatus::InvalidMesh ? AuthoredFrameStatus::InvalidMesh
                                                     : AuthoredFrameStatus::GeometryFailed;
    }
    summary_.triangleCount = frame_->triangleCount;
    summary_.clippedTriangleCount = frame_->clippedTriangleCount;
    summary_.rejectedTriangleCount = frame_->rejectedTriangleCount;
    if (!sink.present(*frame_, sprites_, camera_.clearColor)) {
        return AuthoredFrameStatus::PresentFailed;
    }
    return AuthoredFrameStatus::Ok;
}

const AuthoredFrameSummary& AuthoredFrameAssembler::summary() const noexcept {
    return summary_;
}

const GeometryFrame& AuthoredFrameAssembler::geometry() const noexcept {
    return *frame_;
}

const SpriteFrame& AuthoredFrameAssembler::sprites() const noexcept {
    return sprites_;
}

} // namespace helengine::dreamcast
