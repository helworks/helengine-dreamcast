using helengine;
using helengine.baseplatform.Definitions;

namespace helengine.dreamcast.builder;

/// <summary>Publishes native PVR formats while retaining the historical Rgba32-to-ARGB4444 setting.</summary>
public static class DreamcastTextureFormatCatalog {
    /// <summary>Validated native descriptors shared by cooking, editor capabilities and package validation.</summary>
    public static IReadOnlyList<DreamcastTextureFormat> Formats { get; } = CreateFormats();

    /// <summary>Resolves a published format identifier, rejecting unrelated platform and numeric identifiers.</summary>
    public static DreamcastTextureFormat Resolve(string id) {
        foreach (DreamcastTextureFormat format in Formats) if (format.Id == id) return format;
        throw new InvalidOperationException($"Unsupported Dreamcast texture color format '{id}'.");
    }

    /// <summary>Resolves persisted format flags without accepting impossible VQ, palette or linear combinations.</summary>
    public static DreamcastTextureFormat ResolveFlags(ushort flags) {
        foreach (DreamcastTextureFormat format in Formats) if (format.Flags == flags) return format;
        throw new InvalidOperationException($"Invalid Dreamcast texture flags 0x{flags:X4}.");
    }

    /// <summary>Creates the capabilities consumed by the editor's platform-specific texture controls.</summary>
    public static PlatformTextureFormatCapabilityDefinition CreateCapabilities() {
        List<string> ids = [TextureAssetColorFormat.Rgba32.ToString()];
        List<PlatformTextureFormatCombinationDefinition> combinations = [
            new("Rgba32", TextureAssetAlphaPrecision.Opaque), new("Rgba32", TextureAssetAlphaPrecision.A8)
        ];
        foreach (DreamcastTextureFormat format in Formats) {
            ids.Add(format.Id);
            foreach (TextureAssetAlphaPrecision alpha in format.GetAlphaPrecisions()) combinations.Add(new(format.Id, alpha));
        }
        return new(ids.ToArray(), [TextureAssetAlphaPrecision.Opaque, TextureAssetAlphaPrecision.Binary, TextureAssetAlphaPrecision.A4, TextureAssetAlphaPrecision.A8], combinations.ToArray());
    }

    /// <summary>Enumerates seven twiddled formats, mipmaps, five direct-color VQ formats and linear direct-color variants.</summary>
    static IReadOnlyList<DreamcastTextureFormat> CreateFormats() {
        string[] names = ["DcArgb1555", "DcRgb565", "DcArgb4444", "DcYuv422", "DcBump", "DcPal4", "DcPal8"];
        List<DreamcastTextureFormat> formats = [];
        for (int pixel = 0; pixel < names.Length; pixel++) {
            formats.Add(new(names[pixel], pixel, false, false, false));
            formats.Add(new(names[pixel] + "_Mipmapped", pixel, false, true, false));
            if (pixel < 5) {
                formats.Add(new(names[pixel] + "_Vq", pixel, true, false, false));
                formats.Add(new(names[pixel] + "_VqMipmapped", pixel, true, true, false));
                formats.Add(new(names[pixel] + "_Linear", pixel, false, false, true));
            }
        }
        return formats.AsReadOnly();
    }
}
