namespace helengine.dreamcast.builder;

/// <summary>Defines stable field identifiers for the Dreamcast opaque unlit material schema.</summary>
public static class DreamcastMaterialSchemaIds {
    /// <summary>Stable schema identifier consumed by the Dreamcast builder.</summary>
    public const string UnlitSchemaId = "dc-unlit-v1";

    /// <summary>Authored base-color field identifier.</summary>
    public const string BaseColorFieldId = "base-color";

    /// <summary>Authored texture path field identifier.</summary>
    public const string TextureRelativePathFieldId = "texture-relative-path";

    /// <summary>Authored lighting mode field identifier.</summary>
    public const string LightingModeFieldId = "lighting-mode";

    /// <summary>Authored transparency mode field identifier.</summary>
    public const string TransparencyFieldId = "transparency";

    /// <summary>Authored double-sided mode field identifier.</summary>
    public const string DoubleSidedFieldId = "double-sided";

    /// <summary>Authored vertex-color mode field identifier.</summary>
    public const string VertexColorModeFieldId = "vertex-color-mode";
}
