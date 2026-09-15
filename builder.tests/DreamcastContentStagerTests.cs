using System.Security.Cryptography;
using System.Text;
using helengine.baseplatform.Manifest;
using helengine.dreamcast.builder;

namespace helengine.dreamcast.builder.tests;

/// <summary>Verifies manifest-owned Dreamcast package staging and validation.</summary>
public sealed class DreamcastContentStagerTests {
    /// <summary>Copies only declared artifacts and gives every output a stable timestamp.</summary>
    [Fact]
    public void StageCopiesDeclaredArtifactsInStableForm() {
        using TemporaryDirectory temporary = new();
        string sourceRoot = Path.Combine(temporary.Path, "package-source");
        string destinationRoot = Path.Combine(temporary.Path, "stage");
        Directory.CreateDirectory(Path.Combine(sourceRoot, "cooked", "scenes"));
        byte[] content = Encoding.UTF8.GetBytes("startup-scene");
        File.WriteAllBytes(Path.Combine(sourceRoot, "cooked", "scenes", "startup.hasset"), content);
        File.WriteAllText(Path.Combine(sourceRoot, "ambient.hasset"), "must not be copied");

        PlatformBuildArtifact artifact = new(
            "cooked/scenes/startup.hasset",
            "scene/startup",
            "sha256:" + Convert.ToHexString(SHA256.HashData(content)).ToLowerInvariant(),
            "scene",
            "baseline");
        PlatformBuildManifest manifest = CreateManifest(artifact);

        IReadOnlyList<string> paths = new DreamcastContentStager().Stage(manifest, sourceRoot, destinationRoot, CancellationToken.None);

        Assert.Equal(new[] { "cooked/scenes/startup.hasset" }, paths);
        Assert.Equal(content, File.ReadAllBytes(Path.Combine(destinationRoot, "cooked", "scenes", "startup.hasset")));
        Assert.False(File.Exists(Path.Combine(destinationRoot, "ambient.hasset")));
        Assert.Equal(new DateTime(2020, 1, 1, 0, 0, 0, DateTimeKind.Utc), File.GetLastWriteTimeUtc(destinationRoot));
    }

    /// <summary>Rejects a declaration that cannot be associated with one exact cooked file.</summary>
    [Fact]
    public void StageRejectsMissingDeclaredArtifactWithoutLoneFileFallback() {
        using TemporaryDirectory temporary = new();
        string sourceRoot = Path.Combine(temporary.Path, "package-source");
        Directory.CreateDirectory(sourceRoot);
        File.WriteAllText(Path.Combine(sourceRoot, "unrelated.hasset"), "wrong scene");
        PlatformBuildArtifact artifact = new("cooked/scenes/missing.hasset", "scene/startup", "sha256:" + new string('0', 64), "scene", "baseline");
        PlatformBuildManifest manifest = CreateManifest(artifact);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() =>
            new DreamcastContentStager().Stage(manifest, sourceRoot, Path.Combine(temporary.Path, "stage"), CancellationToken.None));

        Assert.Contains("missing", exception.Message, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Rejects duplicate normalized paths and required runtime features.</summary>
    [Fact]
    public void ValidatorRejectsCaseCollisionAndUnsupportedFeature() {
        PlatformBuildArtifact first = new("cooked/scenes/startup.hasset", "one", "sha256:" + new string('0', 64), "scene", "baseline");
        PlatformBuildArtifact second = new("COOKED\\SCENES\\STARTUP.HASSET", "two", "sha256:" + new string('0', 64), "scene", "baseline");
        PlatformBuildManifest collision = CreateManifest(first, [second]);
        Assert.Contains("collision", Assert.Throws<InvalidOperationException>(() => new DreamcastSceneCompatibilityValidator().Validate(collision)).Message, StringComparison.OrdinalIgnoreCase);

        PlatformBuildManifest unsupported = CreateManifest(first, runtimeFeatures: new PlatformBuildRuntimeFeatureManifest([
            new PlatformBuildRequiredRuntimeFeature("unknown.feature", default, "test", "test")
        ]));
        Assert.Contains("support", Assert.Throws<InvalidOperationException>(() => new DreamcastSceneCompatibilityValidator().Validate(unsupported)).Message, StringComparison.OrdinalIgnoreCase);
    }

    static PlatformBuildManifest CreateManifest(
        PlatformBuildArtifact artifact,
        PlatformBuildArtifact[] additionalArtifacts = null,
        PlatformBuildRuntimeFeatureManifest runtimeFeatures = null) {
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
            runtimeFeatures ?? PlatformBuildRuntimeFeatureManifest.Empty);
    }

    sealed class TemporaryDirectory : IDisposable {
        public TemporaryDirectory() {
            Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "helengine-dc-stager-" + Guid.NewGuid().ToString("N"));
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
