#include "DreamcastRenderManager3D.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "Asset.hpp"
#include "AssetSerializer.hpp"
#include "DreamcastRuntimeMaterial.hpp"
#include "DreamcastRuntimeModel.hpp"
#include "IContentStreamSource.hpp"
#include "ModelAsset.hpp"
#include "PlatformMaterialAsset.hpp"
#include "system/io/stream.hpp"

#include <stdexcept>

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

} // namespace

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
