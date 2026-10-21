using helengine.baseplatform.Definitions;
using helengine.baseplatform.Profiles;
using helengine.editor;

namespace helengine.dreamcast.builder;

/// <summary>Creates the editor-facing Dreamcast platform contract for the authored-scene milestone.</summary>
public static class DreamcastPlatformDefinitionFactory {
    const string ForcedDisabledFeatures = "debug_overlay;shaders;physics3d.diagnostics;reflection_like_runtime;host_file_system;text_processing";

    /// <summary>Creates the complete Dreamcast build, graphics, schema, and runtime definition.</summary>
    /// <returns>Validated platform definition.</returns>
    public static PlatformDefinition Create() {
        PlatformSettingDefinition[] buildSettings = [
            new("game-name", "Game Name", PlatformSettingKind.Text, "helengine", true, []),
            new("enable-native-runtime-diagnostics", "Enable Native Runtime Diagnostics", PlatformSettingKind.Boolean, "false", true, [])
        ];

        PlatformCodegenProfileDefinition codegen = new(
            "default",
            "Default",
            "Dreamcast C# to C++ generated runtime profile",
            PlatformCodegenLanguage.Cpp,
            PlatformSerializationEndianness.LittleEndian,
            [
                new(PlatformCodegenSettingIds.ForcedDisabledFeatures, "Forced Disabled Features", PlatformSettingKind.Text, ForcedDisabledFeatures, true, []),
                new("generated-math-convention", "Generated Math Convention", PlatformSettingKind.Text, "native-column-vector", true, []),
                new("pointer-size-bytes", "Pointer Size (Bytes)", PlatformSettingKind.Text, "4", true, []),
                // KallistiOS has no fileno/ftruncate/fstat, so the generated file stream must not
                // take its hosted path. This replaced the older native-file-stream-descriptor-apis
                // option, which the current generator no longer reads.
                new("codegen-use-hosted-file-system", "Use Hosted File System", PlatformSettingKind.Boolean, "false", true, []),
                // Compact messages keep exception strings out of a 16 MiB console's budget, and are
                // the branch of the generated exception helpers that consumes its fallback argument.
                new("codegen-compact-native-exception-messages", "Compact Native Exception Messages", PlatformSettingKind.Boolean, "true", true, []),
                new("type-remaps", "Type Remaps", PlatformSettingKind.Text, "System.Numerics.Vector2=helengine.float2;System.Numerics.Vector3=helengine.float3;System.Numerics.Vector4=helengine.float4;System.Numerics.Quaternion=helengine.float4", true, []),
                new("native-file-system-header", "Native File System Header", PlatformSettingKind.Text, "\"platform/dreamcast/DreamcastContentStreamSource.hpp\"", true, []),
                new("native-file-system-type", "Native File System Type", PlatformSettingKind.Text, "helengine::dreamcast::DreamcastContentStreamSource", true, []),
                new("codegen-use-rtti", "Use Compiler RTTI", PlatformSettingKind.Boolean, "true", true, []),
                new("codegen-use-exceptions", "Use C++ Exceptions", PlatformSettingKind.Boolean, "true", true, [])
            ]);

        PlatformMaterialSchemaDefinition materialSchema = new(
            DreamcastMaterialSchemaIds.StandardSchemaId,
            "Dreamcast Opaque Standard",
            [DreamcastMaterialSchemaIds.StandardGraphicsProfileId],
            [
                new(DreamcastMaterialSchemaIds.BaseColorFieldId, "Base Color", PlatformMaterialFieldKind.Color, "#FFFFFFFF", true, []),
                new(DreamcastMaterialSchemaIds.TextureRelativePathFieldId, "Texture", PlatformMaterialFieldKind.Text, string.Empty, true, []),
                new(DreamcastMaterialSchemaIds.LightingModeFieldId, "Lighting", PlatformMaterialFieldKind.Choice, DreamcastMaterialSchemaIds.LightingLitValue, true, [DreamcastMaterialSchemaIds.LightingLitValue, DreamcastMaterialSchemaIds.LightingUnlitValue]),
                new(DreamcastMaterialSchemaIds.TransparencyFieldId, "Transparency", PlatformMaterialFieldKind.Choice, "opaque", true, ["opaque"]),
                new(DreamcastMaterialSchemaIds.DoubleSidedFieldId, "Double Sided", PlatformMaterialFieldKind.Boolean, "false", true, []),
                new(DreamcastMaterialSchemaIds.VertexColorModeFieldId, "Vertex Color", PlatformMaterialFieldKind.Choice, "ignore", true, ["ignore"])
            ]);

        PlatformBuildProfileDefinition[] profiles = [
            new("debug", "Debug", "Dreamcast development build", DreamcastMaterialSchemaIds.StandardGraphicsProfileId, "default", buildSettings),
            new("release", "Release", "Dreamcast release build", DreamcastMaterialSchemaIds.StandardGraphicsProfileId, "default", buildSettings)
        ];

        // Ordinary textures keep the 64-pixel cap used by sprites. Font atlases
        // need their own larger setting so glyph metrics are not crushed by the
        // icon limit; the editor propagates these settings while transforming
        // the FontAsset's atlas and metrics.
        PlatformAssetCookCapabilityDefinition[] assetCookCapabilities = [
            new PlatformAssetCookCapabilityDefinition(
                "texture",
                "runtime-texture",
                PlatformAssetCookOwnershipKind.BuilderOwned,
                "dc-texture",
                CreateDefaultSerializedTextureCookSettings(),
                CreateTextureFormatCapabilities()),
            new PlatformAssetCookCapabilityDefinition(
                "font-atlas-texture",
                "runtime-texture",
                PlatformAssetCookOwnershipKind.BuilderOwned,
                "dc-texture",
                CreateDefaultSerializedFontAtlasCookSettings(),
                CreateTextureFormatCapabilities())
        ];

        return new PlatformDefinition(
            "dc",
            "Sega Dreamcast",
            profiles,
            [new PlatformGraphicsProfileDefinition(DreamcastMaterialSchemaIds.StandardGraphicsProfileId, "Dreamcast Standard", "PVR opaque renderer with per-vertex lighting", [])],
            [new PlatformAssetRequirementDefinition("scene", "Scene", true, ["helen"])],
            [materialSchema],
            [
                new PlatformComponentSupportRule("helengine.meshcomponent", PlatformComponentSupportKind.Transform, "One static triangle-list mesh is supported.", "Remove unsupported mesh components."),
                new PlatformComponentSupportRule("helengine.cameracomponent", PlatformComponentSupportKind.Transform, "One perspective camera is supported.", "Keep one authored camera."),
                new PlatformComponentSupportRule("helengine.DirectionalLightComponent", PlatformComponentSupportKind.Transform, "Dreamcast v0.3 supports per-vertex directional lighting.", string.Empty),
                new PlatformComponentSupportRule("helengine.AmbientLightComponent", PlatformComponentSupportKind.Transform, "Dreamcast v0.3 supports one summed ambient color.", string.Empty),
                new PlatformComponentSupportRule("helengine.PointLightComponent", PlatformComponentSupportKind.Unsupported, "Dreamcast lighting supports directional and ambient lights only.", "Remove point lights or exclude them on Dreamcast."),
                new PlatformComponentSupportRule("helengine.SpotLightComponent", PlatformComponentSupportKind.Unsupported, "Dreamcast lighting supports directional and ambient lights only.", "Remove spot lights or exclude them on Dreamcast."),
                new PlatformComponentSupportRule("helengine.ViewportComponent", PlatformComponentSupportKind.Transform, "Reference-canvas viewports scale screen-space sprite layouts to the Dreamcast output.", "Keep the viewport component that owns the authored overlay."),
                new PlatformComponentSupportRule("helengine.SpriteComponent", PlatformComponentSupportKind.Transform, "Authored sprites draw in the translucent overlay list.", "Keep sprite components; they render as textured quads."),
                new PlatformComponentSupportRule("helengine.TextComponent", PlatformComponentSupportKind.Transform, "Dreamcast draws text from cooked FontAsset atlases with bounded glyph layout.", "Keep text within the Dreamcast font-atlas and plain-text feature limits."),
                new PlatformComponentSupportRule("helengine.RoundedRectComponent", PlatformComponentSupportKind.Transform, "Dreamcast draws filled and bordered rounded rectangles in the 2D overlay.", "Use finite bounds, radius, border and supported corner flags.")
            ],
            [codegen],
            [new PlatformStorageProfileDefinition("romfs-package", "ROMFS Package", PlatformStorageProfileKind.LooseFiles, "dc-romfs-package", false)],
            [new PlatformMediaProfileDefinition("dreamcast-elf", "Dreamcast ELF", PlatformMediaLayoutKind.InstallTree, false, true)],
            new RuntimeGenerationContract(RuntimeMaterialResolutionMode.CookedPlatformOwned, true, PackagedPathPolicy.ContentRelativeOnly,
                null, false, requiresCookedViewportLayout: true),
            assetCookCapabilities: assetCookCapabilities);
    }

    /// <summary>
    /// Creates the serialized default Dreamcast texture cook settings used when an asset carries no Dreamcast override.
    /// </summary>
    /// <returns>Serialized default Dreamcast texture settings.</returns>
    static string CreateDefaultSerializedTextureCookSettings() {
        return DreamcastTextureCookSettingsSerializer.Serialize(new TextureAssetProcessorSettings {
            MaxResolution = 64,
            ColorFormat = TextureAssetColorFormat.Rgba32,
            AlphaPrecision = TextureAssetAlphaPrecision.A8
        });
    }

    /// <summary>Creates the Dreamcast font-atlas settings while retaining enough glyph resolution for authored text.</summary>
    static string CreateDefaultSerializedFontAtlasCookSettings() {
        return DreamcastTextureCookSettingsSerializer.Serialize(new TextureAssetProcessorSettings {
            MaxResolution = 512,
            ColorFormat = TextureAssetColorFormat.Rgba32,
            AlphaPrecision = TextureAssetAlphaPrecision.A8
        });
    }

    /// <summary>
    /// Creates the Dreamcast texture capabilities for native pixel types, mipmaps, VQ and linear storage.
    /// </summary>
    /// <returns>Dreamcast texture format capabilities.</returns>
    static PlatformTextureFormatCapabilityDefinition CreateTextureFormatCapabilities() {
        return DreamcastTextureFormatCatalog.CreateCapabilities();
    }
}
