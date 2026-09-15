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

    /// <summary>A camera on a disabled entity does not count, so the scene has no usable camera.</summary>
    [Fact]
    public void RejectsDisabledCameraEntity() {
        using TemporaryDirectory temporary = new();
        Stage(temporary.Path, cameraEnabled: false);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Validate(temporary.Path));

        Assert.Contains("found 0", exception.Message, StringComparison.OrdinalIgnoreCase);
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

    /// <summary>Lit, textured and non-opaque cooked materials are rejected.</summary>
    [Theory]
    [InlineData("lit", "unlit")]
    [InlineData("alpha", "opaque")]
    [InlineData("texture", "texture")]
    public void RejectsUnsupportedMaterial(string mutation, string fragment) {
        using TemporaryDirectory temporary = new();
        PlatformMaterialAsset material = DreamcastCookedTestContent.CreateMaterial();
        if (mutation == "lit") {
            material.Lit = true;
        } else if (mutation == "alpha") {
            material.BaseColorA = 254;
        } else {
            material.TextureRelativePath = "textures/cube.png";
        }
        Stage(temporary.Path, material: material);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Validate(temporary.Path));

        Assert.Contains(fragment, exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>A scene reference whose cooked file is absent from the staged package is rejected.</summary>
    [Fact]
    public void RejectsMissingReferencedModel() {
        using TemporaryDirectory temporary = new();
        DreamcastCookedTestContent.WriteValidPackage(temporary.Path);
        File.Delete(Path.Combine(temporary.Path, DreamcastCookedTestContent.ModelPath.Replace('/', Path.DirectorySeparatorChar)));

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() => Validate(temporary.Path));

        Assert.Contains("missing", exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    static void Validate(string root) {
        new DreamcastCookedContentValidator().Validate(root, DreamcastCookedTestContent.ScenePath);
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
