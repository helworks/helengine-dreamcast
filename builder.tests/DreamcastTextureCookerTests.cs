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
        Assert.Equal((ushort)8, BitConverter.ToUInt16(cooked, 8));
        Assert.Equal((ushort)3, BitConverter.ToUInt16(cooked, 10));
        Assert.Equal((ushort)2, BitConverter.ToUInt16(cooked, 12));
        Assert.Equal(DreamcastTextureSchemaIds.HeaderBytes + (8 * 8 * 2), cooked.Length);
    }

    /// <summary>
    /// Confirms art below the samplable minimum is padded up rather than rejected,
    /// and that the header still records its real used extent.
    /// </summary>
    [Fact]
    public void Cook_PadsASourceBelowTheMinimumSideUpToTheMinimum() {
        byte[] cooked = new DreamcastTextureCooker().Cook(CreateRgba32(1, 1));

        Assert.Equal((ushort)DreamcastTextureSchemaIds.MinimumSide, BitConverter.ToUInt16(cooked, 8));
        Assert.Equal((ushort)1, BitConverter.ToUInt16(cooked, 10));
        Assert.Equal((ushort)1, BitConverter.ToUInt16(cooked, 12));
        Assert.Equal(
            DreamcastTextureSchemaIds.HeaderBytes +
                (DreamcastTextureSchemaIds.MinimumSide * DreamcastTextureSchemaIds.MinimumSide * 2),
            cooked.Length);
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
        // 3x2 pads to an 8x8 square, so (3,3) is padding while (2,1) is real.
        // Every source pixel is opaque white, so a zero can only come from padding.
        helengine.TextureAsset asset = CreateRgba32(3, 2);
        Array.Fill(asset.Colors, (byte)0xFF);

        byte[] cooked = new DreamcastTextureCooker().Cook(asset);

        int padding = DreamcastTextureSchemaIds.HeaderBytes + (DreamcastTextureTwiddle.TwiddledIndex(3, 3) * 2);
        int real = DreamcastTextureSchemaIds.HeaderBytes + (DreamcastTextureTwiddle.TwiddledIndex(2, 1) * 2);
        Assert.Equal((ushort)0x0000, BitConverter.ToUInt16(cooked, padding));
        Assert.Equal((ushort)0xFFFF, BitConverter.ToUInt16(cooked, real));
    }

    /// <summary>
    /// Confirms a source that would pad above the platform limit is rejected by name,
    /// both just over the threshold and far above it.
    /// </summary>
    /// <remarks>
    /// 513 is the first width that pads to 1024, which is 2,097,168 cooked bytes and
    /// cannot be read back through the one MiB content stream cap. Rejecting it here
    /// is what stops a texture that builds clean and always fails to load.
    /// </remarks>
    [Theory]
    [InlineData(513, 4)]
    [InlineData(1025, 4)]
    public void Cook_RejectsASourceAboveThePlatformLimit(int width, int height) {
        helengine.TextureAsset asset = CreateRgba32(width, height);
        asset.Id = "textures/huge";

        InvalidOperationException exception =
            Assert.Throws<InvalidOperationException>(() => new DreamcastTextureCooker().Cook(asset));

        Assert.Contains("textures/huge", exception.Message, StringComparison.Ordinal);
        Assert.Contains("512", exception.Message, StringComparison.Ordinal);
    }

    /// <summary>
    /// Confirms the largest accepted source cooks to a payload that fits the one MiB
    /// content stream read cap, so the platform limit is not off by one power of two.
    /// </summary>
    [Fact]
    public void Cook_AcceptsTheLargestSourceThatStillFitsTheContentReadCap() {
        byte[] cooked = new DreamcastTextureCooker().Cook(CreateRgba32(512, 512));

        Assert.Equal((ushort)DreamcastTextureSchemaIds.MaximumSide, BitConverter.ToUInt16(cooked, 8));
        Assert.Equal(DreamcastTextureSchemaIds.HeaderBytes + (512 * 512 * 2), cooked.Length);
        Assert.True(cooked.Length <= 1024 * 1024);
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

    /// <summary>Confirms non-uniform Rgba32 pixels convert correctly with distinct channel values to detect order bugs.</summary>
    [Fact]
    public void Cook_ConvertsNonUniformRgba32ToArgb4444() {
        helengine.TextureAsset asset = CreateRgba32(1, 1);
        asset.Colors = [0xF0, 0x80, 0x10, 0x40];  // R=0xF0, G=0x80, B=0x10, A=0x40

        byte[] cooked = new DreamcastTextureCooker().Cook(asset);

        int offset = DreamcastTextureSchemaIds.HeaderBytes + (DreamcastTextureTwiddle.TwiddledIndex(0, 0) * 2);
        Assert.Equal((ushort)0x4F81, BitConverter.ToUInt16(cooked, offset));
    }

    /// <summary>Confirms Rgba4444 sources undergo correct alpha rotation to ARGB4444.</summary>
    [Fact]
    public void Cook_ConvertsRgba4444WithAlphaRotation() {
        helengine.TextureAsset asset = new helengine.TextureAsset {
            Id = "textures/test",
            Width = 1,
            Height = 1,
            ColorFormat = helengine.TextureAssetColorFormat.Rgba4444,
            AlphaPrecision = helengine.TextureAssetAlphaPrecision.A4,
            Colors = new byte[2]
        };
        // Source RGBA4444 with distinct nibbles: R=1, G=2, B=3, A=4
        // Encoded as: [R][G][B][A] = 0x1234
        BitConverter.TryWriteBytes(asset.Colors.AsSpan(0), (ushort)0x1234);

        byte[] cooked = new DreamcastTextureCooker().Cook(asset);

        int offset = DreamcastTextureSchemaIds.HeaderBytes + (DreamcastTextureTwiddle.TwiddledIndex(0, 0) * 2);
        // Alpha rotated from bits 3-0 to bits 15-12: result is ARGB4444 = 0x4123
        Assert.Equal((ushort)0x4123, BitConverter.ToUInt16(cooked, offset));
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
