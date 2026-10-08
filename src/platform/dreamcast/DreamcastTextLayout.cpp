#include "DreamcastTextLayout.hpp"

#include <cmath>
#include <limits>

namespace helengine::dreamcast {
namespace {

bool finite(float value) noexcept { return std::isfinite(value); }

void reset(GlyphLayoutFrame& output) noexcept {
    // GlyphLayoutFrame is deliberately fixed-capacity and several KiB large.
    // Reset only its live count; aggregate assignment would materialize a
    // second full frame on the Dreamcast's small thread stack.
    output.glyphCount = 0;
}

bool finiteColor(const ColorRgba& c) noexcept {
    return finite(c.r) && finite(c.g) && finite(c.b) && finite(c.a);
}

const AuthoredFontGlyph* findGlyph(const AuthoredFontView& font,
                                   std::uint32_t codepoint) noexcept {
    for (std::uint16_t index = 0; index < font.glyphCount; ++index) {
        if (font.glyphs[index].codepoint == codepoint) return &font.glyphs[index];
    }
    return nullptr;
}

bool validFont(const AuthoredFontView& font) noexcept {
    if (font.atlasWidth == 0 || font.atlasHeight == 0 || font.paddedSide == 0 ||
        font.atlasWidth > font.paddedSide || font.atlasHeight > font.paddedSide ||
        font.paddedSide > 512 || !finite(font.lineHeight) || font.lineHeight <= 0.0f ||
        !finite(font.spaceWidth) || font.spaceWidth < 0.0f || font.texture == nullptr ||
        font.glyphCount > kMaxFontGlyphMetrics ||
        (font.glyphCount != 0 && font.glyphs == nullptr)) return false;
    for (std::uint16_t i = 0; i < font.glyphCount; ++i) {
        const AuthoredFontGlyph& glyph = font.glyphs[i];
        if (glyph.codepoint > 0x10ffffu ||
            (glyph.codepoint >= 0xd800u && glyph.codepoint <= 0xdfffu) ||
            !finite(glyph.sourceU) || !finite(glyph.sourceV) ||
            !finite(glyph.sourceWidth) || !finite(glyph.sourceHeight) ||
            !finite(glyph.offsetY) || !finite(glyph.advanceWidth) ||
            glyph.sourceU < 0.0f || glyph.sourceV < 0.0f ||
            glyph.sourceWidth <= 0.0f || glyph.sourceHeight <= 0.0f ||
            glyph.sourceU + glyph.sourceWidth > 1.0f ||
            glyph.sourceV + glyph.sourceHeight > 1.0f ||
            !finite(glyph.sourceWidth * static_cast<float>(font.atlasWidth)) ||
            glyph.sourceWidth * static_cast<float>(font.atlasWidth) <= 0.0f ||
            !finite(glyph.sourceHeight * static_cast<float>(font.atlasHeight)) ||
            glyph.sourceHeight * static_cast<float>(font.atlasHeight) <= 0.0f) return false;
        for (std::uint16_t previous = 0; previous < i; ++previous) {
            if (font.glyphs[previous].codepoint == glyph.codepoint) return false;
        }
    }
    return true;
}

bool decodeUtf8(const char* text, std::uint32_t* codepoints,
                std::uint16_t& count, bool& tooLong) noexcept {
    count = 0;
    tooLong = false;
    if (text == nullptr) return true;
    const auto* bytes = reinterpret_cast<const unsigned char*>(text);
    std::uint32_t index = 0;
    while (bytes[index] != 0) {
        if (count >= kMaxTextCodepoints) { tooLong = true; return false; }
        const unsigned char first = bytes[index++];
        std::uint32_t codepoint = 0;
        std::uint32_t continuationCount = 0;
        if (first < 0x80) codepoint = first;
        else if ((first & 0xe0u) == 0xc0u) { codepoint = first & 0x1fu; continuationCount = 1; }
        else if ((first & 0xf0u) == 0xe0u) { codepoint = first & 0x0fu; continuationCount = 2; }
        else if ((first & 0xf8u) == 0xf0u) { codepoint = first & 0x07u; continuationCount = 3; }
        else return false;
        for (std::uint32_t part = 0; part < continuationCount; ++part) {
            const unsigned char next = bytes[index++];
            if (next == 0 || (next & 0xc0u) != 0x80u) return false;
            codepoint = (codepoint << 6) | (next & 0x3fu);
        }
        if ((continuationCount == 1 && codepoint < 0x80u) ||
            (continuationCount == 2 && codepoint < 0x800u) ||
            (continuationCount == 3 && codepoint < 0x10000u) ||
            codepoint > 0x10ffffu || (codepoint >= 0xd800u && codepoint <= 0xdfffu)) return false;
        codepoints[count++] = codepoint;
    }
    return true;
}

double wrapAdvance(const AuthoredFontView& font, std::uint32_t codepoint) noexcept {
    if (codepoint == ' ') return font.spaceWidth;
    const AuthoredFontGlyph* glyph = findGlyph(font, codepoint);
    if (glyph == nullptr) return 0.0;
    if (glyph->advanceWidth > 0.0f) return glyph->advanceWidth;
    return static_cast<double>(glyph->sourceWidth) * font.atlasWidth;
}

bool wrapCodepoints(const AuthoredFontView& font, const std::uint32_t* source,
                    std::uint16_t sourceCount, std::uint32_t maxWidth,
                    std::uint32_t* destination, std::uint16_t& destinationCount) noexcept {
    destinationCount = 0;
    std::uint16_t lineStart = 0;
    std::int32_t lastSpace = -1;
    double lineWidth = 0.0;
    double widthAtLastSpace = 0.0;
    for (std::uint16_t index = 0; index < sourceCount;) {
        const std::uint32_t codepoint = source[index];
        if (codepoint == '\r') { ++index; continue; }
        if (codepoint == '\n') {
            if (destinationCount >= kMaxTextCodepoints) return false;
            destination[destinationCount++] = '\n'; ++index;
            lineStart = destinationCount; lastSpace = -1;
            lineWidth = 0.0; widthAtLastSpace = 0.0;
            continue;
        }
        const double characterWidth = wrapAdvance(font, codepoint);
        if (lineWidth > 0.0 && lineWidth + characterWidth > maxWidth) {
            if (lastSpace >= static_cast<std::int32_t>(lineStart)) {
                destination[lastSpace] = '\n';
                lineStart = static_cast<std::uint16_t>(lastSpace + 1);
                lineWidth -= widthAtLastSpace;
            } else {
                if (destinationCount >= kMaxTextCodepoints) return false;
                destination[destinationCount++] = '\n';
                lineStart = destinationCount;
                lineWidth = 0.0;
                if (codepoint == ' ') { ++index; continue; }
            }
            lastSpace = -1;
            widthAtLastSpace = 0.0;
            continue; // retry the code point, as the engine's wrapper does
        }
        if (destinationCount >= kMaxTextCodepoints) return false;
        destination[destinationCount++] = codepoint;
        lineWidth += characterWidth;
        if (codepoint == ' ') {
            lastSpace = destinationCount - 1;
            widthAtLastSpace = lineWidth;
        }
        ++index;
    }
    return true;
}

double visibleLineWidth(const AuthoredFontView& font, const std::uint32_t* text,
                        std::uint16_t begin, std::uint16_t end, double scale) noexcept {
    double visible = 0.0;
    double offset = 0.0;
    const double space = font.spaceWidth * scale > 1.0 ? font.spaceWidth * scale : 1.0;
    for (std::uint16_t i = begin; i < end; ++i) {
        const std::uint32_t codepoint = text[i];
        if (codepoint == ' ') { offset += space; if (offset > visible) visible = offset; continue; }
        const auto* glyph = findGlyph(font, codepoint);
        if (glyph == nullptr) continue;
        double width = static_cast<double>(glyph->sourceWidth) * font.atlasWidth * scale;
        if (width < 1.0) width = 1.0;
        if (offset + width > visible) visible = offset + width;
        offset += (glyph->advanceWidth > 0.0f)
                      ? glyph->advanceWidth * scale
                      : static_cast<double>(glyph->sourceWidth) * font.atlasWidth * scale;
    }
    return visible;
}

double alignmentOffset(TextHorizontalAlignment alignment, double boxWidth,
                       double visibleWidth) noexcept {
    const double remaining = boxWidth - visibleWidth;
    if (remaining <= 0.0) return 0.0;
    if (alignment == TextHorizontalAlignment::Center) return remaining * 0.5;
    if (alignment == TextHorizontalAlignment::Right) return remaining;
    return 0.0;
}

} // namespace

TextLayoutStatus layoutAuthoredText(const AuthoredFontView& font,
                                    const TextLayoutInput& input,
                                    GlyphLayoutFrame& output) noexcept {
    reset(output);
    if (!validFont(font)) return TextLayoutStatus::InvalidFont;
    if (!finite(input.fontScale) || input.fontScale <= 0.0f) return TextLayoutStatus::InvalidScale;
    if (!finite(input.originX) || !finite(input.originY) || !finite(input.boxWidth) ||
        !finite(input.boxHeight) || !finiteColor(input.color) ||
        input.boxWidth < 0.0f || input.boxHeight < 0.0f ||
        (input.alignment != TextHorizontalAlignment::Left &&
         input.alignment != TextHorizontalAlignment::Center &&
         input.alignment != TextHorizontalAlignment::Right)) return TextLayoutStatus::InvalidInput;

    std::uint32_t source[kMaxTextCodepoints]{};
    std::uint32_t wrapped[kMaxTextCodepoints]{};
    std::uint16_t sourceCount = 0;
    bool textTooLong = false;
    if (!decodeUtf8(input.utf8Text, source, sourceCount, textTooLong)) {
        return textTooLong ? TextLayoutStatus::TextTooLong : TextLayoutStatus::InvalidText;
    }
    if (input.wrap && sourceCount != 0) {
        const double rawWidth = std::nearbyint(static_cast<double>(input.boxWidth) / input.fontScale);
        const std::uint32_t maxWidth = rawWidth < 1.0 ? 1u :
            rawWidth > std::numeric_limits<std::uint32_t>::max()
                ? std::numeric_limits<std::uint32_t>::max()
                : static_cast<std::uint32_t>(rawWidth);
        std::uint16_t wrappedCount = 0;
        if (!wrapCodepoints(font, source, sourceCount, maxWidth, wrapped, wrappedCount))
            return TextLayoutStatus::TextTooLong;
        sourceCount = wrappedCount;
        for (std::uint16_t i = 0; i < sourceCount; ++i) source[i] = wrapped[i];
    }

    const double scale = input.fontScale;
    const double lineHeight = font.lineHeight * scale > 1.0 ? font.lineHeight * scale : 1.0;
    if (!std::isfinite(lineHeight) || !std::isfinite(font.spaceWidth * scale))
        return TextLayoutStatus::InvalidInput;
    const double baseX = std::nearbyint(input.originX);
    const double baseY = std::nearbyint(input.originY);
    double verticalOffset = 0.0;
    std::uint16_t lineStart = 0;
    while (lineStart <= sourceCount) {
        std::uint16_t lineEnd = lineStart;
        while (lineEnd < sourceCount && source[lineEnd] != '\n') ++lineEnd;
        const double lineOffset = alignmentOffset(input.alignment, input.boxWidth,
                                                   visibleLineWidth(font, source, lineStart, lineEnd, scale));
        double x = 0.0;
        for (std::uint16_t i = lineStart; i < lineEnd; ++i) {
            const std::uint32_t codepoint = source[i];
            if (codepoint == ' ') { x += font.spaceWidth * scale; continue; }
            const AuthoredFontGlyph* metric = findGlyph(font, codepoint);
            if (metric == nullptr) continue;
            if (output.glyphCount >= kMaxGlyphs) {
                reset(output);
                return TextLayoutStatus::TooManyGlyphs;
            }
            const float width = metric->sourceWidth * font.atlasWidth * input.fontScale;
            const float height = metric->sourceHeight * font.atlasHeight * input.fontScale;
            const float uScale = static_cast<float>(font.atlasWidth) / font.paddedSide;
            const float vScale = static_cast<float>(font.atlasHeight) / font.paddedSide;
            output.glyphs[output.glyphCount++] = {
                static_cast<float>(baseX + lineOffset + x),
                static_cast<float>(baseY + std::nearbyint(verticalOffset) + metric->offsetY * scale),
                width, height,
                metric->sourceU * uScale,
                metric->sourceV * vScale,
                (metric->sourceU + metric->sourceWidth) * uScale,
                (metric->sourceV + metric->sourceHeight) * vScale,
                font.texture,
                input.color};
            const AuthoredGlyphState& emitted = output.glyphs[output.glyphCount - 1];
            if (!finite(emitted.destinationX) || !finite(emitted.destinationY) ||
                !finite(width) || width <= 0.0f || !finite(height) || height <= 0.0f) {
                reset(output);
                return TextLayoutStatus::InvalidInput;
            }
            x += metric->advanceWidth > 0.0f
                     ? metric->advanceWidth * scale
                     : width;
            if (!std::isfinite(x)) {
                reset(output);
                return TextLayoutStatus::InvalidInput;
            }
        }
        if (lineEnd == sourceCount) break;
        verticalOffset += lineHeight;
        if (!std::isfinite(verticalOffset)) {
            reset(output);
            return TextLayoutStatus::InvalidInput;
        }
        lineStart = static_cast<std::uint16_t>(lineEnd + 1);
    }
    return TextLayoutStatus::Ok;
}

} // namespace helengine::dreamcast
