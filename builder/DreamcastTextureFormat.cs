using helengine;
using helengine.editor;

namespace helengine.dreamcast.builder;

/// <summary>Describes a selectable PVR pixel format and its independently encoded storage layout.</summary>
public sealed class DreamcastTextureFormat {
    /// <summary>Stable editor-facing format identifier.</summary>
    public string Id { get; }
    /// <summary>PVR pixel code from ARGB1555 (0) through PAL8 (6).</summary>
    public int PixelFormat { get; }
    /// <summary>Whether the payload contains a full 256-vector codebook and block indices.</summary>
    public bool Vq { get; }
    /// <summary>Whether all mip levels are stored smallest-first at the hardware offsets.</summary>
    public bool Mipmaps { get; }
    /// <summary>Whether words are row-major instead of Morton ordered.</summary>
    public bool Linear { get; }
    /// <summary>Flags persisted in the version-two cooked header.</summary>
    public ushort Flags => (ushort)(PixelFormat | (Vq ? 8 : 0) | (Mipmaps ? 16 : 0) | (Linear ? 32 : 0));
    /// <summary>Number of RGBA8888 palette entries, or zero for direct-color formats.</summary>
    public int PaletteEntries => PixelFormat == 5 ? 16 : PixelFormat == 6 ? 256 : 0;

    /// <summary>Creates an immutable descriptor for a hardware-valid pixel/layout combination.</summary>
    public DreamcastTextureFormat(string id, int pixelFormat, bool vq, bool mipmaps, bool linear) {
        Id = id; PixelFormat = pixelFormat; Vq = vq; Mipmaps = mipmaps; Linear = linear;
    }

    /// <summary>Lists the alpha precisions that survive this pixel encoding.</summary>
    public TextureAssetAlphaPrecision[] GetAlphaPrecisions() {
        if (PixelFormat == 0) return [TextureAssetAlphaPrecision.Opaque, TextureAssetAlphaPrecision.Binary];
        if (PixelFormat == 2) return [TextureAssetAlphaPrecision.Opaque, TextureAssetAlphaPrecision.Binary, TextureAssetAlphaPrecision.A4];
        if (PixelFormat >= 5) return [TextureAssetAlphaPrecision.Opaque, TextureAssetAlphaPrecision.Binary, TextureAssetAlphaPrecision.A4, TextureAssetAlphaPrecision.A8];
        return [TextureAssetAlphaPrecision.Opaque];
    }

    /// <summary>Returns the aligned GPU upload size, excluding palette RAM.</summary>
    public int GetPixelBytes(int side) {
        return (DreamcastPvrPixelCodec.PayloadBytes(side, side, PixelFormat, Mipmaps, Vq) + 31) & ~31;
    }
}
