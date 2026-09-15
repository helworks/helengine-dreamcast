#include "DreamcastRuntimeMaterial.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "MaterialBlendMode.hpp"
#include "MaterialCullMode.hpp"
#include "MaterialRenderState.hpp"

#include <memory>
#include <stdexcept>

namespace helengine::dreamcast {

DreamcastRuntimeMaterial::DreamcastRuntimeMaterial() : ::RuntimeMaterial() {
    set_LightingModel(::RuntimeMaterialLightingModel::Unlit);
    set_SupportsNormalMapping(false);
    set_SupportsEmissive(false);
    set_CastsShadows(false);
    set_ReceivesShadows(false);
}

DreamcastRuntimeMaterial* DreamcastRuntimeMaterial::Create(
    const ::PlatformMaterialAsset& asset) {
    if (asset.RendererFamilyId != "dc-unlit" || !asset.TextureRelativePath.empty() ||
        asset.DoubleSided || asset.UseVertexColor || asset.Lit || asset.BaseColorA != 255) {
        throw std::invalid_argument("Dreamcast material does not match the opaque dc-unlit contract.");
    }
    auto runtime = std::unique_ptr<DreamcastRuntimeMaterial>(new DreamcastRuntimeMaterial());
    try {
        runtime->set_Id(asset.Id);
        runtime->color_ = ColorRgba{
            static_cast<float>(asset.BaseColorR) / 255.0f,
            static_cast<float>(asset.BaseColorG) / 255.0f,
            static_cast<float>(asset.BaseColorB) / 255.0f,
            1.0f,
        };
        ::MaterialRenderState* state = runtime->get_RenderState();
        state->set_BlendMode(::MaterialBlendMode::Opaque);
        state->set_CullMode(::MaterialCullMode::Back);
        state->set_DepthTestEnabled(true);
        state->set_DepthWriteEnabled(true);
        return runtime.release();
    } catch (...) {
        throw;
    }
}

DreamcastRuntimeMaterial::~DreamcastRuntimeMaterial() {
    Dispose();
}

void DreamcastRuntimeMaterial::Dispose() {
    if (!disposed_) {
        disposed_ = true;
        ::RuntimeMaterial::Dispose();
    }
}

ColorRgba DreamcastRuntimeMaterial::color() const noexcept {
    return color_;
}

} // namespace helengine::dreamcast

#endif
