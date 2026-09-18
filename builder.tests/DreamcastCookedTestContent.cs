using System.Security.Cryptography;
using System.Text;
using helengine;
using helengine.baseplatform.Manifest;

namespace helengine.dreamcast.builder.tests;

/// <summary>Builds real cooked scene, model, material and texture bytes with the current engine serializers for builder tests.</summary>
internal sealed class DreamcastCookedTestContent : IDisposable {
    public const string ScenePath = "cooked/scenes/startup.hasset";
    public const string ModelPath = "cooked/imported/models/cube.hasset";
    public const string MaterialPath = "cooked/materials/cube.helmat";
    public const string TexturePath = "textures/icon.hasset";
    public const string CameraTypeId = "helengine.CameraComponent";
    public const string MeshTypeId = "helengine.MeshComponent";

    readonly string RootPath;

    DreamcastCookedTestContent(PlatformBuildManifest manifest, string sourceRoot, string destinationRoot, string rootPath) {
        Manifest = manifest;
        SourceRoot = sourceRoot;
        DestinationRoot = destinationRoot;
        RootPath = rootPath;
    }

    /// <summary>Gets the manifest describing the written cooked package.</summary>
    public PlatformBuildManifest Manifest { get; }

    /// <summary>Gets the cooked package source root on disk.</summary>
    public string SourceRoot { get; }

    /// <summary>Gets the fresh, not-yet-created ROMFS staging root on disk.</summary>
    public string DestinationRoot { get; }

    /// <summary>Builds a valid baseline package plus one declared texture artifact at <see cref="TexturePath"/>.</summary>
    public static DreamcastCookedTestContent CreateWithTexture() {
        string rootPath = Path.Combine(Path.GetTempPath(), "helengine-dc-cooked-content-" + Guid.NewGuid().ToString("N"));
        string sourceRoot = Path.Combine(rootPath, "package-source");
        string destinationRoot = Path.Combine(rootPath, "stage");
        PlatformBuildArtifact[] baseArtifacts = WriteValidPackage(sourceRoot);

        using TextureAsset texture = CreateTexture(3, 2);
        byte[] textureBytes = helengine.files.AssetSerializer.SerializeToBytes(texture);
        WriteFile(sourceRoot, TexturePath, textureBytes);
        PlatformBuildArtifact textureArtifact = new(TexturePath, "texture/icon", Hash(textureBytes), "texture", "baseline");

        PlatformBuildManifest manifest = CreateManifest(baseArtifacts[0], [.. baseArtifacts[1..], textureArtifact]);
        return new DreamcastCookedTestContent(manifest, sourceRoot, destinationRoot, rootPath);
    }

    /// <summary>Creates a solid-colour texture of the given dimensions in Rgba32.</summary>
    public static TextureAsset CreateTexture(int width, int height) {
        byte[] colors = new byte[width * height * 4];
        for (int index = 0; index < width * height; index++) {
            colors[index * 4] = 208;
            colors[(index * 4) + 1] = 32;
            colors[(index * 4) + 2] = 32;
            colors[(index * 4) + 3] = 255;
        }
        return new TextureAsset {
            Id = "textures/icon",
            AuthoringAssetId = CreateAssetId(TexturePath),
            FormerAuthoringAssetIds = Array.Empty<string>(),
            Width = (ushort)width,
            Height = (ushort)height,
            ColorFormat = TextureAssetColorFormat.Rgba32,
            AlphaPrecision = TextureAssetAlphaPrecision.A8,
            Colors = colors,
            PaletteColors = Array.Empty<byte>(),
            IsEngineOwned = false
        };
    }

    /// <summary>Builds the manifest wrapper the Dreamcast validator and stager both need.</summary>
    static PlatformBuildManifest CreateManifest(PlatformBuildArtifact artifact, PlatformBuildArtifact[] additionalArtifacts) {
        PlatformBuildArtifact[] artifacts = [artifact, .. additionalArtifacts ?? []];
        PlatformBuildScene scene = new(
            "startup",
            "Startup",
            "scenes/startup.helen",
            [new PlatformBuildPayloadReference("startup", "scenes/startup.helen")],
            [new KeyValuePair<string, string>(PlatformBuildSceneMetadataKeys.CookedRelativePath, "cooked/scenes/startup.hasset")]);
        return new PlatformBuildManifest(
            1,
            "dreamcast-test",
            "1.0.0",
            "0.4.0",
            "dc",
            "0.2.0",
            "startup",
            [scene],
            [],
            artifacts,
            [],
            [],
            new PlatformContainerWritePlan("dc-romfs-package", []),
            [],
            PlatformBuildRuntimeFeatureManifest.Empty);
    }

    /// <summary>Deletes the temporary directory tree backing this cooked content.</summary>
    public void Dispose() {
        if (Directory.Exists(RootPath)) {
            Directory.Delete(RootPath, recursive: true);
        }
    }

    /// <summary>Writes a valid one-camera/one-mesh package below <paramref name="packageRoot"/> and returns its manifest artifacts.</summary>
    public static PlatformBuildArtifact[] WriteValidPackage(string packageRoot) {
        byte[] modelBytes = helengine.files.AssetSerializer.SerializeToBytes(CreateModel(8, 12));
        byte[] materialBytes = helengine.files.AssetSerializer.SerializeToBytes(CreateMaterial());
        byte[] sceneBytes = SerializeScene(modelBytes, materialBytes, cameraEnabled: true, extraEntities: null);
        WriteFile(packageRoot, ModelPath, modelBytes);
        WriteFile(packageRoot, MaterialPath, materialBytes);
        WriteFile(packageRoot, ScenePath, sceneBytes);
        return [
            new PlatformBuildArtifact(ScenePath, "scene/startup", Hash(sceneBytes), "scene", "baseline"),
            new PlatformBuildArtifact(ModelPath, "model/cube", Hash(modelBytes), "model", "baseline"),
            new PlatformBuildArtifact(MaterialPath, "material/cube", Hash(materialBytes), "material", "baseline")
        ];
    }

    /// <summary>Serializes a startup scene referencing the given model and material bytes.</summary>
    public static byte[] SerializeScene(byte[] modelBytes, byte[] materialBytes, bool cameraEnabled, SceneEntityAsset[] extraEntities) {
        SceneEntityAsset camera = CreateEntity(1, cameraEnabled, [CreateComponent(CameraTypeId)]);
        SceneEntityAsset mesh = CreateEntity(2, true, [CreateComponent(MeshTypeId)]);
        SceneAsset scene = new() {
            Id = "scenes/startup.helen",
            AuthoringAssetId = CreateAssetId("scenes/startup.helen"),
            FormerAuthoringAssetIds = Array.Empty<string>(),
            RootEntities = [camera, mesh, .. extraEntities ?? []],
            AssetReferences = [
                SceneAssetReferenceFactory.CreateFileSystemReference(CreateAssetId(ModelPath), ModelPath, Hash(modelBytes)),
                SceneAssetReferenceFactory.CreateFileSystemReference(CreateAssetId(MaterialPath), MaterialPath, Hash(materialBytes))
            ],
            SceneSettings = new SceneSettingsAsset()
        };
        return helengine.files.AssetSerializer.SerializeToBytes(scene);
    }

    /// <summary>Creates a fan model with the requested position and triangle counts.</summary>
    public static ModelAsset CreateModel(int positionCount, int triangleCount) {
        float3[] positions = new float3[positionCount];
        for (int index = 0; index < positionCount; index++) {
            positions[index] = new float3(index, index * 0.5f, 0f);
        }
        ushort[] indices = new ushort[triangleCount * 3];
        for (int index = 0; index < triangleCount; index++) {
            indices[index * 3] = 0;
            indices[index * 3 + 1] = (ushort)((index + 1) % positionCount);
            indices[index * 3 + 2] = (ushort)((index + 2) % positionCount);
        }
        return new ModelAsset {
            Id = "models/cube.hasset",
            AuthoringAssetId = CreateAssetId(ModelPath),
            FormerAuthoringAssetIds = Array.Empty<string>(),
            Positions = positions,
            Normals = Array.Empty<float3>(),
            TexCoords = Array.Empty<float2>(),
            Indices16 = indices,
            Indices32 = Array.Empty<uint>(),
            Submeshes = [new ModelSubmeshAsset { MaterialSlotName = "Default", IndexStart = 0, IndexCount = indices.Length }]
        };
    }

    /// <summary>Creates the opaque red dc-standard material.</summary>
    public static PlatformMaterialAsset CreateMaterial() {
        return new PlatformMaterialAsset {
            Id = "materials/cube",
            AuthoringAssetId = CreateAssetId(MaterialPath),
            FormerAuthoringAssetIds = Array.Empty<string>(),
            RendererFamilyId = "dc-standard",
            TextureRelativePath = string.Empty,
            DoubleSided = false,
            UseVertexColor = false,
            Lit = false,
            BaseColorR = 208,
            BaseColorG = 32,
            BaseColorB = 32,
            BaseColorA = 255
        };
    }

    /// <summary>Creates one root entity record.</summary>
    public static SceneEntityAsset CreateEntity(uint id, bool enabled, SceneComponentAssetRecord[] components) {
        return new SceneEntityAsset {
            Id = id,
            Name = "entity-" + id,
            IsStatic = true,
            Enabled = enabled,
            LayerMask = ushort.MaxValue,
            LocalPosition = float3.Zero,
            LocalScale = float3.One,
            LocalOrientation = float4.Identity,
            Components = components,
            Children = Array.Empty<SceneEntityAsset>(),
            PlatformExistenceOverrides = Array.Empty<SceneEntityPlatformExistenceOverrideAsset>(),
            PlatformTransformOverrides = Array.Empty<SceneEntityPlatformTransformOverrideAsset>(),
            PlatformComponentOverrides = Array.Empty<SceneEntityPlatformComponentOverrideAsset>()
        };
    }

    /// <summary>Creates one component record with an opaque payload.</summary>
    public static SceneComponentAssetRecord CreateComponent(string typeId) {
        return new SceneComponentAssetRecord {
            ComponentKey = typeId,
            ComponentTypeId = typeId,
            ComponentIndex = 0,
            Payload = [1, 2, 3, 4]
        };
    }

    /// <summary>Writes bytes below a root using a forward-slash relative path.</summary>
    public static void WriteFile(string root, string relativePath, byte[] bytes) {
        string path = Path.Combine(root, relativePath.Replace('/', Path.DirectorySeparatorChar));
        Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllBytes(path, bytes);
    }

    /// <summary>Returns the manifest-style lowercase sha256 content hash.</summary>
    public static string Hash(byte[] bytes) => "sha256:" + Convert.ToHexString(SHA256.HashData(bytes)).ToLowerInvariant();

    /// <summary>Returns a deterministic 32-character asset identity.</summary>
    public static string CreateAssetId(string value) => Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes("dreamcast-test|" + value)), 0, 16).ToLowerInvariant();
}
