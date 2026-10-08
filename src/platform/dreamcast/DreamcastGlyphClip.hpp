#pragma once

#include "DreamcastAuthoredFrame.hpp"
#include <algorithm>

namespace helengine::dreamcast {

/// Intersects live ancestor clip rectangles once per text draw, then clips each
/// glyph and effect pass without visiting the entity hierarchy again.
class DreamcastGlyphClip {
public:
    /// Adds one ancestor's current world-space rectangle to the intersection.
    void include(float x, float y, float width, float height) noexcept {
        const float right = x + width;
        const float bottom = y + height;
        if (!hasBounds_) {
            left_ = x; top_ = y; right_ = right; bottom_ = bottom;
            hasBounds_ = true;
        } else {
            left_ = std::max(left_, x); top_ = std::max(top_, y);
            right_ = std::min(right_, right); bottom_ = std::min(bottom_, bottom);
        }
    }

    /// Crops a glyph to the ancestor intersection, preserving its atlas UVs.
    /// Fully contained glyphs retain their geometry without UV divisions.
    /// Returns false when the glyph is entirely outside the intersection.
    bool clip(AuthoredGlyphState& glyph) const noexcept {
        if (!hasBounds_) return true;
        const float glyphRight = glyph.destinationX + glyph.destinationWidth;
        const float glyphBottom = glyph.destinationY + glyph.destinationHeight;
        const float left = std::max(glyph.destinationX, left_);
        const float top = std::max(glyph.destinationY, top_);
        const float right = std::min(glyphRight, right_);
        const float bottom = std::min(glyphBottom, bottom_);
        if (right <= left || bottom <= top) return false;
        if (left == glyph.destinationX && top == glyph.destinationY &&
            right == glyphRight && bottom == glyphBottom) return true;

        const float u = (glyph.sourceU1 - glyph.sourceU0) / glyph.destinationWidth;
        const float v = (glyph.sourceV1 - glyph.sourceV0) / glyph.destinationHeight;
        glyph.sourceU1 -= (glyphRight - right) * u;
        glyph.sourceV1 -= (glyphBottom - bottom) * v;
        glyph.sourceU0 += (left - glyph.destinationX) * u;
        glyph.sourceV0 += (top - glyph.destinationY) * v;
        glyph.destinationX = left; glyph.destinationY = top;
        glyph.destinationWidth = right - left; glyph.destinationHeight = bottom - top;
        return true;
    }

private:
    /// Whether any ancestor supplied a clip rectangle for this text draw.
    bool hasBounds_ = false;
    /// Left, top, right and bottom of the intersection in world-space pixels.
    float left_ = 0.0f, top_ = 0.0f, right_ = 0.0f, bottom_ = 0.0f;
};

} // namespace helengine::dreamcast
