#include "DreamcastRenderManager3D.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "AmbientLightComponent.hpp"
#include "Asset.hpp"
#include "AssetSerializer.hpp"
#include "CameraClearSettings.hpp"
#include "Core.hpp"
#include "DirectionalLightComponent.hpp"
#include "DreamcastAuthoredTransform.hpp"
#include "DreamcastRuntimeMaterial.hpp"
#include "DreamcastRuntimeModel.hpp"
#include "Entity.hpp"
#include "ICamera.hpp"
#include "IContentStreamSource.hpp"
#include "IDrawable3D.hpp"
#include "IRenderQueue3D.hpp"
#include "IRenderVisitor3D.hpp"
#include "LightComponent.hpp"
#include "ModelAsset.hpp"
#include "ObjectManager.hpp"
#include "PlatformMaterialAsset.hpp"
#include "float4.hpp"
#include "float4x4.hpp"
#include "int2.hpp"
#include "runtime/array.hpp"
#include "runtime/native_list.hpp"
#include "system/io/stream.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace helengine::dreamcast {

namespace {

struct StreamLease {
    ::Stream* value;

    ~StreamLease() {
        if (value != nullptr) {
            value->Dispose();
            delete value;
        }
    }
};

void destroyAsset(::Asset* asset) noexcept {
    if (asset == nullptr) {
        return;
    }
    if (auto* disposable = dynamic_cast<::IDisposable*>(asset); disposable != nullptr) {
        disposable->Dispose();
    }
    if (asset->FormerAuthoringAssetIds != nullptr &&
        asset->FormerAuthoringAssetIds != ::Array<std::string>::Empty()) {
        delete asset->FormerAuthoringAssetIds;
    }
    asset->FormerAuthoringAssetIds = nullptr;
    delete asset;
}

template <typename TAsset>
TAsset* deserializeAsset(std::string path, ::IContentStreamSource* source) {
    if (source == nullptr) {
        throw std::invalid_argument("Dreamcast content stream source cannot be null.");
    }
    ::Stream* stream = source->OpenRead(path);
    if (stream == nullptr) {
        throw std::runtime_error("Dreamcast content source returned a null stream.");
    }
    StreamLease streamLease{stream};
    ::Asset* asset = ::AssetSerializer::Deserialize(stream);
    TAsset* typedAsset = dynamic_cast<TAsset*>(asset);
    if (typedAsset == nullptr) {
        destroyAsset(asset);
        throw std::invalid_argument("Cooked asset has an unexpected generated type.");
    }
    return typedAsset;
}

[[noreturn]] void rejectFrame(AuthoredFrameStatus status) {
    throw std::runtime_error(std::string("Dreamcast authored frame rejected: ") +
                             authoredFrameStatusName(status));
}

/// Visits the camera's ordered queue and forwards every enabled, layer-visible
/// drawable to the assembler. The first assembler failure is retained and
/// raised after the generated visit completes.
class DrawableCollector final : public ::IRenderVisitor3D {
public:
    DrawableCollector(AuthoredFrameAssembler& assembler, std::uint16_t cameraLayerMask) noexcept
        : assembler_(assembler), cameraLayerMask_(cameraLayerMask) {
    }

    void Visit(::IDrawable3D* drawable) override {
        if (drawable == nullptr) {
            record(AuthoredFrameStatus::InvalidMesh);
            return;
        }
        ::Entity* owner = drawable->get_Parent();
        if (owner == nullptr || !owner->get_IsHierarchyEnabled() ||
            (owner->get_LayerMask() & cameraLayerMask_) == 0) {
            return;
        }
        AuthoredDrawState state{};
        state.worldFromModel = matrixFromGenerated(owner->get_WorldTransformMatrix());
        auto* model = dynamic_cast<DreamcastRuntimeModel*>(drawable->get_Model());
        state.mesh = model != nullptr ? model->meshView() : MeshView{nullptr, 0, nullptr, 0, false};
        ::Array<::RuntimeMaterial*>* materials = drawable->get_Materials();
        auto* material = materials != nullptr && materials->get_Length() > 0
                             ? dynamic_cast<DreamcastRuntimeMaterial*>((*materials)[0])
                             : nullptr;
        state.hasMaterial = material != nullptr;
        if (material != nullptr) {
            state.materialColor = material->color();
            state.lit = material->lit();
        }
        record(assembler_.addDrawable(state));
    }

    AuthoredFrameStatus status() const noexcept {
        return status_;
    }

private:
    void record(AuthoredFrameStatus status) noexcept {
        if (status_ == AuthoredFrameStatus::Ok) {
            status_ = status;
        }
    }

    AuthoredFrameAssembler& assembler_;
    std::uint16_t cameraLayerMask_;
    AuthoredFrameStatus status_ = AuthoredFrameStatus::Ok;
};

/// Reads the enabled scene lights into the generated-header-free light state.
/// Ambient contributions sum; directional lights keep authored order and are
/// capped by the assembler, which fails rather than dropping a fifth light.
AuthoredLightState gatherLights(::ObjectManager& objectManager) {
    AuthoredLightState lights{};
    lights.ambient = ColorRgba{0.0f, 0.0f, 0.0f, 1.0f};
    ::List<::AmbientLightComponent*>* ambientLights = objectManager.get_AmbientLights();
    if (ambientLights != nullptr) {
        for (int32_t index = 0; index < ambientLights->get_Count(); ++index) {
            ::AmbientLightComponent* light = ambientLights->get_Item(index);
            if (light == nullptr) {
                continue;
            }
            ::Entity* owner = light->get_Parent();
            if (owner == nullptr || !owner->get_IsHierarchyEnabled()) {
                continue;
            }
            const ::float4 color = light->get_Color();
            const float intensity = light->get_Intensity();
            lights.ambient.r += color.X * intensity;
            lights.ambient.g += color.Y * intensity;
            lights.ambient.b += color.Z * intensity;
        }
    }

    ::List<::DirectionalLightComponent*>* directionalLights = objectManager.get_DirectionalLights();
    if (directionalLights != nullptr) {
        for (int32_t index = 0; index < directionalLights->get_Count(); ++index) {
            ::DirectionalLightComponent* light = directionalLights->get_Item(index);
            if (light == nullptr) {
                continue;
            }
            ::Entity* owner = light->get_Parent();
            if (owner == nullptr || !owner->get_IsHierarchyEnabled()) {
                continue;
            }
            if (lights.directionalCount >= kMaxDirectionalLights) {
                // Report the real count so the assembler fails explicitly.
                lights.directionalCount = static_cast<std::uint8_t>(kMaxDirectionalLights + 1);
                return lights;
            }
            const ::float4 orientation = owner->get_Orientation();
            const Vec3 forward = directionalLightForward(
                GeneratedQuaternion{orientation.X, orientation.Y, orientation.Z, orientation.W});
            const float lengthSquared =
                forward.x * forward.x + forward.y * forward.y + forward.z * forward.z;
            if (!(lengthSquared > 1.0e-12f)) {
                continue;
            }
            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            const ::float4 color = light->get_Color();
            const float intensity = light->get_Intensity();
            lights.directional[lights.directionalCount] = AuthoredDirectionalLight{
                Vec3{forward.x * inverseLength, forward.y * inverseLength, forward.z * inverseLength},
                ColorRgba{color.X * intensity, color.Y * intensity, color.Z * intensity, 1.0f}};
            ++lights.directionalCount;
        }
    }
    return lights;
}

AuthoredCameraState gatherCamera(::ICamera& camera) {
    ::Entity* owner = camera.get_Parent();
    if (owner == nullptr) {
        rejectFrame(AuthoredFrameStatus::InvalidCamera);
    }
    AuthoredCameraState state{};
    state.worldFromCamera = matrixFromGenerated(owner->get_WorldTransformMatrix());
    state.nearPlane = camera.get_NearPlaneDistance();
    state.farPlane = camera.get_FarPlaneDistance();
    const ::float4 viewport = camera.get_Viewport();
    state.viewport = AuthoredViewport{viewport.X, viewport.Y, viewport.Z, viewport.W};
    state.hasRenderTarget = camera.get_RenderTarget() != nullptr;
    ::CameraClearSettings clear = camera.get_ClearSettings();
    state.clearColorEnabled = clear.get_ClearColorEnabled();
    state.clearDepthEnabled = clear.get_ClearDepthEnabled();
    state.clearStencilEnabled = clear.get_ClearStencilEnabled();
    const ::float4 clearColor = clear.get_ClearColor();
    state.clearColor = ColorRgba{clearColor.X, clearColor.Y, clearColor.Z, clearColor.W};
    return state;
}

} // namespace

DreamcastRenderManager3D::DreamcastRenderManager3D() : ::RenderManager3D() {
}

void DreamcastRenderManager3D::installFrameSink(IAuthoredFrameSink* sink) noexcept {
    sink_ = sink;
}

void DreamcastRenderManager3D::Draw() {
    ::Core* core = get_OwnerCore();
    ::ObjectManager* objectManager = core != nullptr ? core->get_ObjectManager() : nullptr;
    ::List<::ICamera*>* cameras = objectManager != nullptr ? objectManager->get_Cameras() : nullptr;
    const int32_t cameraCount = cameras != nullptr ? cameras->get_Count() : 0;
    if (sink_ == nullptr) {
        if (cameraCount == 0) {
            // RuntimeInitProbe: an empty scene performs zero work.
            lastSummary_ = AuthoredFrameSummary{};
            return;
        }
        throw std::runtime_error("Dreamcast authored draw requested before a frame sink was installed.");
    }

    const ::int2 windowSize = get_MainWindowSize();
    assembler_.begin(static_cast<float>(windowSize.X), static_cast<float>(windowSize.Y));
    AuthoredFrameStatus status = assembler_.setLights(gatherLights(*objectManager));
    for (int32_t index = 0; index < cameraCount && status == AuthoredFrameStatus::Ok; ++index) {
        ::ICamera* camera = cameras->get_Item(index);
        if (camera == nullptr) {
            status = AuthoredFrameStatus::InvalidCamera;
            break;
        }
        status = assembler_.setCamera(gatherCamera(*camera));
        if (status != AuthoredFrameStatus::Ok) {
            break;
        }
        DrawableCollector collector(assembler_, camera->get_LayerMask());
        ::IRenderQueue3D* queue = camera->get_RenderQueue3D();
        if (queue == nullptr) {
            status = AuthoredFrameStatus::NoDrawable;
            break;
        }
        queue->VisitOrdered(&collector);
        status = collector.status();
    }
    if (status == AuthoredFrameStatus::Ok) {
        status = assembler_.present(*sink_);
    }
    lastSummary_ = assembler_.summary();
    if (status != AuthoredFrameStatus::Ok) {
        rejectFrame(status);
    }
    ++presentedFrames_;
}

void DreamcastRenderManager3D::Dispose() {
    sink_ = nullptr;
    ::RenderManager3D::Dispose();
}

const AuthoredFrameSummary& DreamcastRenderManager3D::lastFrameSummary() const noexcept {
    return lastSummary_;
}

std::uint32_t DreamcastRenderManager3D::presentedFrameCount() const noexcept {
    return presentedFrames_;
}

::RuntimeMaterial* DreamcastRenderManager3D::BuildMaterialFromCooked(
    ::PlatformMaterialAsset* materialAsset) {
    if (materialAsset == nullptr) {
        throw std::invalid_argument("Dreamcast material asset cannot be null.");
    }
    return DreamcastRuntimeMaterial::Create(*materialAsset);
}

::RuntimeMaterial* DreamcastRenderManager3D::BuildMaterialFromCooked(
    std::string cookedAssetPath,
    ::IContentStreamSource* contentStreamSource) {
    if (cookedAssetPath.empty()) {
        throw std::invalid_argument("Dreamcast cooked material path cannot be empty.");
    }
    ::PlatformMaterialAsset* asset =
        deserializeAsset<::PlatformMaterialAsset>(cookedAssetPath, contentStreamSource);
    try {
        ::RuntimeMaterial* runtime = BuildMaterialFromCooked(asset);
        destroyAsset(asset);
        return runtime;
    } catch (...) {
        destroyAsset(asset);
        throw;
    }
}

::RuntimeModel* DreamcastRenderManager3D::BuildModelFromCooked(
    std::string cookedAssetPath,
    ::IContentStreamSource* contentStreamSource) {
    if (cookedAssetPath.empty()) {
        throw std::invalid_argument("Dreamcast cooked model path cannot be empty.");
    }
    ::ModelAsset* asset = deserializeAsset<::ModelAsset>(cookedAssetPath, contentStreamSource);
    return DreamcastRuntimeModel::CreateOwned(asset);
}

::RuntimeModel* DreamcastRenderManager3D::BuildModelFromRaw(::ModelAsset* data) {
    (void)data;
    throw std::runtime_error("Dreamcast raw model creation is unsupported; use cooked assets.");
}

void DreamcastRenderManager3D::ReleaseMaterial(::RuntimeMaterial* material) {
    if (material == nullptr) {
        throw std::invalid_argument("Dreamcast material cannot be null.");
    }
    material->Dispose();
    delete material;
}

void DreamcastRenderManager3D::ReleaseModel(::RuntimeModel* model) {
    if (model == nullptr) {
        throw std::invalid_argument("Dreamcast model cannot be null.");
    }
    model->Dispose();
    delete model;
}

} // namespace helengine::dreamcast

#endif
