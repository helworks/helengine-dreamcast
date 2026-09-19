#pragma once

#include "DreamcastGeometryPipeline.hpp"

#include <cstdint>

namespace helengine::dreamcast {

/// Maximum simultaneously submitted sprites for one authored frame.
constexpr std::uint16_t kMaxSprites = 64;

/// One authored sprite submission, drawn as a screen-space overlay on top of
/// the 3D scene. Carries no generated engine headers and no console SDK
/// headers so it stays host-testable.
struct AuthoredSpriteState {
    float destinationX;      // screen pixels, left
    float destinationY;      // screen pixels, top
    float destinationWidth;  // screen pixels
    float destinationHeight; // screen pixels
    float sourceU0;          // normalized, within the used sub-rectangle
    float sourceV0;
    float sourceU1;
    float sourceV1;
    const void* texture;     // opaque handle; the raster stage knows its real type
    ColorRgba tint;
};

/// Sprite overlay submitted for one frame, in submission order.
struct SpriteFrame {
    AuthoredSpriteState sprites[kMaxSprites];
    std::uint16_t spriteCount;
};

/// A source sub-rectangle in the form the engine authors it: origin and
/// extent, normalized against the *real image*, so (0, 0, 1, 1) is the whole
/// image. This is what ISpriteDrawable2D::get_SourceRect returns.
struct SpriteSourceRect {
    float x;
    float y;
    float width;
    float height;
};

/// The UV span the raster stage samples with, normalized against the padded
/// power-of-two square the texture is actually stored in.
struct SpriteSourceSpan {
    float u0;
    float v0;
    float u1;
    float v1;
};

/// Rewrites an engine source rectangle into the padded-texture UV span.
///
/// A cooked Dreamcast texture stores its image in the top-left corner of a
/// larger power-of-two square and the PVR samples the whole square, so the
/// real image occupies only usedWidth/paddedSide of the U axis and
/// usedHeight/paddedSide of the V axis. An engine source rectangle is
/// normalized against the image, not the square, so it has to be rescaled
/// before it can be handed to the raster stage.
///
/// This is deliberately total and decides nothing: a degenerate texture (a
/// zero padded side, a used extent larger than the square) or a degenerate
/// rectangle produces a non-finite, out-of-range or inverted span, which the
/// frame assembler rejects through its InvalidSprite path. Division is done
/// in float so a zero padded side yields infinity rather than trapping.
constexpr SpriteSourceSpan spriteSourceSpan(const SpriteSourceRect& rect,
                                            std::uint16_t usedWidth,
                                            std::uint16_t usedHeight,
                                            std::uint16_t paddedSide) noexcept {
    const float side = static_cast<float>(paddedSide);
    const float uScale = static_cast<float>(usedWidth) / side;
    const float vScale = static_cast<float>(usedHeight) / side;
    return SpriteSourceSpan{rect.x * uScale,
                            rect.y * vScale,
                            (rect.x + rect.width) * uScale,
                            (rect.y + rect.height) * vScale};
}

} // namespace helengine::dreamcast
