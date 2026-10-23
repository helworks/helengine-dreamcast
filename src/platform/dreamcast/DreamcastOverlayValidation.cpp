#include "DreamcastOverlayValidation.hpp"

#include <cmath>

namespace helengine::dreamcast::renderer_detail {
namespace {

bool finiteColor(const ColorRgba& c) noexcept {
    return std::isfinite(c.r) && std::isfinite(c.g) &&
           std::isfinite(c.b) && std::isfinite(c.a);
}

bool validTexture(const void* handle) noexcept {
    if (handle == nullptr) return false;
    const auto& texture = *static_cast<const SpriteTexture*>(handle);
    return texture.vram != nullptr && texture.paddedSide >= kMinSpriteTextureSide &&
           texture.paddedSide <= kMaxSpriteTextureSide &&
           (texture.paddedSide & static_cast<std::uint16_t>(texture.paddedSide - 1)) == 0 &&
           texture.usedWidth > 0 && texture.usedHeight > 0 &&
           texture.usedWidth <= texture.paddedSide && texture.usedHeight <= texture.paddedSide;
}

bool validDestination(float x, float y, float width, float height) noexcept {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(width) &&
           std::isfinite(height) && width > 0.0f && height > 0.0f &&
           std::isfinite(x + width) && std::isfinite(y + height);
}

bool validSprite(const AuthoredSpriteState& sprite) noexcept {
    if (sprite.hasResolvedCorners) {
        // Shared-core sprites carry affine corners rather than a destination
        // rectangle. Match the assembler's finite-input contract without
        // requiring the unused width and height to be positive.
        if (!std::isfinite(sprite.destinationX) || !std::isfinite(sprite.destinationY) ||
            !std::isfinite(sprite.destinationWidth) || !std::isfinite(sprite.destinationHeight))
            return false;
        for (unsigned i = 0; i < 4; ++i)
            if (!std::isfinite(sprite.cornerX[i]) || !std::isfinite(sprite.cornerY[i]))
                return false;
    } else if (!validDestination(sprite.destinationX, sprite.destinationY,
                                 sprite.destinationWidth, sprite.destinationHeight)) {
        return false;
    }
    return validTexture(sprite.texture) && finiteColor(sprite.tint) &&
           std::isfinite(sprite.rotationCos) && std::isfinite(sprite.rotationSin) &&
           sprite.sourceU0 >= 0.0f && sprite.sourceU0 < sprite.sourceU1 &&
           sprite.sourceU1 <= 1.0f && sprite.sourceV0 >= 0.0f &&
           sprite.sourceV0 < sprite.sourceV1 && sprite.sourceV1 <= 1.0f;
}

bool validGlyph(const AuthoredGlyphState& glyph) noexcept {
    return validDestination(glyph.destinationX, glyph.destinationY,
                            glyph.destinationWidth, glyph.destinationHeight) &&
           validTexture(glyph.texture) && finiteColor(glyph.tint) &&
           std::isfinite(glyph.sourceU0) && std::isfinite(glyph.sourceV0) &&
           std::isfinite(glyph.sourceU1) && std::isfinite(glyph.sourceV1) &&
           glyph.sourceU0 >= 0.0f && glyph.sourceU0 < glyph.sourceU1 &&
           glyph.sourceU1 <= 1.0f && glyph.sourceV0 >= 0.0f &&
           glyph.sourceV0 < glyph.sourceV1 && glyph.sourceV1 <= 1.0f;
}

bool validRect(const AuthoredRoundedRectState& rect) noexcept {
    return validDestination(rect.destinationX, rect.destinationY,
                            rect.destinationWidth, rect.destinationHeight) &&
           std::isfinite(rect.radius) && rect.radius >= 0.0f &&
           std::isfinite(rect.borderThickness) && rect.borderThickness >= 0.0f &&
           (rect.roundedCorners & ~kRoundedRectAllCorners) == 0 &&
           finiteColor(rect.fillColor) && finiteColor(rect.borderColor);
}

} // namespace

bool validOverlayFrame(const AuthoredOverlayFrame& overlay) noexcept {
    const SpriteFrame& sprites = overlay.sprites;
    if (sprites.spriteCount > kMaxSprites || overlay.roundedRectCount > kMaxRoundedRects ||
        overlay.glyphCount > kMaxGlyphs || overlay.orderCount > kMaxOverlaySubmissions ||
        overlay.orderCount != sprites.spriteCount + overlay.roundedRectCount + overlay.glyphCount)
        return false;
    for (std::uint16_t i = 0; i < sprites.spriteCount; ++i)
        if (!validSprite(sprites.sprites[i])) return false;
    for (std::uint16_t i = 0; i < overlay.roundedRectCount; ++i)
        if (!validRect(overlay.roundedRects[i])) return false;
    for (std::uint16_t i = 0; i < overlay.glyphCount; ++i)
        if (!validGlyph(overlay.glyphs[i])) return false;

    bool seenSprites[kMaxSprites]{};
    bool seenRects[kMaxRoundedRects]{};
    bool seenGlyphs[kMaxGlyphs]{};
    for (std::uint16_t i = 0; i < overlay.orderCount; ++i) {
        const AuthoredOverlayOrder& item = overlay.order[i];
        switch (item.kind) {
        case AuthoredOverlayKind::Sprite:
            if (item.index >= sprites.spriteCount || seenSprites[item.index]) return false;
            seenSprites[item.index] = true;
            break;
        case AuthoredOverlayKind::RoundedRect:
            if (item.index >= overlay.roundedRectCount || seenRects[item.index]) return false;
            seenRects[item.index] = true;
            break;
        case AuthoredOverlayKind::Glyph:
            if (item.index >= overlay.glyphCount || seenGlyphs[item.index]) return false;
            seenGlyphs[item.index] = true;
            break;
        default:
            return false;
        }
    }
    return true;
}

} // namespace helengine::dreamcast::renderer_detail
