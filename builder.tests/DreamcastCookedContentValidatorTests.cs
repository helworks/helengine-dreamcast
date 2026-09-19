using helengine;
using helengine.dreamcast.builder;

namespace helengine.dreamcast.builder.tests;

/// <summary>Verifies build-time validation of staged cooked scene, model and material data.</summary>
public sealed class DreamcastCookedContentValidatorTests {
    /// <summary>A staged package with one camera, one mesh, a bounded model and an opaque unlit material passes.</summary>
    [Fact]
    public void ValidStagedContentPasses() {
        using TemporaryDirectory temporary = new();
        DreamcastCookedTestContent.WriteValidPackage(temporary.Path);

        new DreamcastCookedContentValidator().Validate(temporary.Path, DreamcastCookedTestContent.ScenePath);
    }

    /// <summary>Two enabled camera components are rejected with an identified cause.</summary>
    [Fact]
    public void RejectsTwoCameras() {
        using TemporaryDirectory temporary = new();
        Stage(temporary.Path, extraEntities: [DreamcastCookedTestContent.CreateEntity(3, true, [DreamcastCookedTestContent.CreateComponent(DreamcastCookedTestContent.CameraTypeId)])]);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Validate(temporary.Path));

        Assert.Contains("camera", exception.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("found 2", exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>An engine component without a Dreamcast adapter that survives platform pruning is rejected by name.</summary>
    [Fact]
    public void RejectsUnsupportedEngineComponent() {
        using TemporaryDirectory temporary = new();
        Stage(temporary.Path, extraEntities: [DreamcastCookedTestContent.CreateEntity(3, true, [DreamcastCookedTestContent.CreateComponent("helengine.SpriteComponent")])]);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Validate(temporary.Path));

        Assert.Contains("helengine.SpriteComponent", exception.Message, StringComparison.Ordinal);
    }

    /// <summary>Project code components run inside generated Core and are accepted.</summary>
    [Fact]
    public void AcceptsProjectCodeComponent() {
        using TemporaryDirectory temporary = new();
        Stage(temporary.Path, extraEntities: [DreamcastCookedTestContent.CreateEntity(3, true, [DreamcastCookedTestContent.CreateComponent("city.rendering.CubeTestSpinComponent, gameplay")])]);

        Validate(temporary.Path);
    }

    /// <summary>A camera on a disabled entity does not count, so the scene has no usable camera.</summary>
    [Fact]
    public void RejectsDisabledCameraEntity() {
        using TemporaryDirectory temporary = new();
        Stage(temporary.Path, cameraEnabled: false);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Validate(temporary.Path));

        Assert.Contains("found 0", exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>A model with no explicit submeshes (the engine's built-in cube form) is accepted as one implicit submesh.</summary>
    [Fact]
    public void AcceptsImplicitSingleSubmesh() {
        using TemporaryDirectory temporary = new();
        ModelAsset model = DreamcastCookedTestContent.CreateModel(8, 12);
        model.Submeshes = Array.Empty<ModelSubmeshAsset>();
        Stage(temporary.Path, model: model);

        Validate(temporary.Path);
    }

    /// <summary>Two explicit submeshes are rejected.</summary>
    [Fact]
    public void RejectsTwoSubmeshes() {
        using TemporaryDirectory temporary = new();
        ModelAsset model = DreamcastCookedTestContent.CreateModel(8, 12);
        model.Submeshes = [
            new ModelSubmeshAsset { MaterialSlotName = "A", IndexStart = 0, IndexCount = 18 },
            new ModelSubmeshAsset { MaterialSlotName = "B", IndexStart = 18, IndexCount = 18 }
        ];
        Stage(temporary.Path, model: model);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Validate(temporary.Path));

        Assert.Contains("at most one submesh", exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Position and triangle limits are enforced against the cooked model.</summary>
    [Theory]
    [InlineData(65, 1, "positions")]
    [InlineData(64, 129, "triangles")]
    public void RejectsOverLimitModel(int positionCount, int triangleCount, string fragment) {
        using TemporaryDirectory temporary = new();
        Stage(temporary.Path, model: DreamcastCookedTestContent.CreateModel(positionCount, triangleCount));

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Validate(temporary.Path));

        Assert.Contains(fragment, exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Textured and non-opaque cooked materials are rejected.</summary>
    [Theory]
    [InlineData("alpha", "opaque")]
    [InlineData("texture", "texture")]
    public void RejectsUnsupportedMaterial(string mutation, string fragment) {
        using TemporaryDirectory temporary = new();
        PlatformMaterialAsset material = DreamcastCookedTestContent.CreateMaterial();
        if (mutation == "alpha") {
            material.BaseColorA = 254;
        } else {
            material.TextureRelativePath = "textures/cube.png";
        }
        Stage(temporary.Path, material: material);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Validate(temporary.Path));

        Assert.Contains(fragment, exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Lit materials are supported and the light components that drive them are allowed.</summary>
    [Fact]
    public void AcceptsLitMaterialAndLightComponents() {
        using TemporaryDirectory temporary = new();
        PlatformMaterialAsset material = DreamcastCookedTestContent.CreateMaterial();
        material.Lit = true;
        Stage(temporary.Path, material: material, extraEntities: [
            DreamcastCookedTestContent.CreateEntity(3, true, [DreamcastCookedTestContent.CreateComponent("helengine.DirectionalLightComponent")]),
            DreamcastCookedTestContent.CreateEntity(4, true, [DreamcastCookedTestContent.CreateComponent("helengine.AmbientLightComponent")])
        ]);

        Validate(temporary.Path);
    }

    /// <summary>A package whose model file is absent is rejected even though the scene still names it.</summary>
    [Fact]
    public void RejectsPackageWithoutModel() {
        using TemporaryDirectory temporary = new();
        DreamcastCookedTestContent.WriteValidPackage(temporary.Path);
        File.Delete(Path.Combine(temporary.Path, DreamcastCookedTestContent.ModelPath.Replace('/', Path.DirectorySeparatorChar)));

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Validate(temporary.Path));

        Assert.Contains("no model", exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>
    /// A staged Dreamcast cooked texture passes. Textures are the platform's own <c>dc-texture-v1</c> payloads,
    /// not serialized engine assets, so the validator has to recognize them by their own magic; handing one to
    /// the engine deserializer would fail every build that cooks a texture.
    /// </summary>
    [Fact]
    public void AcceptsAStagedCookedTexture() {
        using TemporaryDirectory temporary = new();
        DreamcastCookedTestContent.WriteValidPackage(temporary.Path);
        DreamcastCookedTestContent.WriteFile(temporary.Path, "textures/icon.dctex", CookTexture(12, 9));

        Validate(temporary.Path);
    }

    /// <summary>A cooked texture the runtime parser would refuse is rejected at build time, with its path and cause.</summary>
    [Theory]
    [InlineData(4, (ushort)2, "version")]
    [InlineData(6, (ushort)1, "flags")]
    [InlineData(8, (ushort)12, "padded side")]
    [InlineData(10, (ushort)0, "used extent")]
    [InlineData(12, (ushort)64, "used extent")]
    public void RejectsACookedTextureTheRuntimeCouldNotLoad(int fieldOffset, ushort corruptValue, string fragment) {
        using TemporaryDirectory temporary = new();
        DreamcastCookedTestContent.WriteValidPackage(temporary.Path);
        byte[] cooked = CookTexture(12, 9);
        BitConverter.TryWriteBytes(cooked.AsSpan(fieldOffset), corruptValue);
        DreamcastCookedTestContent.WriteFile(temporary.Path, "textures/icon.dctex", cooked);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Validate(temporary.Path));

        Assert.Contains("textures/icon.dctex", exception.Message, StringComparison.Ordinal);
        Assert.Contains(fragment, exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>A cooked texture whose payload length disagrees with its declared padded side is rejected.</summary>
    [Fact]
    public void RejectsACookedTextureWhoseLengthDisagreesWithItsPaddedSide() {
        using TemporaryDirectory temporary = new();
        DreamcastCookedTestContent.WriteValidPackage(temporary.Path);
        byte[] cooked = CookTexture(12, 9);
        DreamcastCookedTestContent.WriteFile(temporary.Path, "textures/icon.dctex", cooked[..^2]);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Validate(temporary.Path));

        Assert.Contains("textures/icon.dctex", exception.Message, StringComparison.Ordinal);
        Assert.Contains("ARGB4444 payload is", exception.Message, StringComparison.Ordinal);
    }

    /// <summary>A truncated cooked texture that cannot even carry a header is rejected rather than read past its end.</summary>
    [Fact]
    public void RejectsACookedTextureShorterThanItsHeader() {
        using TemporaryDirectory temporary = new();
        DreamcastCookedTestContent.WriteValidPackage(temporary.Path);
        DreamcastCookedTestContent.WriteFile(temporary.Path, "textures/icon.dctex", CookTexture(12, 9)[..8]);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Validate(temporary.Path));

        Assert.Contains("shorter than", exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>
    /// A staged engine asset of an unsupported type is rejected with its path. The rejection applies to engine
    /// assets only; a Dreamcast cooked texture is recognized by its own magic before this branch is reached.
    /// </summary>
    [Fact]
    public void RejectsUnsupportedStagedAssetType() {
        using TemporaryDirectory temporary = new();
        DreamcastCookedTestContent.WriteValidPackage(temporary.Path);
        byte[] modelBytes = helengine.files.AssetSerializer.SerializeToBytes(DreamcastCookedTestContent.CreateModel(4, 2));
        byte[] materialBytes = helengine.files.AssetSerializer.SerializeToBytes(DreamcastCookedTestContent.CreateMaterial());
        DreamcastCookedTestContent.WriteFile(temporary.Path, "cooked/scenes/other.hasset", DreamcastCookedTestContent.SerializeScene(modelBytes, materialBytes, true, null));

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Validate(temporary.Path));

        Assert.Contains("unsupported cooked asset type", exception.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("cooked/scenes/other.hasset", exception.Message, StringComparison.Ordinal);
    }

    static void Validate(string root) {
        new DreamcastCookedContentValidator().Validate(root, DreamcastCookedTestContent.ScenePath);
    }

    /// <summary>Cooks a real dc-texture-v1 payload through the production cooker, so the tests pin the shipping format.</summary>
    static byte[] CookTexture(int width, int height) {
        using TextureAsset texture = DreamcastCookedTestContent.CreateTexture(width, height);
        return new DreamcastTextureCooker().Cook(texture);
    }

    static void Stage(
        string root,
        ModelAsset model = null,
        PlatformMaterialAsset material = null,
        bool cameraEnabled = true,
        SceneEntityAsset[] extraEntities = null) {
        byte[] modelBytes = helengine.files.AssetSerializer.SerializeToBytes(model ?? DreamcastCookedTestContent.CreateModel(8, 12));
        byte[] materialBytes = helengine.files.AssetSerializer.SerializeToBytes(material ?? DreamcastCookedTestContent.CreateMaterial());
        DreamcastCookedTestContent.WriteFile(root, DreamcastCookedTestContent.ModelPath, modelBytes);
        DreamcastCookedTestContent.WriteFile(root, DreamcastCookedTestContent.MaterialPath, materialBytes);
        DreamcastCookedTestContent.WriteFile(root, DreamcastCookedTestContent.ScenePath, DreamcastCookedTestContent.SerializeScene(modelBytes, materialBytes, cameraEnabled, extraEntities));
    }

    sealed class TemporaryDirectory : IDisposable {
        public TemporaryDirectory() {
            Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "helengine-dc-content-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Path);
        }

        public string Path { get; }

        public void Dispose() {
            if (Directory.Exists(Path)) {
                Directory.Delete(Path, recursive: true);
            }
        }
    }
}
