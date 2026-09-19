#pragma once

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "DreamcastAuthoredFrame.hpp"

#include "RenderManager2D.hpp"

namespace helengine::dreamcast {

/// Minimum Core-compatible 2D adapter.
///
/// Textures are cooked offline into the dc-texture-v1 form and loaded through
/// BuildTextureFromCooked, which the generated scene asset reference resolver
/// calls for every texture a scene references. Raw, runtime-built textures are
/// not supported on this platform.
///
/// Sprites are gathered into the frame assembler the runtime host installs.
/// That assembler is the same object DreamcastRenderManager3D gathers geometry
/// into, which is what puts both in one presented frame; neither manager owns
/// it.
class DreamcastRenderManager2D final : public ::RenderManager2D {
public:
    /// Installs (or clears with nullptr) the frame assembler sprites are
    /// gathered into. A draw request without an assembler fails rather than
    /// dereferencing.
    void installAssembler(AuthoredFrameAssembler* assembler) noexcept;

    void Dispose() override;

    ::RuntimeTexture* BuildTextureFromCooked(std::string cookedAssetPath,
                                             ::IContentStreamSource* contentStreamSource) override;
    ::RuntimeTexture* BuildTextureFromRaw(::TextureAsset* data) override;
    void ReleaseTexture(::RuntimeTexture* texture) override;
    void DrawRoundedRect(::IRoundedRectDrawable2D* shape) override;
    void DrawSprite(::ISpriteDrawable2D* sprite) override;
    void DrawText(::ITextDrawable2D* text) override;

protected:
    void UpdateTextureRegionCore(::RuntimeTexture* texture,
                                 int32_t x,
                                 int32_t y,
                                 int32_t width,
                                 int32_t height,
                                 ::Array<uint8_t>* rgba8,
                                 int32_t sourceRowPitch) override;

private:
    AuthoredFrameAssembler* assembler_ = nullptr;
};

} // namespace helengine::dreamcast

#endif
