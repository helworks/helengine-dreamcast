using System.Buffers.Binary;
using helengine;
using helengine.editor;

namespace helengine.dreamcast.builder;

/// <summary>Encodes all PVR pixel types, global palettes, mip chains and VQ into version-two DCTX payloads.</summary>
public sealed class DreamcastNativeTextureCooker {
    /// <summary>Converts source pixels into a padded square hardware texture with an optional external palette tail.</summary>
    public byte[] Cook(TextureAsset asset, TextureAssetProcessorSettings settings) {
        if (asset == null) throw new ArgumentNullException(nameof(asset));
        if (settings == null) throw new ArgumentNullException(nameof(settings));
        DreamcastTextureFormat format = DreamcastTextureFormatCatalog.Resolve(settings.ColorFormatId);
        if (!format.GetAlphaPrecisions().Contains(settings.AlphaPrecision)) throw new InvalidOperationException($"{format.Id} cannot store {settings.AlphaPrecision} alpha.");
        if (asset.Width == 0 || asset.Height == 0) throw new InvalidOperationException("Dreamcast textures require positive dimensions.");
        int side = 8;
        while (side < Math.Max(asset.Width, asset.Height)) side *= 2;
        if (side > DreamcastTextureSchemaIds.MaximumSide) throw new InvalidOperationException($"Dreamcast texture '{asset.Id}' exceeds the {DreamcastTextureSchemaIds.MaximumSide} pixel padded-side limit.");
        byte[] source = TextureAssetPixelCodec.DecodeToRgba32(asset);
        byte[] rgba = new byte[side * side * 4];
        for (int y = 0; y < asset.Height; y++) Buffer.BlockCopy(source, y * asset.Width * 4, rgba, y * side * 4, asset.Width * 4);
        for (int pixel = 0; pixel < rgba.Length; pixel += 4) {
            // Padding stays zero for alpha formats; opaque formats always have opaque texels.
            int alpha = rgba[pixel + 3];
            rgba[pixel + 3] = settings.AlphaPrecision == TextureAssetAlphaPrecision.Opaque ? (byte)255 : settings.AlphaPrecision == TextureAssetAlphaPrecision.Binary ? (byte)(alpha >= 128 ? 255 : 0) : settings.AlphaPrecision == TextureAssetAlphaPrecision.A4 ? (byte)((alpha >> 4) * 17) : (byte)alpha;
        }
        byte[] palette = new byte[0];
        if (format.PaletteEntries != 0) {
            using TextureAsset padded = new() { Width = (ushort)side, Height = (ushort)side, ColorFormat = TextureAssetColorFormat.Rgba32, AlphaPrecision = settings.AlphaPrecision, Colors = rgba, PaletteColors = new byte[0] };
            using TextureAsset indexed = new TextureAssetIndexedQuantizer().Quantize(padded, format.PaletteEntries, format.PixelFormat == 5 ? TextureAssetColorFormat.Indexed4 : TextureAssetColorFormat.Indexed8, settings.AlphaPrecision);
            palette = (byte[])indexed.PaletteColors.Clone();
        }
        int pixelBytes = format.GetPixelBytes(side);
        byte[] cooked = new byte[16 + pixelBytes + palette.Length];
        BinaryPrimitives.WriteUInt32LittleEndian(cooked.AsSpan(0), DreamcastTextureSchemaIds.Magic);
        BinaryPrimitives.WriteUInt16LittleEndian(cooked.AsSpan(4), 2);
        BinaryPrimitives.WriteUInt16LittleEndian(cooked.AsSpan(6), format.Flags);
        BinaryPrimitives.WriteUInt16LittleEndian(cooked.AsSpan(8), (ushort)side);
        BinaryPrimitives.WriteUInt16LittleEndian(cooked.AsSpan(10), asset.Width);
        BinaryPrimitives.WriteUInt16LittleEndian(cooked.AsSpan(12), asset.Height);
        BinaryPrimitives.WriteUInt16LittleEndian(cooked.AsSpan(14), (ushort)format.PaletteEntries);
        List<byte[]> levels = [EncodeLevel(rgba, side, format, palette)];
        if (format.Mipmaps) {
            byte[] previous = rgba;
            for (int size = side / 2; size >= 1; size /= 2) {
                previous = Downsample(previous, size * 2);
                if (settings.AlphaPrecision == TextureAssetAlphaPrecision.Binary) {
                    for (int offset = 3; offset < previous.Length; offset += 4) previous[offset] = previous[offset] >= 128 ? (byte)255 : (byte)0;
                }
                levels.Add(EncodeLevel(previous, size, format, palette));
            }
        }
        if (format.Vq) {
            byte[] vq = new DreamcastTextureVectorQuantizer().Encode(levels, side, format);
            Buffer.BlockCopy(vq, 0, cooked, 16, vq.Length);
        } else {
            int size = side;
            foreach (byte[] level in levels) {
                int offset = format.Mipmaps ? DreamcastPvrPixelCodec.MipOffset(size, format.PixelFormat, false) : 0;
                Buffer.BlockCopy(level, 0, cooked, 16 + offset, level.Length);
                size /= 2;
            }
        }
        Buffer.BlockCopy(palette, 0, cooked, 16 + pixelBytes, palette.Length);
        return cooked;
    }

    /// <summary>Encodes a square mip level; horizontal YUV pairs are formed before the Morton reorder.</summary>
    static byte[] EncodeLevel(byte[] rgba, int side, DreamcastTextureFormat format, byte[] palette) {
        int pixelFormat = format.PixelFormat;
        byte[] bytes = new byte[pixelFormat == 5 ? Math.Max(1, side * side / 2) : pixelFormat == 6 ? side * side : side * side * 2];
        for (int y = 0; y < side; y++) {
            for (int x = 0; x < side; x++) {
                int source = (y * side + x) * 4;
                int destination = format.Linear ? y * side + x : DreamcastPvrPixelCodec.TwiddledIndex(x, y, side, side);
                if (pixelFormat >= 5) {
                    int index = ClosestPalette(rgba, source, palette);
                    if (pixelFormat == 6) bytes[destination] = (byte)index;
                    else if (side == 1) bytes[0] = (byte)(index << 4);
                    else bytes[destination / 2] |= (byte)(index << ((destination & 1) * 4));
                } else {
                    ushort word;
                    if (pixelFormat == 3 && side > 1) {
                        int first = (y * side + (x & ~1)) * 4;
                        uint pair = DreamcastPvrPixelCodec.EncodeYuvPair(rgba, first, first + 4);
                        word = (ushort)(pair >> ((x & 1) * 16));
                    } else {
                        word = DreamcastPvrPixelCodec.EncodeWord(rgba, source, pixelFormat == 3 ? 1 : pixelFormat);
                    }
                    BinaryPrimitives.WriteUInt16LittleEndian(bytes.AsSpan(destination * 2), word);
                }
            }
        }
        return bytes;
    }

    /// <summary>Reuses one palette across all levels, measuring squared RGBA distance so transparent colors remain distinct.</summary>
    static int ClosestPalette(byte[] rgba, int offset, byte[] palette) {
        int best = 0, bestDistance = int.MaxValue;
        for (int entry = 0; entry < palette.Length / 4; entry++) {
            int distance = 0;
            for (int channel = 0; channel < 4; channel++) { int difference = rgba[offset + channel] - palette[entry * 4 + channel]; distance += difference * difference; }
            if (distance < bestDistance) { best = entry; bestDistance = distance; }
        }
        return best;
    }

    /// <summary>Builds a premultiplied-alpha box-filtered level; normal-map vectors are normalized when encoded.</summary>
    static byte[] Downsample(byte[] rgba, int side) {
        int nextSide = side / 2;
        byte[] next = new byte[nextSide * nextSide * 4];
        for (int y = 0; y < nextSide; y++) {
            for (int x = 0; x < nextSide; x++) {
                int destination = (y * nextSide + x) * 4;
                int alphaSum = 0;
                int[] sums = new int[3];
                for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) {
                    int source = ((y * 2 + dy) * side + x * 2 + dx) * 4;
                    int alpha = rgba[source + 3]; alphaSum += alpha;
                    for (int channel = 0; channel < 3; channel++) sums[channel] += rgba[source + channel] * alpha;
                }
                next[destination + 3] = (byte)((alphaSum + 2) / 4);
                for (int channel = 0; channel < 3; channel++) next[destination + channel] = alphaSum == 0 ? (byte)0 : (byte)((sums[channel] + alphaSum / 2) / alphaSum);
            }
        }
        return next;
    }
}
