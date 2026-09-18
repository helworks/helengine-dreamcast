using helengine.dreamcast.builder;

namespace helengine.dreamcast.builder.tests;

/// <summary>Verifies conversion, square padding and rejection of unsupported texture input.</summary>
public sealed class DreamcastTextureCookerTests {
    /// <summary>Confirms the header records the padded side and the original used extent.</summary>
    [Fact]
    public void Cook_PadsToSquarePowerOfTwoAndRecordsTheUsedExtent() {
        byte[] cooked = new DreamcastTextureCooker().Cook(CreateRgba32(3, 2));

        Assert.Equal((uint)DreamcastTextureSchemaIds.Magic, BitConverter.ToUInt32(cooked, 0));
        Assert.Equal((ushort)1, BitConverter.ToUInt16(cooked, 4));
        Assert.Equal((ushort)0, BitConverter.ToUInt16(cooked, 6));
        Assert.Equal((ushort)4, BitConverter.ToUInt16(cooked, 8));
        Assert.Equal((ushort)3, BitConverter.ToUInt16(cooked, 10));
        Assert.Equal((ushort)2, BitConverter.ToUInt16(cooked, 12));
        Assert.Equal(DreamcastTextureSchemaIds.HeaderBytes + (4 * 4 * 2), cooked.Length);
    }

    /// <summary>Confirms an opaque white source pixel becomes ARGB4444 0xFFFF at its twiddled position.</summary>
    [Fact]
    public void Cook_ConvertsOpaqueWhiteToArgb4444() {
        helengine.TextureAsset asset = CreateRgba32(1, 1);
        asset.Colors = [0xFF, 0xFF, 0xFF, 0xFF];

        byte[] cooked = new DreamcastTextureCooker().Cook(asset);

        int offset = DreamcastTextureSchemaIds.HeaderBytes + (DreamcastTextureTwiddle.TwiddledIndex(0, 0) * 2);
        Assert.Equal((ushort)0xFFFF, BitConverter.ToUInt16(cooked, offset));
    }

    /// <summary>Confirms padding is transparent black so no colour can bleed at the sampled edge.</summary>
    [Fact]
    public void Cook_FillsPaddingWithTransparentBlack() {
        // 3x2 pads to a 4x4 square, so (3,3) is padding while (2,1) is real.
        // Every source pixel is opaque white, so a zero can only come from padding.
        helengine.TextureAsset asset = CreateRgba32(3, 2);
        Array.Fill(asset.Colors, (byte)0xFF);

        byte[] cooked = new DreamcastTextureCooker().Cook(asset);

        int padding = DreamcastTextureSchemaIds.HeaderBytes + (DreamcastTextureTwiddle.TwiddledIndex(3, 3) * 2);
        int real = DreamcastTextureSchemaIds.HeaderBytes + (DreamcastTextureTwiddle.TwiddledIndex(2, 1) * 2);
        Assert.Equal((ushort)0x0000, BitConverter.ToUInt16(cooked, padding));
        Assert.Equal((ushort)0xFFFF, BitConverter.ToUInt16(cooked, real));
    }

    /// <summary>Confirms a source larger than the PVR limit is rejected by name.</summary>
    [Fact]
    public void Cook_RejectsASourceAboveTheHardwareLimit() {
        helengine.TextureAsset asset = CreateRgba32(1025, 4);
        asset.Id = "textures/huge";

        InvalidOperationException exception =
            Assert.Throws<InvalidOperationException>(() => new DreamcastTextureCooker().Cook(asset));

        Assert.Contains("textures/huge", exception.Message, StringComparison.Ordinal);
        Assert.Contains("1024", exception.Message, StringComparison.Ordinal);
    }

    /// <summary>Confirms a zero-sized source is rejected rather than producing an empty texture.</summary>
    [Theory]
    [InlineData(0, 4)]
    [InlineData(4, 0)]
    public void Cook_RejectsAZeroSizedSource(int width, int height) {
        helengine.TextureAsset asset = CreateRgba32(width, height);

        Assert.Throws<InvalidOperationException>(() => new DreamcastTextureCooker().Cook(asset));
    }

    /// <summary>Confirms colour formats this platform cannot consume are rejected by name.</summary>
    [Theory]
    [InlineData(helengine.TextureAssetColorFormat.Indexed4)]
    [InlineData(helengine.TextureAssetColorFormat.Indexed8)]
    [InlineData(helengine.TextureAssetColorFormat.GxRgb5A3)]
    public void Cook_RejectsUnsupportedColourFormats(helengine.TextureAssetColorFormat format) {
        helengine.TextureAsset asset = CreateRgba32(2, 2);
        asset.ColorFormat = format;

        InvalidOperationException exception =
            Assert.Throws<InvalidOperationException>(() => new DreamcastTextureCooker().Cook(asset));

        Assert.Contains(format.ToString(), exception.Message, StringComparison.Ordinal);
    }

    static helengine.TextureAsset CreateRgba32(int width, int height) {
        return new helengine.TextureAsset {
            Id = "textures/test",
            Width = (ushort)width,
            Height = (ushort)height,
            ColorFormat = helengine.TextureAssetColorFormat.Rgba32,
            AlphaPrecision = helengine.TextureAssetAlphaPrecision.A8,
            Colors = new byte[Math.Max(0, width * height * 4)]
        };
    }
}
