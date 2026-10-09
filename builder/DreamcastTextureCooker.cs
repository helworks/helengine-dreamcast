namespace helengine.dreamcast.builder;

/// <summary>
/// Converts an engine-cooked texture into the Dreamcast's twiddled ARGB4444 form.
/// Padding is to a square power of two rather than per axis: the PVR twiddles a
/// non-square texture in square sub-blocks, which is materially harder to verify,
/// and square padding reduces the reorder to a plain bit interleave.
/// </summary>
public sealed class DreamcastTextureCooker {
    /// <summary>Cooks a published native format, preserving the historical version-one payload for Rgba32 settings.</summary>
    public byte[] Cook(helengine.TextureAsset asset, helengine.editor.TextureAssetProcessorSettings settings) {
        if (settings == null) throw new ArgumentNullException(nameof(settings));
        return settings.ColorFormatId == "Rgba32" ? Cook(asset) : new DreamcastNativeTextureCooker().Cook(asset, settings);
    }
    /// <summary>Cooks one texture asset into dc-texture-v1 bytes.</summary>
    /// <param name="asset">Engine-cooked source texture.</param>
    /// <returns>Header followed by twiddled ARGB4444 pixels.</returns>
    public byte[] Cook(helengine.TextureAsset asset) {
        if (asset == null) {
            throw new ArgumentNullException(nameof(asset));
        }

        string name = string.IsNullOrWhiteSpace(asset.Id) ? "<unnamed>" : asset.Id;
        if (asset.ColorFormat != helengine.TextureAssetColorFormat.Rgba32 &&
            asset.ColorFormat != helengine.TextureAssetColorFormat.Rgba4444) {
            throw new InvalidOperationException(
                $"Dreamcast {DreamcastTextureSchemaIds.FormatId} cannot consume texture '{name}' in format {asset.ColorFormat}; expected Rgba32 or Rgba4444.");
        }
        if (asset.Width == 0 || asset.Height == 0) {
            throw new InvalidOperationException(
                $"Dreamcast {DreamcastTextureSchemaIds.FormatId} rejects zero-sized texture '{name}' ({asset.Width}x{asset.Height}).");
        }

        // Pad up to the smallest side the PVR can sample. A one-by-one icon is
        // legitimate art and must not fail a build; it becomes an eight-by-eight
        // payload whose header still records the real one-by-one used extent.
        int side = Math.Max(DreamcastTextureSchemaIds.MinimumSide,
                            NextPowerOfTwo(Math.Max(asset.Width, asset.Height)));
        if (side > DreamcastTextureSchemaIds.MaximumSide) {
            throw new InvalidOperationException(
                $"Dreamcast {DreamcastTextureSchemaIds.FormatId} rejects texture '{name}': padded side {side} exceeds the platform limit of {DreamcastTextureSchemaIds.MaximumSide}, above which the cooked payload cannot be read at load.");
        }

        ushort[] linear = new ushort[side * side];
        for (int y = 0; y < asset.Height; y++) {
            for (int x = 0; x < asset.Width; x++) {
                linear[(y * side) + x] = ReadPixel(asset, x, y);
            }
        }

        ushort[] twiddled = DreamcastTextureTwiddle.Twiddle(linear, side);
        byte[] cooked = new byte[DreamcastTextureSchemaIds.HeaderBytes + (twiddled.Length * 2)];
        BitConverter.TryWriteBytes(cooked.AsSpan(0), DreamcastTextureSchemaIds.Magic);
        BitConverter.TryWriteBytes(cooked.AsSpan(4), DreamcastTextureSchemaIds.Version);
        BitConverter.TryWriteBytes(cooked.AsSpan(6), (ushort)0);
        BitConverter.TryWriteBytes(cooked.AsSpan(8), (ushort)side);
        BitConverter.TryWriteBytes(cooked.AsSpan(10), asset.Width);
        BitConverter.TryWriteBytes(cooked.AsSpan(12), asset.Height);
        BitConverter.TryWriteBytes(cooked.AsSpan(14), (ushort)0);
        for (int i = 0; i < twiddled.Length; i++) {
            BitConverter.TryWriteBytes(cooked.AsSpan(DreamcastTextureSchemaIds.HeaderBytes + (i * 2)), twiddled[i]);
        }
        return cooked;
    }

    /// <summary>Reads one source pixel and returns it as ARGB4444.</summary>
    static ushort ReadPixel(helengine.TextureAsset asset, int x, int y) {
        if (asset.ColorFormat == helengine.TextureAssetColorFormat.Rgba4444) {
            int offset = ((y * asset.Width) + x) * 2;
            ushort rgba = BitConverter.ToUInt16(asset.Colors, offset);
            // The engine stores red in the low nibble and alpha in the high nibble.
            return (ushort)((rgba & 0xF0F0) | ((rgba & 0x000F) << 8) | ((rgba & 0x0F00) >> 8));
        }

        int index = ((y * asset.Width) + x) * 4;
        int red = asset.Colors[index] >> 4;
        int green = asset.Colors[index + 1] >> 4;
        int blue = asset.Colors[index + 2] >> 4;
        int alpha = asset.Colors[index + 3] >> 4;
        return (ushort)((alpha << 12) | (red << 8) | (green << 4) | blue);
    }

    /// <summary>Returns the smallest power of two greater than or equal to the value.</summary>
    static int NextPowerOfTwo(int value) {
        int result = 1;
        while (result < value) {
            result <<= 1;
        }
        return result;
    }
}
