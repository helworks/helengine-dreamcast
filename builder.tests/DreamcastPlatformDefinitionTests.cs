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
        Assert.Equal("0.4.0", builder.Descriptor.SupportedEngineVersions.MinimumVersion);
        Assert.Equal("999.0.0", builder.Descriptor.SupportedEngineVersions.MaximumVersion);
        Assert.Equal("Sega Dreamcast", builder.Definition.DisplayName);
        Assert.Equal("dc-standard", Assert.Single(builder.Definition.GraphicsProfiles).ProfileId);
        Assert.Equal("dc-standard-v1", Assert.Single(builder.Definition.MaterialSchemas).SchemaId);
        Assert.Equal(new[] { "debug", "release" }, builder.Definition.BuildProfiles.Select(profile => profile.ProfileId));
        PlatformAssetRequirementDefinition sceneRequirement = Assert.Single(builder.Definition.AssetRequirements);
        Assert.Equal("scene", sceneRequirement.RequirementId);
        Assert.Equal(new[] { "helen" }, sceneRequirement.SupportedFileExtensions);
    }

    /// <summary>Confirms directional and ambient lights are transformed through while point and spot lights are rejected.</summary>
    [Fact]
    public void DefinitionSupportsDirectionalAndAmbientLightsAndRejectsPointAndSpot() {
        DreamcastPlatformAssetBuilder builder = new();

        PlatformComponentSupportRule directional = Assert.Single(builder.Definition.ComponentSupportRules, rule => rule.ComponentTypeId == "helengine.DirectionalLightComponent");
        PlatformComponentSupportRule ambient = Assert.Single(builder.Definition.ComponentSupportRules, rule => rule.ComponentTypeId == "helengine.AmbientLightComponent");
        PlatformComponentSupportRule point = Assert.Single(builder.Definition.ComponentSupportRules, rule => rule.ComponentTypeId == "helengine.PointLightComponent");
        PlatformComponentSupportRule spot = Assert.Single(builder.Definition.ComponentSupportRules, rule => rule.ComponentTypeId == "helengine.SpotLightComponent");
        Assert.Equal(PlatformComponentSupportKind.Transform, directional.SupportKind);
        Assert.Equal(PlatformComponentSupportKind.Transform, ambient.SupportKind);
        Assert.Equal(PlatformComponentSupportKind.Unsupported, point.SupportKind);
        Assert.Equal(PlatformComponentSupportKind.Unsupported, spot.SupportKind);
        Assert.DoesNotContain(builder.Definition.ComponentSupportRules, rule => rule.ComponentTypeId == "helengine.lightcomponent");
    }

    /// <summary>Confirms the codegen profile publishes the fixed native ABI choices.</summary>
    [Fact]
    public void DefinitionPublishesDreamcastCodegenContract() {
        DreamcastPlatformAssetBuilder builder = new();
        PlatformCodegenProfileDefinition codegen = Assert.Single(builder.Definition.CodegenProfiles);

        Assert.Equal(PlatformSerializationEndianness.LittleEndian, codegen.Endianness);
        Assert.Contains(codegen.Settings, setting => setting.SettingId == "pointer-size-bytes" && setting.DefaultValue == "4");
        Assert.Contains(codegen.Settings, setting => setting.SettingId == "generated-math-convention" && setting.DefaultValue == "native-column-vector");
        // KallistiOS lacks the descriptor APIs the hosted file stream path calls.
        Assert.Contains(codegen.Settings, setting => setting.SettingId == "codegen-use-hosted-file-system" && setting.DefaultValue == "false");
        Assert.Contains(codegen.Settings, setting => setting.SettingId == "codegen-compact-native-exception-messages" && setting.DefaultValue == "true");
        // The generator stopped reading this name; keeping it would be dead configuration.
        Assert.DoesNotContain(codegen.Settings, setting => setting.SettingId == "native-file-stream-descriptor-apis");
    }

    /// <summary>Confirms the initial native executor validates its required build request.</summary>
    [Fact]
    public void ProductionNativeExecutorReportsNotReady() {
        DreamcastNativeBuildExecutor executor = new();

        ArgumentNullException exception = Assert.Throws<ArgumentNullException>(() => executor.Build(null, "content", "catalog", CancellationToken.None));

        Assert.Equal("request", exception.ParamName);
    }
}
