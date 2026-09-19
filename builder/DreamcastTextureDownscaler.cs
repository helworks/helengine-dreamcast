namespace helengine.dreamcast.builder;

/// <summary>
/// Downscales a decoded straight-RGBA texture to the platform's cooked texture cap by repeated exact halving
/// with a two-by-two box average computed on premultiplied alpha.
/// </summary>
/// <remarks>
/// <para>
/// This filtering is Dreamcast-owned on purpose. A platform's cook work item carries the <em>source</em> asset
/// path and its resolved settings; nothing has resized the image by the time the platform's executor sees it, so
/// the platform decides its own filter. That is how the Nintendo 64 backend gets a box filter out of the same
/// mechanism: its executor never calls the shared <c>TextureAssetProcessor</c>, it hands the decoded source and
/// the settings straight to <c>Nintendo64TextureCooker</c>, which does the resize itself. The shared processor's
/// own <c>ResizeToMaxResolution</c> is a single-step nearest-neighbour sample, which at 256 to 64 keeps one
/// pixel in sixteen and destroys anti-aliased icon art, so this platform resizes here and then lets the shared
/// processor do only the format and alpha-precision work on an image already within the cap.
/// </para>
/// <para>
/// Two properties the design spec requires, and why each matters:
/// </para>
/// <list type="bullet">
/// <item><description>
/// Repeated exact halving rather than one arbitrary-ratio resample. 256 becomes 128 becomes 64, each step an
/// average of a disjoint two-by-two block, so no source pixel is weighted differently from its neighbours and no
/// resampling artifacts arise. Art already within the cap is returned untouched, so small hand-made art keeps
/// its exact pixels.
/// </description></item>
/// <item><description>
/// The average is computed on premultiplied alpha and then unpremultiplied. These are anti-aliased icons on
/// transparency, where the colour stored under a fully transparent pixel is arbitrary and usually black;
/// averaging straight colour against it darkens every edge, producing a halo that reads as a renderer bug.
/// </description></item>
/// </list>
/// <para>
/// An axis with an odd length cannot be halved exactly. Rather than reject legitimate art or leave it above the
/// cap, an odd axis halves to <c>(length + 1) / 2</c> and its final destination pixel averages the one-wide
/// remainder column or row. Every axis this platform actually cooks is a power of two, so that case is a
/// well-defined fallback rather than the normal path.
/// </para>
/// </remarks>
public static class DreamcastTextureDownscaler {
    /// <summary>
    /// Returns the supplied texture at or below <paramref name="maximumSide"/> on both axes.
    /// </summary>
    /// <param name="sourceTexture">Decoded straight-RGBA (<c>Rgba32</c>) texture to downscale.</param>
    /// <param name="maximumSide">Maximum permitted width or height in pixels, or zero when uncapped.</param>
    /// <returns>
    /// The same instance when no work is needed, or a new texture asset owning its own pixel buffer. The caller
    /// keeps ownership of <paramref name="sourceTexture"/> either way; a reference comparison tells the two
    /// cases apart.
    /// </returns>
    public static helengine.TextureAsset Downscale(helengine.TextureAsset sourceTexture, int maximumSide) {
        if (sourceTexture == null) {
            throw new ArgumentNullException(nameof(sourceTexture));
        }
        if (maximumSide < 0) {
            throw new ArgumentOutOfRangeException(nameof(maximumSide), "Dreamcast texture maximum side cannot be negative.");
        }

        string name = string.IsNullOrWhiteSpace(sourceTexture.Id) ? "<unnamed>" : sourceTexture.Id;
        if (sourceTexture.ColorFormat != helengine.TextureAssetColorFormat.Rgba32) {
            throw new InvalidOperationException(
                $"Dreamcast texture downscaling requires an Rgba32 source; texture '{name}' is {sourceTexture.ColorFormat}.");
        }
        if (sourceTexture.Width < 1 || sourceTexture.Height < 1) {
            throw new InvalidOperationException(
                $"Dreamcast texture downscaling rejects zero-sized texture '{name}' ({sourceTexture.Width}x{sourceTexture.Height}).");
        }
        // Width and Height are 16-bit, so their product times four bytes per pixel can leave the 32-bit range.
        // Widen the arithmetic so an oversized source fails this length check rather than passing a wrapped one.
        long expectedByteLength = (long)sourceTexture.Width * sourceTexture.Height * 4L;
        if (sourceTexture.Colors == null || sourceTexture.Colors.LongLength != expectedByteLength) {
            throw new InvalidOperationException(
                $"Dreamcast texture downscaling requires texture '{name}' pixel bytes to match Width * Height * 4.");
        }

        if (maximumSide == 0 || (sourceTexture.Width <= maximumSide && sourceTexture.Height <= maximumSide)) {
            return sourceTexture;
        }

        int width = sourceTexture.Width;
        int height = sourceTexture.Height;
        byte[] colors = sourceTexture.Colors;
        while (width > maximumSide || height > maximumSide) {
            int halvedWidth = (width + 1) / 2;
            int halvedHeight = (height + 1) / 2;
            colors = HalveOnce(colors, width, height, halvedWidth, halvedHeight);
            width = halvedWidth;
            height = halvedHeight;
        }

        return new helengine.TextureAsset {
            Id = sourceTexture.Id,
            RuntimeAssetId = sourceTexture.RuntimeAssetId,
            Width = checked((ushort)width),
            Height = checked((ushort)height),
            Colors = colors,
            PaletteColors = Array.Empty<byte>(),
            ColorFormat = helengine.TextureAssetColorFormat.Rgba32,
            AlphaPrecision = sourceTexture.AlphaPrecision
        };
    }

    /// <summary>
    /// Halves one straight-RGBA image, averaging each destination pixel's two-by-two source block on
    /// premultiplied alpha and unpremultiplying the result.
    /// </summary>
    /// <param name="sourceColors">Straight-RGBA source pixel bytes.</param>
    /// <param name="sourceWidth">Source width in pixels.</param>
    /// <param name="sourceHeight">Source height in pixels.</param>
    /// <param name="targetWidth">Halved width in pixels.</param>
    /// <param name="targetHeight">Halved height in pixels.</param>
    /// <returns>Straight-RGBA pixel bytes of the halved image.</returns>
    static byte[] HalveOnce(byte[] sourceColors, int sourceWidth, int sourceHeight, int targetWidth, int targetHeight) {
        byte[] targetColors = new byte[targetWidth * targetHeight * 4];
        for (int targetY = 0; targetY < targetHeight; targetY++) {
            int sourceRowEnd = Math.Min(sourceHeight, (targetY * 2) + 2);
            for (int targetX = 0; targetX < targetWidth; targetX++) {
                int sourceColumnEnd = Math.Min(sourceWidth, (targetX * 2) + 2);

                // Sums of colour premultiplied by alpha, alongside the plain alpha sum. Dividing a premultiplied
                // sum by the alpha sum is exactly the unpremultiplied average: fully transparent samples carry no
                // weight, so their arbitrary colour cannot darken the result.
                int redTimesAlpha = 0;
                int greenTimesAlpha = 0;
                int blueTimesAlpha = 0;
                int alphaSum = 0;
                int sampleCount = 0;
                for (int sourceY = targetY * 2; sourceY < sourceRowEnd; sourceY++) {
                    for (int sourceX = targetX * 2; sourceX < sourceColumnEnd; sourceX++) {
                        int sourceOffset = ((sourceY * sourceWidth) + sourceX) * 4;
                        int alpha = sourceColors[sourceOffset + 3];
                        redTimesAlpha += sourceColors[sourceOffset] * alpha;
                        greenTimesAlpha += sourceColors[sourceOffset + 1] * alpha;
                        blueTimesAlpha += sourceColors[sourceOffset + 2] * alpha;
                        alphaSum += alpha;
                        sampleCount++;
                    }
                }

                int targetOffset = ((targetY * targetWidth) + targetX) * 4;
                if (alphaSum == 0) {
                    // Every sample is fully transparent, so there is no colour to recover. Emitting transparent
                    // black keeps the cooked padding and the cooked image agreeing on what "nothing" looks like.
                    continue;
                }
                targetColors[targetOffset] = (byte)DivideRounded(redTimesAlpha, alphaSum);
                targetColors[targetOffset + 1] = (byte)DivideRounded(greenTimesAlpha, alphaSum);
                targetColors[targetOffset + 2] = (byte)DivideRounded(blueTimesAlpha, alphaSum);
                targetColors[targetOffset + 3] = (byte)DivideRounded(alphaSum, sampleCount);
            }
        }

        return targetColors;
    }

    /// <summary>
    /// Divides two non-negative integers, rounding halves away from zero.
    /// </summary>
    /// <param name="numerator">Non-negative numerator.</param>
    /// <param name="denominator">Positive denominator.</param>
    /// <returns>The rounded quotient.</returns>
    static int DivideRounded(int numerator, int denominator) {
        return ((numerator * 2) + denominator) / (denominator * 2);
    }
}
