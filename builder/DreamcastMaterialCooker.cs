using System.Globalization;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.dreamcast.builder;

/// <summary>Translates the restricted Dreamcast opaque unlit material schema into a cooked asset.</summary>
public sealed class DreamcastMaterialCooker {
    /// <summary>Cooks one validated opaque, single-sided, unlit material.</summary>
    /// <param name="request">Material schema request supplied by the generic build graph.</param>
    /// <returns>Serialized platform material bytes with no shader dependencies.</returns>
    public PlatformMaterialCookResult Cook(PlatformMaterialCookRequest request) {
        if (request == null) {
            throw new ArgumentNullException(nameof(request));
        } else if (!string.Equals(request.TargetPlatformId, "dc", StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Dreamcast cannot cook materials for target platform '{request.TargetPlatformId}'.");
        } else if (!string.Equals(request.SchemaId, DreamcastMaterialSchemaIds.UnlitSchemaId, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Dreamcast does not support material schema '{request.SchemaId}'.");
        } else if (!string.IsNullOrWhiteSpace(request.SelectedGraphicsProfileId)
            && !string.Equals(request.SelectedGraphicsProfileId, "dc-unlit", StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Dreamcast material requires graphics profile 'dc-unlit', received '{request.SelectedGraphicsProfileId}'.");
        }

        IReadOnlyDictionary<string, string> fields = request.FieldValues;
        string baseColorValue = Require(fields, DreamcastMaterialSchemaIds.BaseColorFieldId);
        string texturePath = RequirePresent(fields, DreamcastMaterialSchemaIds.TextureRelativePathFieldId);
        string lightingMode = Require(fields, DreamcastMaterialSchemaIds.LightingModeFieldId);
        string transparencyMode = Require(fields, DreamcastMaterialSchemaIds.TransparencyFieldId);
        string doubleSidedMode = Require(fields, DreamcastMaterialSchemaIds.DoubleSidedFieldId);
        string vertexColorMode = Require(fields, DreamcastMaterialSchemaIds.VertexColorModeFieldId);

        if (!string.IsNullOrWhiteSpace(texturePath)) {
            throw new InvalidOperationException("Dreamcast dc-unlit-v1 does not support texture bindings.");
        } else if (!string.Equals(lightingMode, "unlit", StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException("Dreamcast dc-unlit-v1 requires unlit lighting.");
        } else if (!string.Equals(transparencyMode, "opaque", StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException("Dreamcast dc-unlit-v1 requires opaque materials.");
        } else if (ParseBoolean(doubleSidedMode, DreamcastMaterialSchemaIds.DoubleSidedFieldId)) {
            throw new InvalidOperationException("Dreamcast dc-unlit-v1 requires single-sided materials.");
        } else if (!string.Equals(vertexColorMode, "ignore", StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException("Dreamcast dc-unlit-v1 requires vertex colors to be ignored.");
        }

        ParseColor(baseColorValue, out byte red, out byte green, out byte blue, out byte alpha);
        if (alpha != byte.MaxValue) {
            throw new InvalidOperationException("Dreamcast dc-unlit-v1 requires an opaque base color (alpha FF).");
        }

        helengine.PlatformMaterialAsset cookedAsset = new() {
            Id = request.MaterialAssetId,
            RendererFamilyId = "dc-unlit",
            TextureRelativePath = string.Empty,
            DoubleSided = false,
            UseVertexColor = false,
            Lit = false,
            BaseColorR = red,
            BaseColorG = green,
            BaseColorB = blue,
            BaseColorA = alpha
        };

        return new PlatformMaterialCookResult(helengine.files.AssetSerializer.SerializeToBytes(cookedAsset), []);
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
            throw new InvalidOperationException($"Dreamcast dc-unlit-v1 requires material field '{fieldId}'.");
        }

        return value.Trim();
    }

    /// <summary>Returns a schema field that must exist while allowing an intentional empty value.</summary>
    /// <param name="fields">Authored field map.</param>
    /// <param name="fieldId">Required field identifier.</param>
    /// <returns>Trimmed field value, including an allowed empty string.</returns>
    static string RequirePresent(IReadOnlyDictionary<string, string> fields, string fieldId) {
        string value;
        if (fields == null) {
            throw new ArgumentNullException(nameof(fields));
        } else if (!fields.TryGetValue(fieldId, out value)) {
            throw new InvalidOperationException($"Dreamcast dc-unlit-v1 requires material field '{fieldId}'.");
        }

        return value?.Trim() ?? string.Empty;
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
