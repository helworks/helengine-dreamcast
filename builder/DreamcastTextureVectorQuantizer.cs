using System.Buffers.Binary;
using helengine.editor;

namespace helengine.dreamcast.builder;

/// <summary>Produces deterministic PVR VQ blocks with exact encoding up to 256 distinct vectors and frequency-ranked quantization beyond that.</summary>
public sealed class DreamcastTextureVectorQuantizer {
    /// <summary>Builds one shared 2 KiB codebook followed by Morton-ordered index planes at the hardware mip offsets.</summary>
    public byte[] Encode(IReadOnlyList<byte[]> levels, int side, DreamcastTextureFormat format) {
        Dictionary<ulong, int> histogram = [];
        foreach (byte[] level in levels) {
            for (int offset = 0; offset < level.Length; offset += 8) {
                ulong vector = ReadVector(level, offset);
                histogram.TryGetValue(vector, out int count);
                histogram[vector] = count + 1;
            }
        }
        ulong[] vectors = histogram.OrderByDescending(pair => pair.Value).ThenBy(pair => pair.Key).Take(256).Select(pair => pair.Key).ToArray();
        Dictionary<ulong, byte> indices = [];
        byte[][] colors = new byte[vectors.Length][];
        byte[] result = new byte[DreamcastPvrPixelCodec.PayloadBytes(side, side, format.PixelFormat, format.Mipmaps, true)];
        for (int entry = 0; entry < vectors.Length; entry++) {
            indices.Add(vectors[entry], (byte)entry);
            BinaryPrimitives.WriteUInt64LittleEndian(result.AsSpan(entry * 8), vectors[entry]);
            colors[entry] = DecodeVector(vectors[entry], format.PixelFormat);
        }
        foreach (ulong vector in histogram.Keys) {
            if (indices.ContainsKey(vector)) continue;
            byte[] color = DecodeVector(vector, format.PixelFormat);
            int best = 0, bestDistance = int.MaxValue;
            for (int entry = 0; entry < colors.Length; entry++) {
                int distance = 0;
                for (int channel = 0; channel < color.Length; channel++) { int difference = color[channel] - colors[entry][channel]; distance += difference * difference; }
                if (distance < bestDistance) { best = entry; bestDistance = distance; }
            }
            indices.Add(vector, (byte)best);
        }
        int size = side;
        foreach (byte[] level in levels) {
            int destination = 2048 + (format.Mipmaps ? DreamcastPvrPixelCodec.MipOffset(size, format.PixelFormat, true) : 0);
            for (int offset = 0; offset < level.Length; offset += 8) result[destination++] = indices[ReadVector(level, offset)];
            size /= 2;
        }
        return result;
    }

    /// <summary>Reads a twiddled 2x2 vector; the 1x1 mip repeats its word so its hardware-selected fourth texel is valid.</summary>
    static ulong ReadVector(byte[] level, int offset) {
        if (level.Length == 2) {
            ulong word = BinaryPrimitives.ReadUInt16LittleEndian(level);
            return word | word << 16 | word << 32 | word << 48;
        }
        return BinaryPrimitives.ReadUInt64LittleEndian(level.AsSpan(offset, 8));
    }

    /// <summary>Compares vectors in expanded color space; YUV pairs are decoded horizontally and bumps compare normal directions.</summary>
    static byte[] DecodeVector(ulong vector, int format) {
        byte[] colors = new byte[16];
        for (int pixel = 0; pixel < 4; pixel++) {
            ushort word = (ushort)(vector >> (pixel * 16));
            if (format == 3) {
                ushort left = (ushort)(vector >> ((pixel & 1) * 16));
                ushort right = (ushort)(vector >> (((pixel & 1) + 2) * 16));
                DreamcastPvrPixelCodec.DecodeYuvWord(word, (left & 255) - 128, (right & 255) - 128, colors, pixel * 4);
            } else DreamcastPvrPixelCodec.DecodeWord(word, format, colors, pixel * 4);
        }
        return colors;
    }
}
