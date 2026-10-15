#pragma once

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "DreamcastAuthoredFrame.hpp"
#include "DreamcastTextLayout.hpp"

#include "RenderManager2D.hpp"

#include <memory>
#include <vector>

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

    /// True when this manager gathers into exactly `assembler`.
    ///
    /// DreamcastRenderManager3D calls this before driving the 2D pass: the
    /// frame it is assembling is only coherent if the manager the Core
    /// dispatches sprites to is the one holding that same assembler. A null
    /// argument is never a match.
    bool sharesAssembler(const AuthoredFrameAssembler* assembler) const noexcept;

    /// Gathers every enabled 2D drawable of every camera into the installed
    /// assembler.
    ///
    /// This is the conventional 2D hook, but nothing in the generated Core
    /// calls it: Core::Draw drives only RenderManager3D::Draw on this
    /// platform. DreamcastRenderManager3D calls it from between its own
    /// gather and present so both lists land in one frame.
    void Draw() override;

    void Dispose() override;

    ::RuntimeTexture* BuildTextureFromCooked(::HeCppString cookedAssetPath,
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
    std::vector<AuthoredFontGlyph> fontGlyphScratch_;
    std::unique_ptr<GlyphLayoutFrame> glyphLayoutScratch_;
};

} // namespace helengine::dreamcast

#endif
