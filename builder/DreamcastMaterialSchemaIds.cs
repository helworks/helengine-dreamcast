namespace helengine.dreamcast.builder;

/// <summary>Defines stable field identifiers for the Dreamcast opaque material schema.</summary>
public static class DreamcastMaterialSchemaIds {
    /// <summary>Stable Dreamcast material schema id.</summary>
    public const string StandardSchemaId = "dc-standard-v1";

    /// <summary>Stable Dreamcast graphics profile and cooked renderer family id.</summary>
    public const string StandardGraphicsProfileId = "dc-standard";

    /// <summary>Authored lighting choice that enables per-vertex lighting.</summary>
    public const string LightingLitValue = "lit";

    /// <summary>Authored lighting choice that keeps the flat base color.</summary>
    public const string LightingUnlitValue = "unlit";

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
