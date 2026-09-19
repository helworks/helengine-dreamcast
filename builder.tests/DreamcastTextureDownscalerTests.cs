using helengine.dreamcast.builder;

namespace helengine.dreamcast.builder.tests;

/// <summary>
/// Verifies the Dreamcast-owned texture downscale: repeated exact halving with a two-by-two box average taken on
/// premultiplied alpha.
/// </summary>
public sealed class DreamcastTextureDownscalerTests {
    /// <summary>Art already within the cap is returned untouched, pixel for pixel.</summary>
    [Fact]
    public void Downscale_LeavesArtWithinTheCapUntouched() {
        using TextureAsset source = CreateTexture(8, 4, (x, y) => [(byte)(x * 8), (byte)(y * 8), 7, 255]);

        TextureAsset result = DreamcastTextureDownscaler.Downscale(source, 8);

        Assert.Same(source, result);
    }

    /// <summary>An uncapped setting of zero never resizes, whatever the source size.</summary>
    [Fact]
    public void Downscale_TreatsZeroAsUncapped() {
        using TextureAsset source = CreateTexture(64, 64, (_, _) => [1, 2, 3, 255]);

        Assert.Same(source, DreamcastTextureDownscaler.Downscale(source, 0));
    }

    /// <summary>
    /// A 256 square reaches 64 through two halvings, and every step averages a disjoint block rather than
    /// sampling one pixel: the source is a horizontal ramp whose value equals x, so the destination pixel at x
    /// must be the mean of the sixteen columns 16x through 16x+15, not the single column the shared engine
    /// processor's nearest-neighbour resize would have kept.
    /// </summary>
    [Fact]
    public void Downscale_AveragesEveryCoveredPixelRatherThanSamplingOne() {
        using TextureAsset source = CreateTexture(256, 256, (x, _) => [(byte)x, (byte)x, (byte)x, 255]);

        TextureAsset result = DreamcastTextureDownscaler.Downscale(source, 64);

        Assert.Equal((ushort)64, result.Width);
        Assert.Equal((ushort)64, result.Height);
        // Hand-computed, not taken from the implementation. The first halving turns columns (2n, 2n+1) into
        // 2n+1, so the 128-wide row reads 1, 3, 5, ... The second turns (4n+1, 4n+3) into 4n+2, so the 64-wide
        // row reads 2, 6, 10, ..., 254. Nearest-neighbour would have produced 0, 4, 8, ..., 252 instead.
        Assert.Equal(2, ReadPixel(result, 0, 0)[0]);
        Assert.Equal(6, ReadPixel(result, 1, 0)[0]);
        Assert.Equal(10, ReadPixel(result, 2, 0)[0]);
        Assert.Equal(254, ReadPixel(result, 63, 0)[0]);
        result.Dispose();
    }

    /// <summary>
    /// The average is taken on premultiplied alpha, so an opaque white pixel beside three fully transparent
    /// pixels stays white at quarter alpha. A straight (non-premultiplied) average of the same block would
    /// return roughly 64 in each colour channel, the dark halo the spec calls out.
    /// </summary>
    [Fact]
    public void Downscale_DoesNotDarkenColorAgainstTransparentNeighbours() {
        using TextureAsset source = CreateTexture(2, 2, (x, y) =>
            x == 0 && y == 0 ? [255, 255, 255, 255] : [0, 0, 0, 0]);

        TextureAsset result = DreamcastTextureDownscaler.Downscale(source, 1);

        Assert.Equal((ushort)1, result.Width);
        Assert.Equal((ushort)1, result.Height);
        byte[] pixel = ReadPixel(result, 0, 0);
        Assert.Equal(255, pixel[0]);
        Assert.Equal(255, pixel[1]);
        Assert.Equal(255, pixel[2]);
        // Alpha is the plain mean of the four samples: (255 + 0 + 0 + 0) / 4, rounded.
        Assert.Equal(64, pixel[3]);
        result.Dispose();
    }

    /// <summary>A block whose samples are all fully transparent yields transparent black rather than a division by zero.</summary>
    [Fact]
    public void Downscale_EmitsTransparentBlackForAFullyTransparentBlock() {
        using TextureAsset source = CreateTexture(2, 2, (_, _) => [200, 100, 50, 0]);

        TextureAsset result = DreamcastTextureDownscaler.Downscale(source, 1);

        Assert.Equal(new byte[] { 0, 0, 0, 0 }, ReadPixel(result, 0, 0));
        result.Dispose();
    }

    /// <summary>
    /// Colour and alpha are averaged independently in the right way: two opaque red pixels and two half-alpha
    /// blue pixels give the alpha-weighted colour, not the straight mean.
    /// </summary>
    [Fact]
    public void Downscale_WeightsColorByAlpha() {
        using TextureAsset source = CreateTexture(2, 2, (x, _) =>
            x == 0 ? [255, 0, 0, 255] : [0, 0, 255, 51]);

        TextureAsset result = DreamcastTextureDownscaler.Downscale(source, 1);

        // Premultiplied sums: red 2*255*255 = 130050, blue 2*255*51 = 26010, alpha 2*255 + 2*51 = 612.
        // Unpremultiplied: red = 130050/612 = 212.5 -> 213, blue = 26010/612 = 42.5 -> 43.
        // Alpha is the plain mean over four samples: 612/4 = 153.
        byte[] pixel = ReadPixel(result, 0, 0);
        Assert.Equal(213, pixel[0]);
        Assert.Equal(0, pixel[1]);
        Assert.Equal(43, pixel[2]);
        Assert.Equal(153, pixel[3]);
        result.Dispose();
    }

    /// <summary>An odd axis halves to the ceiling and its remainder column is averaged on its own.</summary>
    [Fact]
    public void Downscale_HalvesAnOddAxisToTheCeilingWithoutReadingPastTheEnd() {
        using TextureAsset source = CreateTexture(5, 1, (x, _) => [(byte)(x * 10), 0, 0, 255]);

        TextureAsset result = DreamcastTextureDownscaler.Downscale(source, 3);

        Assert.Equal((ushort)3, result.Width);
        Assert.Equal((ushort)1, result.Height);
        Assert.Equal(5, ReadPixel(result, 0, 0)[0]);
        Assert.Equal(25, ReadPixel(result, 1, 0)[0]);
        // The lone remainder column keeps its own value rather than being blended with anything.
        Assert.Equal(40, ReadPixel(result, 2, 0)[0]);
        result.Dispose();
    }

    /// <summary>Halving repeats until both axes are within the cap, even for a very tall source.</summary>
    [Fact]
    public void Downscale_RepeatsUntilBothAxesAreWithinTheCap() {
        using TextureAsset source = CreateTexture(16, 256, (_, _) => [9, 9, 9, 255]);

        TextureAsset result = DreamcastTextureDownscaler.Downscale(source, 64);

        Assert.Equal((ushort)4, result.Width);
        Assert.Equal((ushort)64, result.Height);
        result.Dispose();
    }

    /// <summary>A source in a format the downscale cannot read is rejected by name.</summary>
    [Fact]
    public void Downscale_RejectsANonRgba32Source() {
        using TextureAsset source = new() {
            Id = "textures/icon",
            Width = 2,
            Height = 2,
            Colors = new byte[8],
            PaletteColors = [],
            ColorFormat = TextureAssetColorFormat.Rgba4444,
            AlphaPrecision = TextureAssetAlphaPrecision.A8
        };

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(
            () => DreamcastTextureDownscaler.Downscale(source, 1));

        Assert.Contains("textures/icon", exception.Message, StringComparison.Ordinal);
        Assert.Contains("Rgba4444", exception.Message, StringComparison.Ordinal);
    }

    /// <summary>A pixel buffer whose length disagrees with the declared dimensions is rejected.</summary>
    [Fact]
    public void Downscale_RejectsAPixelBufferOfTheWrongLength() {
        using TextureAsset source = new() {
            Id = "textures/icon",
            Width = 4,
            Height = 4,
            Colors = new byte[8],
            PaletteColors = [],
            ColorFormat = TextureAssetColorFormat.Rgba32,
            AlphaPrecision = TextureAssetAlphaPrecision.A8
        };

        Assert.Throws<InvalidOperationException>(() => DreamcastTextureDownscaler.Downscale(source, 2));
    }

    /// <summary>A negative cap is a caller error rather than a content error.</summary>
    [Fact]
    public void Downscale_RejectsANegativeCap() {
        using TextureAsset source = CreateTexture(2, 2, (_, _) => [1, 1, 1, 255]);

        Assert.Throws<ArgumentOutOfRangeException>(() => DreamcastTextureDownscaler.Downscale(source, -1));
    }

    /// <summary>The identifiers the cooker reports failures with survive the downscale.</summary>
    [Fact]
    public void Downscale_PreservesTheAssetIdentity() {
        using TextureAsset source = CreateTexture(4, 4, (_, _) => [1, 1, 1, 255]);
        source.Id = "textures/icon";
        source.RuntimeAssetId = 0xABCDEF01UL;

        TextureAsset result = DreamcastTextureDownscaler.Downscale(source, 2);

        Assert.Equal("textures/icon", result.Id);
        Assert.Equal(0xABCDEF01UL, result.RuntimeAssetId);
        result.Dispose();
    }

    static TextureAsset CreateTexture(int width, int height, Func<int, int, byte[]> pixel) {
        byte[] colors = new byte[width * height * 4];
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                byte[] rgba = pixel(x, y);
                Buffer.BlockCopy(rgba, 0, colors, ((y * width) + x) * 4, 4);
            }
        }

        return new TextureAsset {
            Width = (ushort)width,
            Height = (ushort)height,
            Colors = colors,
            PaletteColors = [],
            ColorFormat = TextureAssetColorFormat.Rgba32,
            AlphaPrecision = TextureAssetAlphaPrecision.A8
        };
    }

    static byte[] ReadPixel(TextureAsset texture, int x, int y) {
        int offset = ((y * texture.Width) + x) * 4;
        return [texture.Colors[offset], texture.Colors[offset + 1], texture.Colors[offset + 2], texture.Colors[offset + 3]];
    }
}
