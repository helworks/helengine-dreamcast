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

} // namespace helengine::dreamcast
