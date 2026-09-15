using System.Security.Cryptography;
using System.Text;
using helengine;
using helengine.editor;
using helengine.projectfile;

namespace helengine.dreamcast.fixture;

/// <summary>Writes normal Helengine project, scene, model, and material documents for Dreamcast acceptance variants.</summary>
public sealed class DreamcastAuthoredSceneFixtureWriter {
    /// <summary>Stable fixture variants accepted by the command line tool.</summary>
    public static IReadOnlyList<string> SupportedVariants { get; } = ["baseline", "transform", "color", "camera", "geometry"];

    const string SceneRelativePath = "scenes/startup.helen";
    const string ModelRelativePath = "models/cube.hasset";
    const string MaterialRelativePath = "materials/cube.helmat";
    const string MaterialOverrideSuffix = ".dc.hasset";
    const string CameraTypeId = "helengine.CameraComponent";
    const string MeshTypeId = "helengine.MeshComponent";

    /// <summary>Creates one complete fixture tree for the requested authored variant.</summary>
    /// <param name="outputPath">Directory receiving the project and assets.</param>
    /// <param name="variant">Baseline, transform, color, camera, or geometry.</param>
    public void Write(string outputPath, string variant) {
        if (string.IsNullOrWhiteSpace(outputPath)) {
            throw new ArgumentException("Fixture output path is required.", nameof(outputPath));
        } else if (!SupportedVariants.Contains(variant, StringComparer.OrdinalIgnoreCase)) {
            throw new ArgumentException($"Unsupported fixture variant '{variant}'.", nameof(variant));
        }

        string root = Path.GetFullPath(outputPath);
        Directory.CreateDirectory(Path.Combine(root, "assets", "scenes"));
        Directory.CreateDirectory(Path.Combine(root, "assets", "models"));
        Directory.CreateDirectory(Path.Combine(root, "assets", "materials"));
        WriteProjectFile(root);

        string normalizedVariant = variant.ToLowerInvariant();
        byte[] modelBytes = CreateModelBytes(normalizedVariant);
        string modelPath = WriteAsset(root, ModelRelativePath, modelBytes);
        byte[] materialBytes = CreateMaterialCommonBytes(normalizedVariant);
        string materialPath = WriteAsset(root, MaterialRelativePath, materialBytes);
        WriteMaterialOverride(root, normalizedVariant);

        SceneAssetReference modelReference = CreateReference(ModelRelativePath, modelBytes);
        SceneAssetReference materialReference = CreateReference(MaterialRelativePath, materialBytes);
        byte[] sceneBytes = CreateSceneBytes(normalizedVariant, modelReference, materialReference);
        WriteAsset(root, SceneRelativePath, sceneBytes);
        WriteBuildConfiguration(root);

        Console.WriteLine($"model={modelPath}");
        Console.WriteLine($"material={materialPath}");
        Console.WriteLine($"scene={Path.Combine(root, "assets", SceneRelativePath.Replace('/', Path.DirectorySeparatorChar))}");
    }

    /// <summary>Persists editor build selections through the current build-config service.</summary>
    /// <param name="root">Fixture project root.</param>
    static void WriteBuildConfiguration(string root) {
        EditorBuildConfigService service = new(root);
        EditorBuildConfigDocument document = service.Load(["dc"], "scenes/startup");
        EditorBuildPlatformConfigDocument platform = document.Platforms.Single(item => string.Equals(item.PlatformId, "dc", StringComparison.Ordinal));
        platform.SelectedSceneIds = ["scenes/startup"];
        platform.SelectedBuildProfileId = "release";
        platform.SelectedGraphicsProfileId = "dc-unlit";
        platform.SelectedCodegenProfileId = "default";
        platform.SelectedStorageProfileId = "romfs-package";
        platform.SelectedMediaProfileId = "dreamcast-elf";
        platform.SelectedEnvironmentId = "release";
        platform.DebugBuild = false;
        service.Save(document);
    }

    /// <summary>Writes the canonical project file and explicit Dreamcast startup routing.</summary>
    /// <param name="root">Fixture project root.</param>
    static void WriteProjectFile(string root) {
        DateTime timestamp = new DateTime(2026, 1, 1, 0, 0, 0, DateTimeKind.Utc);
        ProjectFileDocument document = new() {
            Name = "Dreamcast Authored Scene Fixture",
            Version = "0.2.0",
            RequiredEngineVersion = "0.4.0",
            SupportedPlatforms = ["dc"],
            Created = timestamp,
            LastOpened = timestamp,
            Description = "Normal Helengine authored-scene fixture for Dreamcast v0.2.",
            SceneRouting = new ProjectSceneRoutingDocument {
                Platforms = new Dictionary<string, ProjectPlatformSceneRoutingDocument>(StringComparer.Ordinal) {
                    ["dc"] = new ProjectPlatformSceneRoutingDocument {
                        BootSceneId = "scenes/startup"
                    }
                }
            }
        };
        new ProjectFileWriter().WriteAsync(Path.Combine(root, "project.heproj"), document).GetAwaiter().GetResult();
    }

    /// <summary>Writes one byte payload below the project assets directory.</summary>
    /// <param name="root">Fixture project root.</param>
    /// <param name="relativePath">Assets-relative output path.</param>
    /// <param name="bytes">Serialized payload.</param>
    /// <returns>Absolute written path.</returns>
    static string WriteAsset(string root, string relativePath, byte[] bytes) {
        string path = Path.Combine(root, "assets", relativePath.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(path) ?? throw new InvalidOperationException("Asset directory is missing."));
        File.WriteAllBytes(path, bytes);
        return path;
    }

    /// <summary>Creates the canonical file-backed reference for one fixture asset.</summary>
    /// <param name="relativePath">Assets-relative path.</param>
    /// <param name="bytes">Serialized asset bytes.</param>
    /// <returns>Validated scene reference containing identity and content hash.</returns>
    static SceneAssetReference CreateReference(string relativePath, byte[] bytes) {
        byte[] identity = SHA256.HashData(Encoding.UTF8.GetBytes("dreamcast-v02|" + relativePath));
        string assetId = Convert.ToHexString(identity, 0, 16).ToLowerInvariant();
        string contentHash = "sha256:" + Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();
        return global::helengine.SceneAssetReferenceFactory.CreateFileSystemReference(assetId, relativePath, contentHash);
    }

    /// <summary>Serializes the authored scene with one camera and one static mesh.</summary>
    /// <param name="variant">Selected fixture variant.</param>
    /// <param name="modelReference">Canonical model reference.</param>
    /// <param name="materialReference">Canonical material reference.</param>
    /// <returns>Current scene asset bytes.</returns>
    static byte[] CreateSceneBytes(string variant, SceneAssetReference modelReference, SceneAssetReference materialReference) {
        GeneratedSceneComponentAuthoringService authoring = new();
        CameraComponent camera = new() {
            CameraDrawOrder = 0,
            LayerMask = ushort.MaxValue,
            Viewport = new float4(0f, 0f, 1f, 1f),
            NearPlaneDistance = 0.1f,
            FarPlaneDistance = 100f,
            ClearSettings = new CameraClearSettings(true, new float4(0.03f, 0.05f, 0.08f, 1f), true, 1f, false, 0)
        };
        float3 cameraPosition = variant == "camera" ? new float3(1f, 0f, 6f) : new float3(0f, 0f, 6f);
        SceneEntityAsset cameraEntity = CreateEntity("camera", "Authored Camera", cameraPosition, float3.One, [CreateComponent(CameraTypeId, authoring.CreateCameraPayload(camera))]);

        float3 meshPosition = variant == "transform" ? new float3(1f, 0f, 0f) : float3.Zero;
        float4 meshRotation = CreateYawRotation(25f);
        SceneEntityAsset meshEntity = CreateEntity("mesh", "Authored Cube", meshPosition, new float3(1f, 1.25f, 1f), [CreateComponent(MeshTypeId, authoring.CreateMeshPayload(modelReference, materialReference))]);
        meshEntity.LocalOrientation = meshRotation;

        SceneAsset scene = new() {
            Id = "scenes/startup.helen",
            AuthoringAssetId = CreateAssetId("scenes/startup.helen"),
            FormerAuthoringAssetIds = Array.Empty<string>(),
            RootEntities = [cameraEntity, meshEntity],
            AssetReferences = [modelReference, materialReference],
            SceneSettings = new SceneSettingsAsset()
        };
        return helengine.files.AssetSerializer.SerializeToBytes(scene);
    }

    /// <summary>Creates one serialized entity record with a stable identity and enabled state.</summary>
    /// <param name="id">Logical entity identity seed.</param>
    /// <param name="name">Display name.</param>
    /// <param name="position">Local position.</param>
    /// <param name="scale">Local scale.</param>
    /// <param name="components">Serialized component records.</param>
    /// <returns>Entity record.</returns>
    static SceneEntityAsset CreateEntity(string id, string name, float3 position, float3 scale, SceneComponentAssetRecord[] components) {
        return new SceneEntityAsset {
            Id = StableEntityId(id),
            Name = name,
            IsStatic = true,
            Enabled = true,
            LayerMask = ushort.MaxValue,
            LocalPosition = position,
            LocalScale = scale,
            LocalOrientation = float4.Identity,
            Components = components,
            Children = Array.Empty<SceneEntityAsset>(),
            PlatformExistenceOverrides = Array.Empty<SceneEntityPlatformExistenceOverrideAsset>(),
            PlatformTransformOverrides = Array.Empty<SceneEntityPlatformTransformOverrideAsset>(),
            PlatformComponentOverrides = Array.Empty<SceneEntityPlatformComponentOverrideAsset>()
        };
    }

    /// <summary>Creates one component payload record in the current scene format.</summary>
    /// <param name="typeId">Stable component type id.</param>
    /// <param name="payload">Current serializer payload.</param>
    /// <returns>Component record.</returns>
    static SceneComponentAssetRecord CreateComponent(string typeId, byte[] payload) {
        return new SceneComponentAssetRecord {
            ComponentKey = typeId,
            ComponentTypeId = typeId,
            ComponentIndex = 0,
            Payload = payload
        };
    }

    /// <summary>Creates the authored cube model with one complete triangle-list submesh.</summary>
    /// <param name="variant">Selected fixture variant.</param>
    /// <returns>Current model asset bytes.</returns>
    static byte[] CreateModelBytes(string variant) {
        float x = variant == "geometry" ? 0.5f : 1f;
        float3[] positions = [
            new(-x, -1f, -1f), new(x, -1f, -1f), new(x, 1f, -1f), new(-x, 1f, -1f),
            new(-x, -1f, 1f), new(x, -1f, 1f), new(x, 1f, 1f), new(-x, 1f, 1f)
        ];
        ushort[] indices = [
            0, 2, 1, 0, 3, 2,
            4, 5, 6, 4, 6, 7,
            0, 1, 5, 0, 5, 4,
            3, 7, 6, 3, 6, 2,
            1, 2, 6, 1, 6, 5,
            0, 4, 7, 0, 7, 3
        ];
        ModelAsset model = new() {
            Id = "models/cube.hasset",
            AuthoringAssetId = CreateAssetId(ModelRelativePath),
            FormerAuthoringAssetIds = Array.Empty<string>(),
            Positions = positions,
            Normals = Array.Empty<float3>(),
            TexCoords = Array.Empty<float2>(),
            Indices16 = indices,
            Indices32 = Array.Empty<uint>(),
            Submeshes = [new ModelSubmeshAsset { MaterialSlotName = "Default", IndexStart = 0, IndexCount = indices.Length }]
        };
        return helengine.files.AssetSerializer.SerializeToBytes(model);
    }

    /// <summary>Creates the shared authored material settings document.</summary>
    /// <param name="variant">Selected fixture variant.</param>
    /// <returns>Current material settings bytes.</returns>
    static byte[] CreateMaterialCommonBytes(string variant) {
        MaterialAssetCommonSettingsDocument document = new() {
            AuthoringAssetId = CreateAssetId(MaterialRelativePath),
            FormerAuthoringAssetIds = new List<string>(),
            Importer = new AssetImporterSettings {
                ImporterId = "helengine.material",
                AssetId = "materials/cube"
            },
            Processor = new MaterialAssetProcessorSettings {
                SchemaId = "dc-unlit-v1"
            }
        };
        document.Processor.FieldValues["base-color"] = variant == "color" ? "#20D050FF" : "#D02020FF";
        return SerializeCommonMaterial(document);
    }

    /// <summary>Writes the Dreamcast platform material override sidecar.</summary>
    /// <param name="root">Fixture project root.</param>
    /// <param name="variant">Selected fixture variant.</param>
    static void WriteMaterialOverride(string root, string variant) {
        MaterialAssetPlatformOverrideDocument document = new() {
            PlatformId = "dc",
            EnvironmentId = string.Empty,
            Processor = new MaterialAssetProcessorOverrideSettings {
                HasSchemaIdOverride = true,
                SchemaId = "dc-unlit-v1"
            }
        };
        document.Processor.FieldValues["base-color"] = variant == "color" ? "#20D050FF" : "#D02020FF";
        document.Processor.FieldValues["texture-relative-path"] = string.Empty;
        document.Processor.FieldValues["lighting-mode"] = "unlit";
        document.Processor.FieldValues["transparency"] = "opaque";
        document.Processor.FieldValues["double-sided"] = "false";
        document.Processor.FieldValues["vertex-color-mode"] = "ignore";
        using MemoryStream stream = new();
        MaterialAssetPlatformOverrideDocumentBinarySerializer.Serialize(stream, document);
        WriteAsset(root, MaterialRelativePath + MaterialOverrideSuffix, stream.ToArray());
    }

    /// <summary>Serializes one common material settings document with the current editor serializer.</summary>
    /// <param name="document">Material settings document.</param>
    /// <returns>Serialized bytes.</returns>
    static byte[] SerializeCommonMaterial(MaterialAssetCommonSettingsDocument document) {
        using MemoryStream stream = new();
        MaterialAssetCommonSettingsDocumentBinarySerializer.Serialize(stream, document);
        return stream.ToArray();
    }

    /// <summary>Creates a deterministic entity id from a logical seed.</summary>
    /// <param name="value">Logical seed.</param>
    /// <returns>Nonzero stable entity id.</returns>
    static uint StableEntityId(string value) {
        byte[] digest = SHA256.HashData(Encoding.UTF8.GetBytes("dreamcast-v02-entity|" + value));
        uint result = BitConverter.ToUInt32(digest, 0);
        return result == 0 ? 1u : result;
    }

    /// <summary>Creates a deterministic lower-case asset identity.</summary>
    /// <param name="value">Logical asset path.</param>
    /// <returns>32-character lowercase hexadecimal identity.</returns>
    static string CreateAssetId(string value) {
        return Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes("dreamcast-v02-asset|" + value)), 0, 16).ToLowerInvariant();
    }

    /// <summary>Creates a quaternion for the baseline twenty-five degree Y rotation.</summary>
    /// <param name="degrees">Yaw angle in degrees.</param>
    /// <returns>Normalized yaw quaternion.</returns>
    static float4 CreateYawRotation(float degrees) {
        float radians = (float)(degrees * Math.PI / 180d);
        float4.CreateFromAxisAngle(new float3(0f, 1f, 0f), radians, out float4 rotation);
        return rotation;
    }
}
