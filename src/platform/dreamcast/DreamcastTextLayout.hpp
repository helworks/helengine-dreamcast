#pragma once

#include "DreamcastSpriteFrame.hpp"

#include <cstdint>

namespace helengine::dreamcast {

constexpr std::uint16_t kMaxFontGlyphMetrics = 256;
constexpr std::uint16_t kMaxTextCodepoints = 1024;

struct AuthoredFontGlyph {
    std::uint32_t codepoint;
    float sourceU;
    float sourceV;
    float sourceWidth;
    float sourceHeight;
    float offsetY;
    float advanceWidth;
};

struct AuthoredFontView {
    std::uint16_t atlasWidth;
    std::uint16_t atlasHeight;
    std::uint16_t paddedSide;
    float lineHeight;
    float spaceWidth;
    const AuthoredFontGlyph* glyphs;
    std::uint16_t glyphCount;
    const void* texture;
};

enum class TextHorizontalAlignment : std::uint8_t { Left, Center, Right };

struct TextLayoutInput {
    const char* utf8Text;
    float originX;
    float originY;
    float boxWidth;
    float boxHeight;
    float fontScale;
    TextHorizontalAlignment alignment;
    bool wrap;
    ColorRgba color;
};

struct GlyphLayoutFrame {
    AuthoredGlyphState glyphs[kMaxGlyphs]{};
    std::uint16_t glyphCount = 0;
};

enum class TextLayoutStatus : std::uint8_t {
    Ok,
    InvalidFont,
    InvalidText,
    InvalidScale,
    InvalidInput,
    TextTooLong,
    TooManyGlyphs
};

/// Lays out UTF-8 text using the authored atlas and metrics; output is bounded
/// and carries UVs normalized to the padded Dreamcast texture square.
TextLayoutStatus layoutAuthoredText(const AuthoredFontView& font,
                                    const TextLayoutInput& input,
                                    GlyphLayoutFrame& output) noexcept;

} // namespace helengine::dreamcast
