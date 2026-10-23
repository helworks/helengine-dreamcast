using System.Buffers.Binary;
using helengine.baseplatform.Manifest;
using helengine.dreamcast.builder;

namespace helengine.dreamcast.builder.tests;

public sealed class DreamcastFontAtlasMetricsScalerTests {
    [Fact]
    public void RescaleMetrics_UsesIndependentCookedAtlasWidthAndHeightRatios() {
        var font = new helengine.FontAsset(
            new helengine.FontInfo("fixture", 18, 8f),
            null,
            new Dictionary<char, helengine.FontChar> {
                ['A'] = new(new helengine.float4(0f, 0f, 0.25f, 0.5f), 6f, 12f, 4f, 8f)
            },
            20f,
            512,
            256);
        try {
            DreamcastFontAtlasMetricsScaler.RescaleMetrics(font, 256, 64);

            Assert.Equal(256, font.AtlasWidth);
            Assert.Equal(64, font.AtlasHeight);
            Assert.Equal(5f, font.LineHeight);
            Assert.Equal(4, font.FontInfo.LineSpacing);
            Assert.Equal(4f, font.FontInfo.SpaceWidth);
            helengine.FontChar glyph = font.Characters['A'];
            Assert.Equal(1.5f, glyph.OffsetY);
            Assert.Equal(6f, glyph.AdvanceWidth);
            Assert.Equal(2f, glyph.BearingX);
            Assert.Equal(2f, glyph.BearingY);
            Assert.Null(font.SourceTextureAsset);
        } finally {
            font.Dispose();
        }
    }

    /// <summary>Preserves font metrics for legacy and current palette, mipmapped and VQ texture payloads.</summary>
    [Theory]
    [InlineData("Legacy")]
    [InlineData("DcPal4")]
    [InlineData("DcPal8_Mipmapped")]
    [InlineData("DcArgb4444_Vq")]
    public void RescalePackagedFonts_UsesCookedNonSquareExtentAndUpdatesSerializedMetrics(string formatId) {
        string fixtureRoot = Path.Combine(Path.GetTempPath(), "font-metrics-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(Path.Combine(fixtureRoot, "cooked"));
        string fontPath = Path.Combine(fixtureRoot, "cooked", "sample.hefont");
        string atlasPath = Path.Combine(fixtureRoot, "cooked", "sample.dctex");
        var font = new helengine.FontAsset(
            new helengine.FontInfo("fixture", 18, 8f), null,
            new Dictionary<char, helengine.FontChar> {
                ['A'] = new(new helengine.float4(0f, 0f, 0.25f, 0.5f), 6f, 12f, 4f, 8f)
            }, 20f, 512, 256) {
            CookedAtlasTextureRelativePath = "cooked/sample.dctex"
        };
        try {
            using (var stream = File.Create(fontPath))
                helengine.files.FontAssetBinarySerializer.Serialize(stream, font);
            WriteCookedExtent(atlasPath, 256, 64, formatId);
            var workItem = new PlatformCookWorkItem(
                "font-atlas", "source.hasset", "font-atlas-texture", "dc", "runtime-texture",
                "cooked/sample.dctex", "font-atlas", "source-hash", "settings-hash", "{}", []);

            DreamcastFontAtlasMetricsScaler.RescalePackagedFonts(fixtureRoot, [workItem]);

            using var resultStream = File.OpenRead(fontPath);
            helengine.FontAsset result = helengine.files.FontAssetBinarySerializer.Deserialize(resultStream);
            try {
                Assert.Equal(256, result.AtlasWidth);
                Assert.Equal(64, result.AtlasHeight);
                Assert.Equal(5f, result.LineHeight);
                Assert.Equal(4f, result.FontInfo.SpaceWidth);
                Assert.Equal(1.5f, result.Characters['A'].OffsetY);
                Assert.Equal(6f, result.Characters['A'].AdvanceWidth);
                Assert.Equal("cooked/sample.dctex", result.CookedAtlasTextureRelativePath);
            } finally {
                result.Dispose();
            }
        } finally {
            font.Dispose();
            Directory.Delete(fixtureRoot, recursive: true);
        }
    }

    /// <summary>Creates real current native textures or the historical uncompressed atlas fixture.</summary>
    static void WriteCookedExtent(string path, ushort width, ushort height, string formatId) {
        if (formatId != "Legacy") {
            using var texture = new helengine.TextureAsset {
                Width = width, Height = height,
                ColorFormat = helengine.TextureAssetColorFormat.Rgba32,
                AlphaPrecision = helengine.TextureAssetAlphaPrecision.A4,
                Colors = new byte[width * height * 4], PaletteColors = []
            };
            var settings = new helengine.editor.TextureAssetProcessorSettings {
                ColorFormatId = formatId, AlphaPrecision = helengine.TextureAssetAlphaPrecision.A4
            };
            File.WriteAllBytes(path, new DreamcastNativeTextureCooker().Cook(texture, settings));
            return;
        }
        const ushort side = 256;
        byte[] bytes = new byte[DreamcastTextureSchemaIds.HeaderBytes + side * side * 2];
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(0, 4), DreamcastTextureSchemaIds.Magic);
        BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(4, 2), DreamcastTextureSchemaIds.Version);
        BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(8, 2), side);
        BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(10, 2), width);
        BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(12, 2), height);
        File.WriteAllBytes(path, bytes);
    }

}
