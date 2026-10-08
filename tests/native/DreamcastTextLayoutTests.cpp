#include "platform/dreamcast/DreamcastTextLayout.hpp"
#include "platform/dreamcast/DreamcastGlyphClip.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace helengine::dreamcast;
int failures = 0;
void expect(bool condition, const char* expression) {
    if (!condition) { std::cerr << "expected " << expression << '\n'; ++failures; }
}
#define EXPECT(x) expect((x), #x)
int atlas = 0;
AuthoredFontGlyph glyph(char32_t code, float x, float y, float w, float h,
                        float offsetY, float advance) {
    return {static_cast<std::uint32_t>(code), x, y, w, h, offsetY, advance};
}
AuthoredFontGlyph glyphs[] = {
    glyph(U'A', 0.0f, 0.0f, 0.25f, 0.5f, 2.0f, 6.0f),
    glyph(U'B', 0.25f, 0.0f, 0.25f, 0.5f, 0.0f, 0.0f),
    glyph(U'é', 0.5f, 0.0f, 0.25f, 0.5f, 0.0f, 7.0f),
};
AuthoredFontView font() { return {64, 32, 128, 12.0f, 3.0f, glyphs, 3, &atlas}; }
TextLayoutInput input(const char* text) {
    return {text, 0.0f, 0.0f, 40.0f, 80.0f, 1.0f, TextHorizontalAlignment::Left, false,
            {1, 1, 1, 1}};
}

void GlyphGeometryOffsetAndPaddedAtlasUvArePreserved() {
    auto in = input("A");
    GlyphLayoutFrame out{};
    EXPECT(layoutAuthoredText(font(), in, out) == TextLayoutStatus::Ok);
    EXPECT(out.glyphCount == 1);
    EXPECT(out.glyphs[0].destinationX == 0.0f);
    EXPECT(out.glyphs[0].destinationY == 2.0f);
    EXPECT(out.glyphs[0].destinationWidth == 16.0f);
    EXPECT(out.glyphs[0].destinationHeight == 16.0f);
    EXPECT(std::fabs(out.glyphs[0].sourceU0 - 0.0f) < 0.001f);
    EXPECT(std::fabs(out.glyphs[0].sourceU1 - 0.125f) < 0.001f);
    EXPECT(std::fabs(out.glyphs[0].sourceV1 - 0.125f) < 0.001f);
    EXPECT(out.glyphs[0].texture == &atlas);
}

void SpacesNewlinesAndMissingGlyphsMatchEngineAdvances() {
    auto in = input("A ?B\nB");
    GlyphLayoutFrame out{};
    EXPECT(layoutAuthoredText(font(), in, out) == TextLayoutStatus::Ok);
    EXPECT(out.glyphCount == 3);
    EXPECT(out.glyphs[1].destinationX == 9.0f); // A advance + one space; missing '?' advances zero.
    EXPECT(out.glyphs[2].destinationY == 12.0f);
}

void WrappingHardWrapsWordsAndAlignmentUsesVisibleWidths() {
    auto in = input("AA");
    in.boxWidth = 10.0f;
    in.wrap = true;
    GlyphLayoutFrame out{};
    EXPECT(layoutAuthoredText(font(), in, out) == TextLayoutStatus::Ok);
    EXPECT(out.glyphCount == 2);
    EXPECT(out.glyphs[1].destinationY == 14.0f); // line height plus authored glyph offset.

    in = input("AA");
    in.boxWidth = 30.0f;
    in.alignment = TextHorizontalAlignment::Center;
    EXPECT(layoutAuthoredText(font(), in, out) == TextLayoutStatus::Ok);
    EXPECT(out.glyphs[0].destinationX == 4.0f);
    in.boxWidth = 30.0f;
    in.alignment = TextHorizontalAlignment::Right;
    EXPECT(layoutAuthoredText(font(), in, out) == TextLayoutStatus::Ok);
    EXPECT(out.glyphs[0].destinationX == 8.0f);

    in = input("A A");
    in.boxWidth = 12.0f;
    in.wrap = true;
    EXPECT(layoutAuthoredText(font(), in, out) == TextLayoutStatus::Ok);
    EXPECT(out.glyphCount == 2);
    EXPECT(out.glyphs[1].destinationY == 14.0f);
}

void InvalidFontUvMetricsAndScaleRejectWithoutPartialOutput() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    GlyphLayoutFrame out{};
    auto bad = font(); bad.atlasWidth = 0;
    EXPECT(layoutAuthoredText(bad, input("A"), out) == TextLayoutStatus::InvalidFont);
    AuthoredFontGlyph badGlyphs[] = {glyphs[0], glyphs[1], glyphs[2]};
    badGlyphs[0].sourceU = 0.8f;
    bad = font(); bad.glyphs = badGlyphs;
    EXPECT(layoutAuthoredText(bad, input("A"), out) == TextLayoutStatus::InvalidFont);
    badGlyphs[0] = glyph(U'A', 0, 0, 0.25f, 0.5f, nan, 6.0f);
    EXPECT(layoutAuthoredText(bad, input("A"), out) == TextLayoutStatus::InvalidFont);
    auto in = input("A"); in.fontScale = 0.0f;
    EXPECT(layoutAuthoredText(font(), in, out) == TextLayoutStatus::InvalidScale);
    EXPECT(out.glyphCount == 0);
}

void ExceedingGlyphCapacityFailsAndClearsOutput() {
    char text[kMaxGlyphs + 2]{};
    for (std::uint16_t i = 0; i < kMaxGlyphs + 1; ++i) text[i] = 'A';
    GlyphLayoutFrame out{};
    EXPECT(layoutAuthoredText(font(), input(text), out) == TextLayoutStatus::TooManyGlyphs);
    EXPECT(out.glyphCount == 0);
}

/// Keeps nested clipping and effect displacement aligned with the glyph's atlas region.
void AncestorIntersectionCropsGlyphUvsAndEffectPasses() {
    GlyphLayoutFrame out{};
    EXPECT(layoutAuthoredText(font(), input("A"), out) == TextLayoutStatus::Ok);
    DreamcastGlyphClip clip;
    clip.include(0.0f, 0.0f, 12.0f, 30.0f);
    clip.include(4.0f, 6.0f, 20.0f, 8.0f);
    AuthoredGlyphState visible = out.glyphs[0];
    EXPECT(clip.clip(visible));
    EXPECT(visible.destinationX == 4.0f);
    EXPECT(visible.destinationY == 6.0f);
    EXPECT(visible.destinationWidth == 8.0f);
    EXPECT(visible.destinationHeight == 8.0f);
    EXPECT(std::fabs(visible.sourceU0 - 0.03125f) < 0.0001f);
    EXPECT(std::fabs(visible.sourceU1 - 0.09375f) < 0.0001f);
    EXPECT(std::fabs(visible.sourceV0 - 0.03125f) < 0.0001f);
    EXPECT(std::fabs(visible.sourceV1 - 0.09375f) < 0.0001f);
    EXPECT(visible.texture == out.glyphs[0].texture);

    AuthoredGlyphState shadow = out.glyphs[0];
    shadow.destinationX += 2.0f;
    EXPECT(clip.clip(shadow));
    EXPECT(shadow.destinationX == 4.0f);
    EXPECT(std::fabs(shadow.sourceU0 - 0.015625f) < 0.0001f);
    EXPECT(std::fabs(shadow.sourceU1 - 0.078125f) < 0.0001f);

    AuthoredGlyphState outside = out.glyphs[0];
    outside.destinationX += 20.0f;
    EXPECT(!clip.clip(outside));
}

/// Preserves untouched glyphs and applies freshly resolved bounds after ancestor movement.
void LiveClipBoundsPreserveContainedGlyphsAndRejectEmptyIntersections() {
    GlyphLayoutFrame out{};
    EXPECT(layoutAuthoredText(font(), input("A"), out) == TextLayoutStatus::Ok);
    AuthoredGlyphState untouched = out.glyphs[0];
    DreamcastGlyphClip none;
    EXPECT(none.clip(untouched));
    DreamcastGlyphClip contained;
    contained.include(-1.0f, -1.0f, 40.0f, 40.0f);
    EXPECT(contained.clip(untouched));
    EXPECT(untouched.destinationX == out.glyphs[0].destinationX);
    EXPECT(untouched.destinationHeight == out.glyphs[0].destinationHeight);
    EXPECT(untouched.sourceU1 == out.glyphs[0].sourceU1);
    EXPECT(untouched.sourceV0 == out.glyphs[0].sourceV0);

    DreamcastGlyphClip moved;
    moved.include(20.0f, 0.0f, 40.0f, 40.0f);
    EXPECT(!moved.clip(untouched));
    DreamcastGlyphClip empty;
    empty.include(0.0f, 0.0f, 40.0f, 40.0f);
    empty.include(10.0f, 0.0f, 0.0f, 40.0f);
    EXPECT(!empty.clip(untouched));
}

/// Keeps the full authored body atlas and glyph size while scaling only to the output viewport.
void FullBodyAtlasPreservesAuthoredButtonSize() {
    const AuthoredFontGlyph body[] = {glyph(U'H', 0.0f, 0.0f, 19.0f / 512.0f, 25.0f / 256.0f, 8.0f, 20.9375f)};
    const AuthoredFontView bodyFont{512, 256, 512, 37.15625f, 9.640625f, body, 1, &atlas};
    auto in = input("H");
    in.fontScale = 2.0f * 480.0f / 720.0f;
    GlyphLayoutFrame out{};
    EXPECT(layoutAuthoredText(bodyFont, in, out) == TextLayoutStatus::Ok);
    EXPECT(out.glyphCount == 1);
    EXPECT(std::fabs(out.glyphs[0].destinationHeight - 33.333333f) < 0.001f);
    EXPECT(std::fabs(out.glyphs[0].destinationWidth - 25.333333f) < 0.001f);
    EXPECT(std::fabs(out.glyphs[0].sourceU1 - 19.0f / 512.0f) < 0.00001f);
    EXPECT(std::fabs(out.glyphs[0].sourceV1 - 25.0f / 512.0f) < 0.00001f);
}
}

int main() {
    GlyphGeometryOffsetAndPaddedAtlasUvArePreserved();
    SpacesNewlinesAndMissingGlyphsMatchEngineAdvances();
    WrappingHardWrapsWordsAndAlignmentUsesVisibleWidths();
    InvalidFontUvMetricsAndScaleRejectWithoutPartialOutput();
    ExceedingGlyphCapacityFailsAndClearsOutput();
    AncestorIntersectionCropsGlyphUvsAndEffectPasses();
    LiveClipBoundsPreserveContainedGlyphsAndRejectEmptyIntersections();
    FullBodyAtlasPreservesAuthoredButtonSize();
    if (failures) { std::cerr << failures << " assertion(s) failed\n"; return 1; }
    std::cout << "DreamcastTextLayoutTests: all tests passed\n";
    return 0;
}
