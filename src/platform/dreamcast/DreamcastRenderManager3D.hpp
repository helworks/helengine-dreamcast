#pragma once

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "RenderManager3D.hpp"

namespace helengine::dreamcast {

class DreamcastRenderManager3D final : public ::RenderManager3D {
public:
    ::RuntimeMaterial* BuildMaterialFromCooked(::PlatformMaterialAsset* materialAsset) override;

    ::RuntimeMaterial* BuildMaterialFromCooked(
        std::string cookedAssetPath,
        ::IContentStreamSource* contentStreamSource) override;

    ::RuntimeModel* BuildModelFromCooked(
        std::string cookedAssetPath,
        ::IContentStreamSource* contentStreamSource) override;

    ::RuntimeModel* BuildModelFromRaw(::ModelAsset* data) override;

    void ReleaseMaterial(::RuntimeMaterial* material) override;

    void ReleaseModel(::RuntimeModel* model) override;
};

} // namespace helengine::dreamcast

#endif
