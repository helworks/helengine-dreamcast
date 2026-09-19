namespace helengine.dreamcast.builder;

/// <summary>Stable identifiers and limits for the Dreamcast cooked texture format.</summary>
public static class DreamcastTextureSchemaIds {
    /// <summary>Format identifier recorded in build reports and diagnostics.</summary>
    public const string FormatId = "dc-texture-v1";

    /// <summary>Header magic, the ASCII bytes D C T X in little-endian order.</summary>
    public const uint Magic = 0x58544344u;

    /// <summary>Current format version.</summary>
    public const ushort Version = 1;

    /// <summary>Header size in bytes, before the pixel payload.</summary>
    public const int HeaderBytes = 16;

    /// <summary>
    /// Smallest padded square side the PVR can sample.
    /// <para>
    /// Below eight the hardware cannot be driven correctly: the texture U/V size
    /// code is derived as <c>ctz(side) - 3</c>, which underflows, and KallistiOS
    /// uploads through <c>sq_cpy</c>, which copies whole thirty-two byte store
    /// queue bursts and would silently drop a payload smaller than that. Small
    /// art is padded up to this side rather than rejected; the used extent in
    /// the header still records the real image size.
    /// </para>
    /// </summary>
    public const int MinimumSide = 8;

    /// <summary>
    /// Largest padded square side this platform can actually load.
    /// <para>
    /// This is a <em>platform</em> limit, not a hardware one, and the two must
    /// not be tidied into a single number. The tile accelerator can sample up
    /// to a 1024 square, and the runtime parser's own upper bound
    /// (<c>kMaxSide</c> in <c>DreamcastTextureFormat.cpp</c>) still says 1024
    /// because that is hardware truth. What binds here is
    /// <c>DreamcastContentStreamSource</c>'s one MiB cooked payload read cap: a
    /// 1024 square is <c>16 + 1024 * 1024 * 2</c> = 2,097,168 bytes and can
    /// never be opened, so cooking one would build clean and always fail to
    /// load. A 512 square is <c>16 + 512 * 512 * 2</c> = 524,304 bytes and fits.
    /// If that read cap is ever raised, this constant moves and the parser's
    /// 1024 does not.
    /// </para>
    /// </summary>
    public const int MaximumSide = 512;
}
