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

    /// <summary>Largest padded square side the PVR can sample.</summary>
    public const int MaximumSide = 1024;
}
