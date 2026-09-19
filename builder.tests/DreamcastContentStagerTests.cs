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
        PlatformBuildArtifact[] artifacts = DreamcastCookedTestContent.WriteValidPackage(sourceRoot);
        File.WriteAllText(Path.Combine(sourceRoot, "ambient.hasset"), "must not be copied");
        byte[] content = File.ReadAllBytes(Path.Combine(sourceRoot, "cooked", "scenes", "startup.hasset"));
        PlatformBuildManifest manifest = CreateManifest(artifacts[0], artifacts[1..]);

        IReadOnlyList<string> paths = new DreamcastContentStager().Stage(manifest, sourceRoot, destinationRoot, [], CancellationToken.None);

        Assert.Equal(new[] { "cooked/imported/models/cube.hasset", "cooked/materials/cube.helmat", "cooked/scenes/startup.hasset" }, paths);
        Assert.Equal(content, File.ReadAllBytes(Path.Combine(destinationRoot, "cooked", "scenes", "startup.hasset")));
        Assert.False(File.Exists(Path.Combine(destinationRoot, "ambient.hasset")));
        Assert.Equal(new DateTime(2020, 1, 1, 0, 0, 0, DateTimeKind.Utc), File.GetLastWriteTimeUtc(destinationRoot));
    }

    /// <summary>Declared artifacts the runtime cannot load are verified to exist but left out of the ROMFS and its budget.</summary>
    [Fact]
    public void StageSkipsDeclaredArtifactsTheRuntimeCannotLoad() {
        using TemporaryDirectory temporary = new();
        string sourceRoot = Path.Combine(temporary.Path, "package-source");
        string destinationRoot = Path.Combine(temporary.Path, "stage");
        PlatformBuildArtifact[] artifacts = DreamcastCookedTestContent.WriteValidPackage(sourceRoot);
        byte[] texture = new byte[2 * 1024 * 1024];
        new Random(7).NextBytes(texture);
        Directory.CreateDirectory(Path.Combine(sourceRoot, "cooked", "imported"));
        File.WriteAllBytes(Path.Combine(sourceRoot, "cooked", "imported", "dpad.hasset"), texture);
        PlatformBuildArtifact textureArtifact = new(
            "cooked/imported/dpad.hasset",
            "texture/dpad",
            "sha256:" + Convert.ToHexString(SHA256.HashData(texture)).ToLowerInvariant(),
            "texture",
            "baseline");
        PlatformBuildManifest manifest = CreateManifest(artifacts[0], [.. artifacts[1..], textureArtifact]);
        DreamcastContentStager stager = new();

        IReadOnlyList<string> paths = stager.Stage(manifest, sourceRoot, destinationRoot, [], CancellationToken.None);

        Assert.DoesNotContain("cooked/imported/dpad.hasset", paths);
        Assert.Equal(new[] { "cooked/imported/dpad.hasset" }, stager.LastSkippedRelativePaths);
        Assert.False(File.Exists(Path.Combine(destinationRoot, "cooked", "imported", "dpad.hasset")));
        Assert.Equal(3, paths.Count);
    }

    /// <summary>
    /// Confirms a declared artifact that deserializes as a generic engine texture is skipped rather than staged.
    /// The Dreamcast runtime opens textures only in the platform's own twiddled <c>dc-texture-v1</c> form, which
    /// the cook work-item executor writes and which reaches the ROMFS through the builder-cooked path instead.
    /// The builder-owned "texture" capability means real builds never declare a texture this way at all, so this
    /// path is dead today; staging it verbatim would nonetheless put bytes the runtime rejects at load into a
    /// resident ROMFS, so the dead path must fail safe.
    /// </summary>
    [Fact]
    public void Stage_SkipsADeclaredEngineFormatTextureRatherThanStagingBytesTheRuntimeRejects() {
        using DreamcastCookedTestContent content = DreamcastCookedTestContent.CreateWithTexture();

        DreamcastContentStager stager = new();
        IReadOnlyList<string> stagedPaths = stager.Stage(
            content.Manifest, content.SourceRoot, content.DestinationRoot, [], CancellationToken.None);

        Assert.DoesNotContain("textures/icon.hasset", stagedPaths);
        Assert.Contains("textures/icon.hasset", stager.LastSkippedRelativePaths);
        Assert.False(File.Exists(Path.Combine(content.DestinationRoot, "textures", "icon.hasset")));
    }

    /// <summary>
    /// Confirms the builder's own cook-work-item outputs (currently textures, already cooked into the Dreamcast
    /// twiddled <c>dc-texture-v1</c> form before staging runs) are embedded into the ROMFS by copying them
    /// verbatim, counted toward the payload budget, even though the editor leaves them out of
    /// <c>manifest.CookedArtifacts</c> entirely.
    /// </summary>
    [Fact]
    public void Stage_EmbedsBuilderCookedRelativePathsVerbatim() {
        using TemporaryDirectory temporary = new();
        string sourceRoot = Path.Combine(temporary.Path, "package-source");
        string destinationRoot = Path.Combine(temporary.Path, "stage");
        PlatformBuildArtifact[] artifacts = DreamcastCookedTestContent.WriteValidPackage(sourceRoot);
        byte[] cookedTextureBytes = new byte[128];
        new Random(11).NextBytes(cookedTextureBytes);
        DreamcastCookedTestContent.WriteFile(sourceRoot, "textures/icon.dctex", cookedTextureBytes);
        PlatformBuildManifest manifest = CreateManifest(artifacts[0], artifacts[1..]);
        DreamcastContentStager stager = new();

        IReadOnlyList<string> paths = stager.Stage(manifest, sourceRoot, destinationRoot, ["textures/icon.dctex"], CancellationToken.None);

        Assert.Contains("textures/icon.dctex", paths);
        Assert.Empty(stager.LastSkippedRelativePaths);
        string staged = Path.Combine(destinationRoot, "textures", "icon.dctex");
        Assert.True(File.Exists(staged));
        Assert.Equal(cookedTextureBytes, File.ReadAllBytes(staged));
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
            new DreamcastContentStager().Stage(manifest, sourceRoot, Path.Combine(temporary.Path, "stage"), [], CancellationToken.None));

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
