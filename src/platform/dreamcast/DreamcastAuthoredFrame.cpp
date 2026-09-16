#include "DreamcastAuthoredFrame.hpp"

#include <cmath>

namespace helengine::dreamcast {
namespace {

constexpr float kViewportEpsilon = 1.0e-6f;

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
    case AuthoredFrameStatus::GeometryFailed:
        return "geometry-failed";
    case AuthoredFrameStatus::PresentFailed:
        return "present-failed";
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
    for (std::uint16_t index = 0; index < drawable.mesh.vertexCount; ++index) {
        coloredVertices_[index] = SourceVertex{drawable.mesh.vertices[index].position,
                                               drawable.materialColor,
                                               drawable.mesh.vertices[index].normal};
    }
    drawable_ = drawable;
    drawable_.mesh.vertices = coloredVertices_;
    drawableValid_ = true;
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
    const GeometryConfig config{
        viewportWidth_,
        viewportHeight_,
        kAuthoredVerticalFovRadians,
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
    if (!sink.present(*frame_, camera_.clearColor)) {
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

} // namespace helengine::dreamcast
