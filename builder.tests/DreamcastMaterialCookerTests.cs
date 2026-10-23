using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine.dreamcast.builder;

namespace helengine.dreamcast.builder.tests;

/// <summary>Verifies strict authored material validation and byte serialization.</summary>
public sealed class DreamcastMaterialCookerTests {
    /// <summary>Reads the cooked envelope using each selected target policy.</summary>
    [Theory]
    [InlineData(16)]
    [InlineData(32)]
    [InlineData(64)]
    public void MaterialPayloadLoadsWithTargetNumericPolicy(int bits) {
        var policy = new helengine.NumericPrecisionPolicy(bits, bits);
        var request = new PlatformMaterialCookRequest("material", "materials/sample.hasset", "dc", "debug", "dc-standard",
            DreamcastMaterialSchemaIds.StandardSchemaId, CreateFields("#FFFFFFFF"), numericPolicy: policy);
        var result = new DreamcastMaterialCooker().Cook(request);
        using var stream = new MemoryStream(result.CookedMaterialBytes);
        Assert.IsType<helengine.PlatformMaterialAsset>(helengine.PackagedAssetBinarySerializer.Deserialize(stream, policy));
    }

    /// <summary>Confirms an authored red material round-trips through the current asset serializer.</summary>
    [Fact]
    public void CookMaterial_EmitsAuthoredOpaqueRed() {
        PlatformMaterialCookResult result = new DreamcastPlatformAssetBuilder().CookMaterial(CreateRequest("#C02010FF"));

        helengine.PlatformMaterialAsset cooked = Assert.IsType<helengine.PlatformMaterialAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(result.CookedMaterialBytes));
        Assert.Equal("dc-standard", cooked.RendererFamilyId);
        Assert.False(cooked.DoubleSided);
        Assert.False(cooked.UseVertexColor);
        Assert.True(cooked.Lit);
        Assert.Equal((byte)0xC0, cooked.BaseColorR);
        Assert.Equal((byte)0x20, cooked.BaseColorG);
        Assert.Equal((byte)0x10, cooked.BaseColorB);
        Assert.Equal(byte.MaxValue, cooked.BaseColorA);
    }

    /// <summary>Confirms a second authored color remains distinct from the first.</summary>
    [Fact]
    public void CookMaterial_EmitsAuthoredGreen() {
        PlatformMaterialCookResult result = new DreamcastPlatformAssetBuilder().CookMaterial(CreateRequest("#18D040"));

        helengine.PlatformMaterialAsset cooked = Assert.IsType<helengine.PlatformMaterialAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(result.CookedMaterialBytes));
        Assert.Equal((byte)0x18, cooked.BaseColorR);
        Assert.Equal((byte)0xD0, cooked.BaseColorG);
        Assert.Equal((byte)0x40, cooked.BaseColorB);
        Assert.Equal(byte.MaxValue, cooked.BaseColorA);
    }

    /// <summary>Confirms unsupported material state is rejected with a diagnostic reason.</summary>
    [Theory]
    [InlineData("alpha", "opaque materials")]
    [InlineData("texture-relative-path", "texture bindings")]
    [InlineData("vertex-color-mode", "vertex colors")]
    public void CookMaterial_RejectsUnsupportedState(string mutation, string messageFragment) {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        if (mutation == "alpha") {
            fields[DreamcastMaterialSchemaIds.BaseColorFieldId] = "#FFFFFFFF"[..^2] + "FE";
            messageFragment = "opaque base color";
        } else if (mutation == "texture-relative-path") {
            fields[DreamcastMaterialSchemaIds.TextureRelativePathFieldId] = "textures/cube.png";
        } else {
            fields[DreamcastMaterialSchemaIds.VertexColorModeFieldId] = "multiply";
        }

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => new DreamcastPlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.Contains(messageFragment, exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Confirms a project material carrying only the engine's common fields cooks through the schema defaults.</summary>
    [Fact]
    public void CookMaterial_AppliesSchemaDefaultsForCommonFieldsOnly() {
        Dictionary<string, string> fields = new(StringComparer.OrdinalIgnoreCase) {
            [DreamcastMaterialSchemaIds.BaseColorFieldId] = "#FFFFFFFF",
            ["casts-shadow"] = "false"
        };

        PlatformMaterialCookResult result = new DreamcastPlatformAssetBuilder().CookMaterial(CreateRequest(fields));

        helengine.PlatformMaterialAsset cooked = Assert.IsType<helengine.PlatformMaterialAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(result.CookedMaterialBytes));
        Assert.True(cooked.Lit);
        Assert.False(cooked.DoubleSided);
        Assert.False(cooked.UseVertexColor);
        Assert.Equal(string.Empty, cooked.TextureRelativePath);
        Assert.Equal(byte.MaxValue, cooked.BaseColorA);
    }

    /// <summary>Confirms an explicit unlit material still cooks with the flag cleared.</summary>
    [Fact]
    public void CookMaterial_EmitsExplicitUnlit() {
        Dictionary<string, string> fields = CreateFields("#C02010FF");
        fields[DreamcastMaterialSchemaIds.LightingModeFieldId] = "unlit";

        PlatformMaterialCookResult result = new DreamcastPlatformAssetBuilder().CookMaterial(CreateRequest(fields));

        helengine.PlatformMaterialAsset cooked = Assert.IsType<helengine.PlatformMaterialAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(result.CookedMaterialBytes));
        Assert.False(cooked.Lit);
    }

    /// <summary>Confirms a material with only the engine's common fields defaults to lit.</summary>
    [Fact]
    public void CookMaterial_DefaultsToLit() {
        Dictionary<string, string> fields = new(StringComparer.OrdinalIgnoreCase) {
            [DreamcastMaterialSchemaIds.BaseColorFieldId] = "#FFFFFFFF",
            ["casts-shadow"] = "false"
        };

        PlatformMaterialCookResult result = new DreamcastPlatformAssetBuilder().CookMaterial(CreateRequest(fields));

        helengine.PlatformMaterialAsset cooked = Assert.IsType<helengine.PlatformMaterialAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(result.CookedMaterialBytes));
        Assert.True(cooked.Lit);
    }

    /// <summary>Confirms a lighting mode that is neither lit nor unlit is rejected with a diagnostic reason.</summary>
    [Fact]
    public void CookMaterial_RejectsUnrecognizedLightingMode() {
        Dictionary<string, string> fields = CreateFields("#FFFFFFFF");
        fields[DreamcastMaterialSchemaIds.LightingModeFieldId] = "phong";

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => new DreamcastPlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.Contains("lit", exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Confirms the base color itself is never defaulted.</summary>
    [Fact]
    public void CookMaterial_RequiresBaseColor() {
        Dictionary<string, string> fields = new(StringComparer.OrdinalIgnoreCase) { ["casts-shadow"] = "false" };

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => new DreamcastPlatformAssetBuilder().CookMaterial(CreateRequest(fields)));

        Assert.Contains("base-color", exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Confirms foreign schema ids are rejected before field interpretation.</summary>
    [Fact]
    public void CookMaterial_RejectsForeignSchema() {
        PlatformMaterialCookRequest request = new(
            "material-cube",
            "materials/cube",
            "dc",
            "debug",
            "dc-standard",
            "foreign-schema",
            CreateFields("#FFFFFFFF"), numericPolicy: global::helengine.NumericPrecisionPolicy.Default);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => new DreamcastPlatformAssetBuilder().CookMaterial(request));

        Assert.Contains("foreign-schema", exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Creates a valid material request with all fields required by the schema.</summary>
    /// <param name="baseColor">Authored hexadecimal base color.</param>
    /// <returns>Material cook request.</returns>
    static PlatformMaterialCookRequest CreateRequest(string baseColor) {
        return CreateRequest(CreateFields(baseColor));
    }

    /// <summary>Creates a material request using the supplied authored fields.</summary>
    /// <param name="fields">Authored field values.</param>
    /// <returns>Material cook request.</returns>
    static PlatformMaterialCookRequest CreateRequest(IReadOnlyDictionary<string, string> fields) {
        return new PlatformMaterialCookRequest(
            "material-cube",
            "materials/cube",
            "dc",
            "debug",
            "dc-standard",
            DreamcastMaterialSchemaIds.StandardSchemaId,
            fields, numericPolicy: global::helengine.NumericPrecisionPolicy.Default);
    }

    /// <summary>Creates the strict valid field set used by the focused cooker tests.</summary>
    /// <param name="baseColor">Authored hexadecimal base color.</param>
    /// <returns>Mutable authored fields.</returns>
    static Dictionary<string, string> CreateFields(string baseColor) {
        return new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase) {
            [DreamcastMaterialSchemaIds.BaseColorFieldId] = baseColor,
            [DreamcastMaterialSchemaIds.TextureRelativePathFieldId] = string.Empty,
            [DreamcastMaterialSchemaIds.LightingModeFieldId] = "lit",
            [DreamcastMaterialSchemaIds.TransparencyFieldId] = "opaque",
            [DreamcastMaterialSchemaIds.DoubleSidedFieldId] = "false",
            [DreamcastMaterialSchemaIds.VertexColorModeFieldId] = "ignore"
        };
    }
}
