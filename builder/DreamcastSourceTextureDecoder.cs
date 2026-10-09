using System.Drawing;

namespace helengine.dreamcast.builder;

/// <summary>
/// Decodes one cook work item source into an uncooked straight-RGBA texture asset for the Dreamcast cook. Work
/// item sources are either authored image files or serialized engine texture assets that the editor generated for
/// the platform, such as the cached platform font atlases and the externalized editor default font atlas.
/// </summary>
public sealed class DreamcastSourceTextureDecoder {
    /// <summary>
    /// Magic bytes that begin every serialized engine binary asset. Generated texture sources reach the cook under
    /// several extensions, so the payload itself decides which decoder runs.
    /// </summary>
    static readonly byte[] EngineAssetMagicBytes = [(byte)'H', (byte)'E', (byte)'L', (byte)'E'];

    /// <summary>
    /// Decodes one source texture into a raw RGBA texture asset.
    /// </summary>
    /// <param name="sourceAssetPath">Absolute source image or engine texture asset path.</param>
    /// <returns>Raw texture asset containing straight RGBA pixel bytes.</returns>
    public helengine.TextureAsset Decode(string sourceAssetPath) {
        if (string.IsNullOrWhiteSpace(sourceAssetPath)) {
            throw new ArgumentException("Source asset path is required.", nameof(sourceAssetPath));
        }
        if (!File.Exists(sourceAssetPath)) {
            throw new FileNotFoundException("Source texture file was not found.", sourceAssetPath);
        }

        if (HasEngineAssetMagic(sourceAssetPath)) {
            return DecodeEngineTextureAsset(sourceAssetPath);
        }

        if (string.Equals(Path.GetExtension(sourceAssetPath), ".pvr", StringComparison.OrdinalIgnoreCase)) {
            using FileStream stream = File.OpenRead(sourceAssetPath);
            return new helengine.editor.DreamcastPvrTextureImporter().ImportTexture(stream);
        }

        return DecodeImageFile(sourceAssetPath);
    }

    /// <summary>
    /// Determines whether one source file begins with the engine binary asset magic bytes.
    /// </summary>
    /// <param name="sourceAssetPath">Absolute source path to inspect.</param>
    /// <returns><c>true</c> when the file is a serialized engine asset.</returns>
    static bool HasEngineAssetMagic(string sourceAssetPath) {
        byte[] magicBuffer = new byte[EngineAssetMagicBytes.Length];
        using FileStream stream = new(sourceAssetPath, FileMode.Open, FileAccess.Read, FileShare.Read);
        if (stream.ReadAtLeast(magicBuffer, magicBuffer.Length, false) != magicBuffer.Length) {
            return false;
        }

        return magicBuffer.AsSpan().SequenceEqual(EngineAssetMagicBytes);
    }

    /// <summary>
    /// Decodes one authored image file through the system image decoder.
    /// </summary>
    /// <param name="sourceAssetPath">Absolute source image path.</param>
    /// <returns>Raw texture asset containing straight RGBA pixel bytes.</returns>
    helengine.TextureAsset DecodeImageFile(string sourceAssetPath) {
        using Bitmap bitmap = new(sourceAssetPath);
        if (bitmap.Width < 1 || bitmap.Height < 1) {
            throw new InvalidOperationException("Source textures must have positive dimensions.");
        }

        byte[] rgbaBytes = new byte[bitmap.Width * bitmap.Height * 4];
        int writeOffset = 0;
        for (int y = 0; y < bitmap.Height; y++) {
            for (int x = 0; x < bitmap.Width; x++) {
                Color pixel = bitmap.GetPixel(x, y);
                rgbaBytes[writeOffset++] = pixel.R;
                rgbaBytes[writeOffset++] = pixel.G;
                rgbaBytes[writeOffset++] = pixel.B;
                rgbaBytes[writeOffset++] = pixel.A;
            }
        }

        return new helengine.TextureAsset {
            Width = checked((ushort)bitmap.Width),
            Height = checked((ushort)bitmap.Height),
            Colors = rgbaBytes,
            PaletteColors = Array.Empty<byte>(),
            ColorFormat = helengine.TextureAssetColorFormat.Rgba32,
            AlphaPrecision = helengine.TextureAssetAlphaPrecision.A8
        };
    }

    /// <summary>
    /// Reads one serialized engine texture asset, which the editor already stored as straight RGBA pixel bytes.
    /// </summary>
    /// <param name="sourceAssetPath">Absolute engine texture asset path.</param>
    /// <returns>Raw texture asset containing straight RGBA pixel bytes.</returns>
    helengine.TextureAsset DecodeEngineTextureAsset(string sourceAssetPath) {
        helengine.Asset deserializedAsset = helengine.files.AssetSerializer.DeserializeFromBytes(File.ReadAllBytes(sourceAssetPath));
        // TextureAsset owns native pixel and palette buffers and implements IDisposable, so the deserialized
        // instance is released on every exit, including the rejection paths below. The returned asset takes a
        // copy of the pixel bytes rather than aliasing the source buffer, so the two assets never share a buffer
        // one of them is about to release.
        helengine.TextureAsset sourceTexture = deserializedAsset as helengine.TextureAsset;
        if (sourceTexture == null) {
            if (deserializedAsset is IDisposable unexpectedDisposable) {
                unexpectedDisposable.Dispose();
            }
            throw new InvalidOperationException($"Dreamcast texture cook source '{sourceAssetPath}' did not contain a texture asset.");
        }

        try {
            if (sourceTexture.ColorFormat != helengine.TextureAssetColorFormat.Rgba32) {
                throw new InvalidOperationException($"Dreamcast texture cook source '{sourceAssetPath}' must be Rgba32; received '{sourceTexture.ColorFormat}'.");
            }
            if (sourceTexture.Width < 1 || sourceTexture.Height < 1) {
                throw new InvalidOperationException("Source textures must have positive dimensions.");
            }

            int expectedByteLength = sourceTexture.Width * sourceTexture.Height * 4;
            if (sourceTexture.Colors == null || sourceTexture.Colors.Length != expectedByteLength) {
                throw new InvalidOperationException($"Dreamcast texture cook source '{sourceAssetPath}' pixel bytes must match Width * Height * 4.");
            }

            byte[] ownedColors = new byte[expectedByteLength];
            Buffer.BlockCopy(sourceTexture.Colors, 0, ownedColors, 0, expectedByteLength);
            return new helengine.TextureAsset {
                Width = sourceTexture.Width,
                Height = sourceTexture.Height,
                Colors = ownedColors,
                PaletteColors = Array.Empty<byte>(),
                ColorFormat = helengine.TextureAssetColorFormat.Rgba32,
                AlphaPrecision = sourceTexture.AlphaPrecision
            };
        } finally {
            sourceTexture.Dispose();
        }
    }
}
