using helengine.baseplatform.Definitions;
using helengine.baseplatform.Profiles;

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
                new("native-file-stream-descriptor-apis", "Native File Stream Descriptor APIs", PlatformSettingKind.Boolean, "false", true, []),
                new("type-remaps", "Type Remaps", PlatformSettingKind.Text, "System.Numerics.Vector2=helengine.float2;System.Numerics.Vector3=helengine.float3;System.Numerics.Vector4=helengine.float4;System.Numerics.Quaternion=helengine.float4", true, []),
                new("native-file-system-header", "Native File System Header", PlatformSettingKind.Text, "\"platform/dreamcast/DreamcastContentStreamSource.hpp\"", true, []),
                new("native-file-system-type", "Native File System Type", PlatformSettingKind.Text, "helengine::dreamcast::DreamcastContentStreamSource", true, [])
            ]);

        PlatformMaterialSchemaDefinition materialSchema = new(
            DreamcastMaterialSchemaIds.UnlitSchemaId,
            "Dreamcast Opaque Unlit",
            ["dc-unlit"],
            [
                new(DreamcastMaterialSchemaIds.BaseColorFieldId, "Base Color", PlatformMaterialFieldKind.Color, "#FFFFFFFF", true, []),
                new(DreamcastMaterialSchemaIds.TextureRelativePathFieldId, "Texture", PlatformMaterialFieldKind.Text, string.Empty, true, []),
                new(DreamcastMaterialSchemaIds.LightingModeFieldId, "Lighting", PlatformMaterialFieldKind.Choice, "unlit", true, ["unlit"]),
                new(DreamcastMaterialSchemaIds.TransparencyFieldId, "Transparency", PlatformMaterialFieldKind.Choice, "opaque", true, ["opaque"]),
                new(DreamcastMaterialSchemaIds.DoubleSidedFieldId, "Double Sided", PlatformMaterialFieldKind.Boolean, "false", true, []),
                new(DreamcastMaterialSchemaIds.VertexColorModeFieldId, "Vertex Color", PlatformMaterialFieldKind.Choice, "ignore", true, ["ignore"])
            ]);

        PlatformBuildProfileDefinition[] profiles = [
            new("debug", "Debug", "Dreamcast development build", "dc-unlit", "default", buildSettings),
            new("release", "Release", "Dreamcast release build", "dc-unlit", "default", buildSettings)
        ];

        return new PlatformDefinition(
            "dc",
            "Sega Dreamcast",
            profiles,
            [new PlatformGraphicsProfileDefinition("dc-unlit", "Dreamcast Unlit", "PVR opaque unlit renderer", [])],
            [new PlatformAssetRequirementDefinition("scene", "Scene", true, ["helen"])],
            [materialSchema],
            [
                new PlatformComponentSupportRule("helengine.meshcomponent", PlatformComponentSupportKind.Transform, "One static triangle-list mesh is supported.", "Remove unsupported mesh components."),
                new PlatformComponentSupportRule("helengine.cameracomponent", PlatformComponentSupportKind.Transform, "One perspective camera is supported.", "Keep one authored camera."),
                new PlatformComponentSupportRule("helengine.lightcomponent", PlatformComponentSupportKind.Unsupported, "Dreamcast v0.2 uses explicit unlit rendering.", "Remove light components.")
            ],
            [codegen],
            [new PlatformStorageProfileDefinition("romfs-package", "ROMFS Package", PlatformStorageProfileKind.LooseFiles, "dc-romfs-package", false)],
            [new PlatformMediaProfileDefinition("dreamcast-elf", "Dreamcast ELF", PlatformMediaLayoutKind.InstallTree, false, true)],
            new RuntimeGenerationContract(RuntimeMaterialResolutionMode.CookedPlatformOwned, true, PackagedPathPolicy.ContentRelativeOnly));
    }
}
