using System.Text.Json;
using helengine.editor;

namespace helengine.dreamcast.builder;

/// <summary>
/// Serializes and parses the Dreamcast texture cook settings payload carried by builder-owned platform cook work items.
/// </summary>
public static class DreamcastTextureCookSettingsSerializer {
    /// <summary>
    /// Serializes one Dreamcast texture settings payload into the stable string form stored on cook work items.
    /// </summary>
    /// <param name="settings">Settings payload to serialize.</param>
    /// <returns>Serialized Dreamcast texture settings string.</returns>
    public static string Serialize(TextureAssetProcessorSettings settings) {
        if (settings == null) {
            throw new ArgumentNullException(nameof(settings));
        }

        return JsonSerializer.Serialize(new Dictionary<string, object> {
            ["maxResolution"] = settings.MaxResolution,
            ["colorFormat"] = settings.ColorFormatId,
            ["alphaPrecision"] = settings.AlphaPrecision.ToString()
        });
    }

    /// <summary>
    /// Parses one Dreamcast texture settings payload from the string form stored on cook work items. The
    /// <c>maxResolution</c>, <c>colorFormat</c>, and <c>alphaPrecision</c> properties are all required; a payload
    /// missing any of them is a malformed Dreamcast texture cook settings payload and is rejected rather than
    /// silently defaulted.
    /// </summary>
    /// <param name="serializedSettings">Serialized settings string.</param>
    /// <returns>Parsed Dreamcast texture settings payload.</returns>
    public static TextureAssetProcessorSettings Deserialize(string serializedSettings) {
        if (string.IsNullOrWhiteSpace(serializedSettings)) {
            throw new ArgumentException("Serialized Dreamcast texture settings are required.", nameof(serializedSettings));
        }

        using JsonDocument document = JsonDocument.Parse(serializedSettings);
        JsonElement root = document.RootElement;
        if (!root.TryGetProperty("maxResolution", out JsonElement maxResolutionElement)) {
            throw new InvalidOperationException("Malformed Dreamcast texture cook settings payload: 'maxResolution' is required.");
        }
        if (!root.TryGetProperty("colorFormat", out JsonElement colorFormatElement)) {
            throw new InvalidOperationException("Malformed Dreamcast texture cook settings payload: 'colorFormat' is required.");
        }
        if (!root.TryGetProperty("alphaPrecision", out JsonElement alphaPrecisionElement)) {
            throw new InvalidOperationException("Malformed Dreamcast texture cook settings payload: 'alphaPrecision' is required.");
        }

        int maxResolution = maxResolutionElement.GetInt32();
        string colorFormatName = colorFormatElement.GetString();
        string alphaPrecisionName = alphaPrecisionElement.GetString();

        if (!Enum.TryParse(alphaPrecisionName, true, out helengine.TextureAssetAlphaPrecision alphaPrecision) || !Enum.IsDefined(alphaPrecision)) {
            throw new InvalidOperationException($"Unsupported Dreamcast texture alpha precision '{alphaPrecisionName}'.");
        }
        if (maxResolution < 0 || maxResolution > DreamcastTextureSchemaIds.MaximumSide) throw new InvalidOperationException("Dreamcast maximum resolution must be 0..512.");
        if (colorFormatName != "Rgba32") {
            DreamcastTextureFormat format = DreamcastTextureFormatCatalog.Resolve(colorFormatName);
            if (!format.GetAlphaPrecisions().Contains(alphaPrecision)) throw new InvalidOperationException($"{format.Id} cannot store {alphaPrecision} alpha.");
        } else if (alphaPrecision != helengine.TextureAssetAlphaPrecision.Opaque && alphaPrecision != helengine.TextureAssetAlphaPrecision.A8) {
            throw new InvalidOperationException("Legacy Dreamcast Rgba32 settings support Opaque or A8 source alpha.");
        }

        return new TextureAssetProcessorSettings {
            MaxResolution = maxResolution,
            ColorFormatId = colorFormatName,
            AlphaPrecision = alphaPrecision
        };
    }
}
