using helengine;
using helengine.dreamcast.builder;

namespace helengine.dreamcast.builder.tests;

/// <summary>Exercises real source decoding without a host-specific image fixture writer.</summary>
public sealed class DreamcastSourceTextureDecoderTests {
    const string RgbaPng = "iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAYAAAD0In+KAAAADklEQVR4nGP4z8AAQg0AD3oDfnfpf5cAAAAASUVORK5CYII=";

    /// <summary>Preserves straight RGB channels and opaque/partial alpha on every build host.</summary>
    [Fact]
    public void AuthoredPngDecodesToOwnedStraightRgbaWithA8() {
        string root = Path.Combine(AppContext.BaseDirectory, "source-decoder-png");
        Directory.CreateDirectory(root);
        string path = Path.Combine(root, "source.png");
        byte[] png = Convert.FromBase64String(RgbaPng);
        try {
            File.WriteAllBytes(path, png);
            using TextureAsset decoded = new DreamcastSourceTextureDecoder().Decode(path);
            Assert.Equal(2, decoded.Width);
            Assert.Equal(1, decoded.Height);
            Assert.Equal(TextureAssetColorFormat.Rgba32, decoded.ColorFormat);
            Assert.Equal(TextureAssetAlphaPrecision.A8, decoded.AlphaPrecision);
            Assert.Equal(new byte[] { 255, 0, 0, 255, 0, 0, 255, 128 }, decoded.Colors);
            Assert.Empty(decoded.PaletteColors);
            decoded.Colors[0] = 0;
            using TextureAsset second = new DreamcastSourceTextureDecoder().Decode(path);
            Assert.Equal(255, second.Colors[0]);
            Assert.Equal(png, File.ReadAllBytes(path));
        } finally {
            File.Delete(path);
            Directory.Delete(root);
        }
    }

    /// <summary>Retains the authoring serializer and alpha metadata for generated font-atlas sources.</summary>
    [Fact]
    public void GeneratedAtlasPayloadKeepsItsSourceAlphaPrecision() {
        string root = Path.Combine(AppContext.BaseDirectory, "source-decoder-atlas");
        Directory.CreateDirectory(root);
        string path = Path.Combine(root, "atlas.generated");
        using TextureAsset source = new() {
            Width = 1, Height = 1, ColorFormat = TextureAssetColorFormat.Rgba32,
            AlphaPrecision = TextureAssetAlphaPrecision.A4,
            Colors = [17, 34, 51, 136], PaletteColors = []
        };
        byte[] payload = helengine.files.AssetSerializer.SerializeToBytes(source);
        try {
            File.WriteAllBytes(path, payload);
            using TextureAsset decoded = new DreamcastSourceTextureDecoder().Decode(path);
            Assert.Equal(1, decoded.Width);
            Assert.Equal(1, decoded.Height);
            Assert.Equal(TextureAssetColorFormat.Rgba32, decoded.ColorFormat);
            Assert.Equal(TextureAssetAlphaPrecision.A4, decoded.AlphaPrecision);
            Assert.Equal(source.Colors, decoded.Colors);
            decoded.Colors[0] = 255;
            Assert.Equal(17, source.Colors[0]);
            Assert.Equal(payload, File.ReadAllBytes(path));
        } finally {
            File.Delete(path);
            Directory.Delete(root);
        }
    }

    /// <summary>Preserves native PVR/DPAL dispatch instead of sending it through the portable PNG decoder.</summary>
    [Fact]
    public void NativePvrStillUsesItsCompanionPalette() {
        string root = Path.Combine(AppContext.BaseDirectory, "source-decoder-pvr");
        Directory.CreateDirectory(root);
        string path = Path.Combine(root, "source.pvr");
        string palettePath = Path.ChangeExtension(path, ".pal");
        byte[] pvr = new byte[48];
        BitConverter.TryWriteBytes(pvr.AsSpan(0), 0x54525650u);
        BitConverter.TryWriteBytes(pvr.AsSpan(4), 40u);
        pvr[9] = 5;
        BitConverter.TryWriteBytes(pvr.AsSpan(12), (ushort)8);
        BitConverter.TryWriteBytes(pvr.AsSpan(14), (ushort)8);
        try {
            File.WriteAllBytes(path, pvr);
            File.WriteAllBytes(palettePath, [(byte)'D', (byte)'P', (byte)'A', (byte)'L', 1, 0, 0, 0, 0, 0, 255, 255]);
            using TextureAsset decoded = new DreamcastSourceTextureDecoder().Decode(path);
            Assert.Equal(8, decoded.Width);
            Assert.Equal(8, decoded.Height);
            Assert.Equal(TextureAssetColorFormat.Rgba32, decoded.ColorFormat);
            Assert.Equal(new byte[] { 255, 0, 0, 255 }, decoded.Colors[..4]);
            Assert.Equal(pvr, File.ReadAllBytes(path));
        } finally {
            File.Delete(path);
            File.Delete(palettePath);
            Directory.Delete(root);
        }
    }
}
