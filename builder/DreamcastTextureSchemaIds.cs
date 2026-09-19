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

    /// <summary>Largest padded square side the PVR can sample.</summary>
    public const int MaximumSide = 1024;
}
