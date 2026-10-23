using System.Globalization;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.dreamcast.builder;

/// <summary>Translates the restricted Dreamcast opaque material schema into a cooked asset.</summary>
public sealed class DreamcastMaterialCooker {
    /// <summary>Cooks one validated opaque, single-sided, untextured material that is lit by default and may opt out with lighting-mode 'unlit'.</summary>
    /// <param name="request">Material schema request supplied by the generic build graph.</param>
    /// <returns>Serialized platform material bytes with no shader dependencies.</returns>
    public PlatformMaterialCookResult Cook(PlatformMaterialCookRequest request) {
        if (request == null) {
            throw new ArgumentNullException(nameof(request));
        } else if (!string.Equals(request.TargetPlatformId, "dc", StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Dreamcast cannot cook materials for target platform '{request.TargetPlatformId}'.");
        } else if (!string.Equals(request.SchemaId, DreamcastMaterialSchemaIds.StandardSchemaId, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Dreamcast does not support material schema '{request.SchemaId}'.");
        } else if (!string.IsNullOrWhiteSpace(request.SelectedGraphicsProfileId)
            && !string.Equals(request.SelectedGraphicsProfileId, DreamcastMaterialSchemaIds.StandardGraphicsProfileId, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Dreamcast material requires graphics profile 'dc-standard', received '{request.SelectedGraphicsProfileId}'.");
        }

        // The authored base color is mandatory; every other field falls back to the
        // schema default declared by the platform definition when a project material
        // carries only the engine's common fields (base-color, casts-shadow). A field
        // that is present but unsupported is still rejected below.
        IReadOnlyDictionary<string, string> fields = request.FieldValues;
        string baseColorValue = Require(fields, DreamcastMaterialSchemaIds.BaseColorFieldId);
        string texturePath = Resolve(fields, DreamcastMaterialSchemaIds.TextureRelativePathFieldId, string.Empty);
        string lightingMode = Resolve(fields, DreamcastMaterialSchemaIds.LightingModeFieldId, DreamcastMaterialSchemaIds.LightingLitValue);
        string transparencyMode = Resolve(fields, DreamcastMaterialSchemaIds.TransparencyFieldId, "opaque");
        string doubleSidedMode = Resolve(fields, DreamcastMaterialSchemaIds.DoubleSidedFieldId, "false");
        string vertexColorMode = Resolve(fields, DreamcastMaterialSchemaIds.VertexColorModeFieldId, "ignore");

        if (!string.IsNullOrWhiteSpace(texturePath)) {
            throw new InvalidOperationException("Dreamcast dc-standard-v1 does not support texture bindings.");
        } else if (!string.Equals(transparencyMode, "opaque", StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException("Dreamcast dc-standard-v1 requires opaque materials.");
        } else if (ParseBoolean(doubleSidedMode, DreamcastMaterialSchemaIds.DoubleSidedFieldId)) {
            throw new InvalidOperationException("Dreamcast dc-standard-v1 requires single-sided materials.");
        } else if (!string.Equals(vertexColorMode, "ignore", StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException("Dreamcast dc-standard-v1 requires vertex colors to be ignored.");
        }

        bool lit;
        if (string.Equals(lightingMode, DreamcastMaterialSchemaIds.LightingLitValue, StringComparison.OrdinalIgnoreCase)) {
            lit = true;
        } else if (string.Equals(lightingMode, DreamcastMaterialSchemaIds.LightingUnlitValue, StringComparison.OrdinalIgnoreCase)) {
            lit = false;
        } else {
            throw new InvalidOperationException($"Dreamcast dc-standard-v1 lighting mode must be 'lit' or 'unlit', received '{lightingMode}'.");
        }

        ParseColor(baseColorValue, out byte red, out byte green, out byte blue, out byte alpha);
        if (alpha != byte.MaxValue) {
            throw new InvalidOperationException("Dreamcast dc-standard-v1 requires an opaque base color (alpha FF).");
        }

        helengine.PlatformMaterialAsset cookedAsset = new() {
            Id = request.MaterialAssetId,
            RendererFamilyId = DreamcastMaterialSchemaIds.StandardGraphicsProfileId,
            TextureRelativePath = string.Empty,
            DoubleSided = false,
            UseVertexColor = false,
            Lit = lit,
            BaseColorR = red,
            BaseColorG = green,
            BaseColorB = blue,
            BaseColorA = alpha
        };

        return new PlatformMaterialCookResult(helengine.files.AssetSerializer.SerializeToBytes(cookedAsset, request.NumericPolicy), []);
    }

    /// <summary>Returns a required nonblank schema field value.</summary>
    /// <param name="fields">Authored field map.</param>
    /// <param name="fieldId">Required field identifier.</param>
    /// <returns>Trimmed field value.</returns>
    static string Require(IReadOnlyDictionary<string, string> fields, string fieldId) {
        string value;
        if (fields == null) {
            throw new ArgumentNullException(nameof(fields));
        } else if (!fields.TryGetValue(fieldId, out value) || string.IsNullOrWhiteSpace(value)) {
            throw new InvalidOperationException($"Dreamcast dc-standard-v1 requires material field '{fieldId}'.");
        }

        return value.Trim();
    }

    /// <summary>Returns an optional schema field, substituting the schema default when it is absent.</summary>
    /// <param name="fields">Authored field map.</param>
    /// <param name="fieldId">Field identifier.</param>
    /// <param name="defaultValue">Schema default used when the field is absent.</param>
    /// <returns>Trimmed authored value, or the default.</returns>
    static string Resolve(IReadOnlyDictionary<string, string> fields, string fieldId, string defaultValue) {
        string value;
        if (fields == null) {
            throw new ArgumentNullException(nameof(fields));
        } else if (!fields.TryGetValue(fieldId, out value) || value == null) {
            return defaultValue;
        }

        return value.Trim();
    }

    /// <summary>Parses a strict authored boolean field.</summary>
    /// <param name="value">Text value to parse.</param>
    /// <param name="fieldId">Field identifier used in diagnostics.</param>
    /// <returns>Parsed boolean.</returns>
    static bool ParseBoolean(string value, string fieldId) {
        if (!bool.TryParse(value, out bool parsed)) {
            throw new InvalidOperationException($"Dreamcast material field '{fieldId}' must be true or false.");
        }

        return parsed;
    }

    /// <summary>Parses an authored hexadecimal RGB or RGBA color.</summary>
    /// <param name="value">Color in #RRGGBB or #RRGGBBAA form.</param>
    /// <param name="red">Parsed red channel.</param>
    /// <param name="green">Parsed green channel.</param>
    /// <param name="blue">Parsed blue channel.</param>
    /// <param name="alpha">Parsed alpha channel.</param>
    static void ParseColor(string value, out byte red, out byte green, out byte blue, out byte alpha) {
        string normalized = value.Trim();
        if (normalized.StartsWith('#')) {
            normalized = normalized[1..];
        }
        if (normalized.Length == 6) {
            normalized += "FF";
        } else if (normalized.Length != 8) {
            throw new InvalidOperationException($"Dreamcast color '{value}' must use #RRGGBB or #RRGGBBAA format.");
        }

        try {
            red = byte.Parse(normalized.AsSpan(0, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            green = byte.Parse(normalized.AsSpan(2, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            blue = byte.Parse(normalized.AsSpan(4, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
            alpha = byte.Parse(normalized.AsSpan(6, 2), NumberStyles.HexNumber, CultureInfo.InvariantCulture);
        } catch (FormatException exception) {
            throw new InvalidOperationException($"Dreamcast color '{value}' contains invalid hexadecimal channels.", exception);
        }
    }
}
