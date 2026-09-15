using helengine.baseplatform.Definitions;
using helengine.baseplatform.Profiles;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;
using helengine.dreamcast.builder;

namespace helengine.dreamcast.builder.tests;

/// <summary>Verifies the stable Dreamcast platform and cooked-material contract.</summary>
public sealed class DreamcastPlatformDefinitionTests {
    /// <summary>Confirms discovery identity, version, and authored-scene profiles.</summary>
    [Fact]
    public void DescriptorTargetsDreamcast() {
        DreamcastPlatformAssetBuilder builder = new();

        Assert.Equal("dc", builder.Descriptor.TargetPlatformId);
        Assert.Equal("helengine.dreamcast.builder", builder.Descriptor.BuilderId);
        Assert.Equal("0.2.0", builder.Descriptor.BuilderVersion);
        Assert.Equal("Sega Dreamcast", builder.Definition.DisplayName);
        Assert.Equal("dc-unlit", Assert.Single(builder.Definition.GraphicsProfiles).ProfileId);
        Assert.Equal("dc-unlit-v1", Assert.Single(builder.Definition.MaterialSchemas).SchemaId);
        Assert.Equal(new[] { "debug", "release" }, builder.Definition.BuildProfiles.Select(profile => profile.ProfileId));
        PlatformAssetRequirementDefinition sceneRequirement = Assert.Single(builder.Definition.AssetRequirements);
        Assert.Equal("scene", sceneRequirement.RequirementId);
        Assert.Equal(new[] { "helen" }, sceneRequirement.SupportedFileExtensions);
    }

    /// <summary>Confirms the codegen profile publishes the fixed native ABI choices.</summary>
    [Fact]
    public void DefinitionPublishesDreamcastCodegenContract() {
        DreamcastPlatformAssetBuilder builder = new();
        PlatformCodegenProfileDefinition codegen = Assert.Single(builder.Definition.CodegenProfiles);

        Assert.Equal(PlatformSerializationEndianness.LittleEndian, codegen.Endianness);
        Assert.Contains(codegen.Settings, setting => setting.SettingId == "pointer-size-bytes" && setting.DefaultValue == "4");
        Assert.Contains(codegen.Settings, setting => setting.SettingId == "generated-math-convention" && setting.DefaultValue == "native-column-vector");
    }

    /// <summary>Confirms the initial native executor validates its required build request.</summary>
    [Fact]
    public void ProductionNativeExecutorReportsNotReady() {
        DreamcastNativeBuildExecutor executor = new();

        ArgumentNullException exception = Assert.Throws<ArgumentNullException>(() => executor.Build(null, "content", "catalog", CancellationToken.None));

        Assert.Equal("request", exception.ParamName);
    }
}
