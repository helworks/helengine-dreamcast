using System.Buffers.Binary;
using helengine;
using helengine.editor;
using helengine.dreamcast.builder;

namespace helengine.dreamcast.builder.tests;

/// <summary>Checks native bit layouts, imported Sega fixtures, hardware mip offsets and every selectable cooker format.</summary>
public sealed class DreamcastNativeTextureFormatTests {
    /// <summary>Supplies every published native format and a representable alpha precision.</summary>
    public static IEnumerable<object[]> Formats() {
        foreach (DreamcastTextureFormat format in DreamcastTextureFormatCatalog.Formats) yield return [format.Id, format.GetAlphaPrecisions().Last()];
    }

    /// <summary>Checks every format through settings serialization and cooking with independently known sample colors.</summary>
    [Theory]
    [MemberData(nameof(Formats))]
    public void Cook_AllNativeFormatsProduceValidSizesAndBasePixels(string id, TextureAssetAlphaPrecision alpha) {
        using TextureAsset source = SolidTexture(8, 255, 0, 0, 255);
        if (id.StartsWith("DcBump", StringComparison.Ordinal)) Array.Fill(source.Colors, (byte)128);
        TextureAssetProcessorSettings settings = DreamcastTextureCookSettingsSerializer.Deserialize(DreamcastTextureCookSettingsSerializer.Serialize(new TextureAssetProcessorSettings { ColorFormatId = id, AlphaPrecision = alpha, MaxResolution = 512 }));
        DreamcastTextureFormat format = DreamcastTextureFormatCatalog.Resolve(id);
        byte[] cooked = new DreamcastTextureCooker().Cook(source, settings);
        Assert.Equal((ushort)2, BitConverter.ToUInt16(cooked, 4));
        Assert.Equal(format.Flags, BitConverter.ToUInt16(cooked, 6));
        Assert.Equal(format.PaletteEntries, BitConverter.ToUInt16(cooked, 14));
        Assert.Equal(0, format.GetPixelBytes(8) % 32);
        Assert.Equal(16 + format.GetPixelBytes(8) + format.PaletteEntries * 4, cooked.Length);
        Assert.True(TextureVramUsageCalculator.TryCalculateBytes(8, 8, settings, out long memory));
        Assert.Equal(cooked.Length - 16, memory);
        int storage = format.Linear ? 9 : format.Vq ? (format.Mipmaps ? 4 : 3) : format.PixelFormat == 5 ? (format.Mipmaps ? 6 : 5) : format.PixelFormat == 6 ? (format.Mipmaps ? 8 : 7) : format.Mipmaps ? 2 : 1;
        int length = DreamcastPvrPixelCodec.PayloadBytes(8, 8, format.PixelFormat, format.Mipmaps, format.Vq);
        byte[] payload = cooked.AsSpan(16, length).ToArray();
        byte[] palette = format.PaletteEntries == 0 ? null : cooked.AsSpan(16 + format.GetPixelBytes(8)).ToArray();
        using MemoryStream pvr = new(Pvr(format.PixelFormat, storage, 8, 8, payload));
        using TextureAsset imported = new DreamcastPvrTextureImporter().ImportTexture(pvr, palette);
        Assert.Equal((ushort)8, imported.Width);
        if (format.PixelFormat != 3 && format.PixelFormat != 4) {
            Assert.Equal(source.Colors, imported.Colors);
        } else if (format.PixelFormat == 3) {
            Assert.InRange(imported.Colors[0], (byte)240, (byte)255);
            Assert.InRange(imported.Colors[1], (byte)0, (byte)12);
        }
    }

    /// <summary>Uses hand-written words to detect wrong channel order and wrong Morton-axis ordering.</summary>
    [Fact]
    public void Import_GoldenArgb1555UsesNativeChannelsAndTwiddle() {
        byte[] payload = [0x00, 0xFC, 0x1F, 0x80, 0xE0, 0x83, 0xFF, 0xFF];
        using MemoryStream stream = new(Pvr(0, 1, 2, 2, payload));
        using TextureAsset texture = new DreamcastPvrTextureImporter().ImportTexture(stream);
        Assert.Equal(new byte[] { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 }, texture.Colors);
    }

    /// <summary>Checks that index depth comes from Sega storage codes and PAL4 uses its low nibble first.</summary>
    [Theory]
    [InlineData(5, new byte[] { 0x10, 0x32 })]
    [InlineData(7, new byte[] { 0, 1, 2, 3 })]
    public void Import_GoldenIndexedStorageUsesExternalPalette(int storage, byte[] payload) {
        byte[] palette = [255, 0, 0, 255, 0, 0, 255, 255, 0, 255, 0, 255, 255, 255, 255, 255];
        using MemoryStream stream = new(Pvr(0, storage, 2, 2, payload));
        using TextureAsset texture = new DreamcastPvrTextureImporter().ImportTexture(stream, palette);
        Assert.Equal(new byte[] { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255 }, texture.Colors);
    }

    /// <summary>Verifies the fixed PVR hardware mip offsets independently of the encoder's size function.</summary>
    [Theory]
    [InlineData(1, 6)] [InlineData(2, 8)] [InlineData(4, 16)] [InlineData(8, 48)] [InlineData(16, 176)] [InlineData(32, 688)] [InlineData(512, 174768)]
    public void MipOffsetsMatchHardware(int side, int bytes) {
        Assert.Equal(bytes, DreamcastPvrPixelCodec.MipOffset(side, 2, false));
        Assert.Equal(bytes / 8, DreamcastPvrPixelCodec.MipOffset(side, 2, true));
    }

    /// <summary>Rejects truncated chunks, missing palettes, impossible VQ formats and unsupported storage types.</summary>
    [Fact]
    public void Import_RejectsInvalidPayloadsWithoutInventingPixels() {
        DreamcastPvrTextureImporter importer = new();
        Assert.Throws<InvalidDataException>(() => importer.ImportTexture(new MemoryStream(Pvr(0, 5, 2, 2, [0, 0]))));
        byte[] truncated = Pvr(0, 1, 2, 2, [0, 0]);
        Assert.Throws<InvalidDataException>(() => importer.ImportTexture(new MemoryStream(truncated)));
        Assert.Throws<InvalidDataException>(() => importer.ImportTexture(new MemoryStream(Pvr(0, 99, 2, 2, new byte[8]))));
    }

    /// <summary>Checks transparent and opaque pixels against the native alpha bit and the normal-map spherical pole.</summary>
    [Fact]
    public void NativeWordsUseHardwarePacking() {
        Assert.Equal((ushort)0xFC00, DreamcastPvrPixelCodec.EncodeWord([255, 0, 0, 255], 0, 0));
        Assert.Equal((ushort)0x7C00, DreamcastPvrPixelCodec.EncodeWord([255, 0, 0, 0], 0, 0));
        Assert.Equal((ushort)0xF800, DreamcastPvrPixelCodec.EncodeWord([255, 0, 0, 255], 0, 1));
        Assert.Equal((ushort)0x4F81, DreamcastPvrPixelCodec.EncodeWord([240, 128, 16, 64], 0, 2));
        Assert.Equal((ushort)0xFF00, DreamcastPvrPixelCodec.EncodeWord([128, 128, 255, 255], 0, 4));
    }

    /// <summary>Decodes a reduced codebook whose hand-authored vectors differ in both axes and rejects an out-of-range index.</summary>
    [Fact]
    public void Import_SmallVqPreservesBlockAndTexelOrdering() {
        byte[] payload = new byte[128 + 16];
        // Vector five: top-left red, bottom-left blue, top-right green, bottom-right white.
        new byte[] { 0, 0xFC, 0x1F, 0x80, 0xE0, 0x83, 0xFF, 0xFF }.CopyTo(payload, 5 * 8);
        Array.Fill(payload, (byte)5, 128, 16);
        using TextureAsset texture = new DreamcastPvrTextureImporter().ImportTexture(new MemoryStream(Pvr(0, 16, 8, 8, payload)));
        Assert.Equal(new byte[] { 255, 0, 0, 255, 0, 255, 0, 255 }, texture.Colors.Take(8));
        Assert.Equal(new byte[] { 0, 0, 255, 255 }, texture.Colors.Skip(8 * 4).Take(4));
        payload[128] = 16;
        Assert.Throws<InvalidDataException>(() => new DreamcastPvrTextureImporter().ImportTexture(new MemoryStream(Pvr(0, 16, 8, 8, payload))));
    }

    /// <summary>Checks rectangular twiddling as separate square blocks rather than incorrectly interleaving both full dimensions.</summary>
    [Fact]
    public void Import_RectangleTwiddledUsesSquareBlocks() {
        byte[] payload = [0, 0xF1, 0, 0xF2, 0, 0xF3, 0, 0xF4, 0, 0xF5, 0, 0xF6, 0, 0xF7, 0, 0xF8];
        using TextureAsset texture = new DreamcastPvrTextureImporter().ImportTexture(new MemoryStream(Pvr(2, 13, 4, 2, payload)));
        Assert.Equal(new byte[] { 17, 51, 85, 119, 34, 68, 102, 136 }, texture.Colors.Where((value, index) => index % 4 == 0));
    }

    /// <summary>Verifies all direct-color mip levels and unused prefix bytes against independently known hardware positions.</summary>
    [Fact]
    public void Cook_MipmapsIncludeTheOnePixelLevelAndHardwarePadding() {
        using TextureAsset source = SolidTexture(8, 255, 0, 0, 255);
        byte[] cooked = new DreamcastTextureCooker().Cook(source, new TextureAssetProcessorSettings { ColorFormatId = "DcRgb565_Mipmapped", AlphaPrecision = TextureAssetAlphaPrecision.Opaque });
        Assert.All(cooked.Skip(16).Take(6), value => Assert.Equal((byte)0, value));
        foreach (int offset in new[] { 6, 8, 16, 48 }) Assert.Equal((ushort)0xF800, BitConverter.ToUInt16(cooked, 16 + offset));
        Assert.All(cooked.Skip(16 + 176), value => Assert.Equal((byte)0, value));
    }

    /// <summary>Exercises the lossy VQ path with more than 256 distinct blocks and confirms stable bounded codebooks.</summary>
    [Fact]
    public void Cook_VqQuantizationIsDeterministicForComplexImages() {
        using TextureAsset source = SolidTexture(64, 0, 0, 0, 255);
        Random random = new(1234);
        for (int offset = 0; offset < source.Colors.Length; offset += 4) {
            source.Colors[offset] = (byte)random.Next(256); source.Colors[offset + 1] = (byte)random.Next(256); source.Colors[offset + 2] = (byte)random.Next(256);
        }
        TextureAssetProcessorSettings settings = new() { ColorFormatId = "DcRgb565_Vq", AlphaPrecision = TextureAssetAlphaPrecision.Opaque };
        DreamcastTextureCooker cooker = new();
        byte[] first = cooker.Cook(source, settings);
        Assert.Equal(first, cooker.Cook(source, settings));
        Assert.Equal(16 + 2048 + 1024, first.Length);
        using TextureAsset imported = new DreamcastPvrTextureImporter().ImportTexture(new MemoryStream(Pvr(1, 3, 64, 64, first.Skip(16).ToArray())));
        Assert.Equal(source.Colors.Length, imported.Colors.Length);
        Assert.All(imported.Colors.Where((value, index) => index % 4 == 3), value => Assert.Equal((byte)255, value));
    }

    /// <summary>Creates a small RGBA source whose uniform color makes lossy packing expectations unambiguous.</summary>
    static TextureAsset SolidTexture(int side, byte red, byte green, byte blue, byte alpha) {
        byte[] rgba = new byte[side * side * 4];
        for (int offset = 0; offset < rgba.Length; offset += 4) { rgba[offset] = red; rgba[offset + 1] = green; rgba[offset + 2] = blue; rgba[offset + 3] = alpha; }
        return new TextureAsset { Width = (ushort)side, Height = (ushort)side, ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = TextureAssetAlphaPrecision.A8, Colors = rgba, PaletteColors = new byte[0] };
    }

    /// <summary>Builds a standard Sega PVRT chunk with a size excluding FourCC and length.</summary>
    static byte[] Pvr(int format, int storage, int width, int height, byte[] payload) {
        byte[] bytes = new byte[16 + payload.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(bytes, 0x54525650);
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(4), (uint)(bytes.Length - 8));
        bytes[8] = (byte)format; bytes[9] = (byte)storage;
        BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(12), (ushort)width);
        BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(14), (ushort)height);
        payload.CopyTo(bytes, 16);
        return bytes;
    }
}
