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

    /// <summary>
    /// Largest total video memory, in bytes, that the cooked textures of one build may occupy.
    /// <para>
    /// This is a third budget and is not the same as either of the other two. The ROMFS payload cap in
    /// <c>DreamcastContentStager</c> bounds what the embedded filesystem costs in main RAM;
    /// <see cref="MaximumSide"/> bounds what one texture costs. This bounds what the whole set costs in video
    /// memory, where textures are allocated once from the pool the tile accelerator leaves behind and held for
    /// the scene's lifetime. Nothing on the console recovers from an allocation failure mid-load, so the total
    /// is checked on the developer's machine.
    /// </para>
    /// <para>
    /// Derivation. The runtime's diagnostic snapshot reported 6,937,344 bytes of free video memory after tile
    /// accelerator initialisation on the 2026-09-16 ten-minute run, recorded in <c>docs/Validation.md</c>. That
    /// build predates the sprite path; enabling the translucent object-pointer buffer and disabling translucent
    /// autosort costs roughly 75 KiB more of that pool, leaving about 6,860,544. The limit below is 4 MiB,
    /// roughly sixty per cent of that, so more than 2.6 MiB stays unaccounted for as margin. It is deliberately
    /// a round number set below a measurement rather than derived exactly from one, because the measurement on
    /// a build carrying the sprite path has not been taken yet: the design requires reading the snapshot on such
    /// a build and recording both the figure and the limit derived from it. Tighten this then.
    /// </para>
    /// <para>
    /// Only the pixel payload counts. The sixteen-byte header is read in the builder and never uploaded, and
    /// <c>pvr_mem_malloc</c> is asked for exactly <c>side * side * 2</c> bytes.
    /// </para>
    /// </summary>
    public const long MaximumTotalVideoMemoryBytes = 4L * 1024L * 1024L;
}
