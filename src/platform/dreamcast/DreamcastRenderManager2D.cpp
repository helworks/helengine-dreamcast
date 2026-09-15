#include "DreamcastRenderManager2D.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include <stdexcept>

namespace helengine::dreamcast {

::RuntimeTexture* DreamcastRenderManager2D::BuildTextureFromRaw(::TextureAsset*) {
    throw std::runtime_error("Dreamcast 2D raw texture support is not implemented.");
}

void DreamcastRenderManager2D::DrawRoundedRect(::IRoundedRectDrawable2D*) {
    throw std::runtime_error("Dreamcast 2D rounded-rectangle support is not implemented.");
}

void DreamcastRenderManager2D::DrawSprite(::ISpriteDrawable2D*) {
    throw std::runtime_error("Dreamcast 2D sprite support is not implemented.");
}

void DreamcastRenderManager2D::DrawText(::ITextDrawable2D*) {
    throw std::runtime_error("Dreamcast 2D text support is not implemented.");
}

void DreamcastRenderManager2D::UpdateTextureRegionCore(::RuntimeTexture*,
                                                       int32_t,
                                                       int32_t,
                                                       int32_t,
                                                       int32_t,
                                                       ::Array<uint8_t>*,
                                                       int32_t) {
    throw std::runtime_error("Dreamcast 2D texture updates are not implemented.");
}

} // namespace helengine::dreamcast

#endif
