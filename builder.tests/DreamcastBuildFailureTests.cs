using System.Security.Cryptography;
using System.Text;
using helengine.baseplatform.Builders;
using helengine.baseplatform.Definitions;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Profiles;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Targets;
using helengine.dreamcast.builder;

namespace helengine.dreamcast.builder.tests;

/// <summary>Verifies that native failures, cancellation and executor preconditions surface explicitly.</summary>
public sealed class DreamcastBuildFailureTests {
    static readonly object EnvironmentLock = new();

    /// <summary>Stages content and catalog before the executor runs and reports its failure as a diagnostic.</summary>
    [Fact]
    public async Task BuildAsyncReportsExecutorFailureAfterStaging() {
        using TemporaryDirectory temporary = new();
        RecordingExecutor executor = new(new InvalidOperationException("native compile exploded"));
        DreamcastPlatformAssetBuilder builder = new(executor);
        PlatformBuildRequest request = CreateRequest(temporary.Path);
        RecordingReporter reporter = new();

        PlatformBuildReport report = await builder.BuildAsync(request, reporter, reporter, CancellationToken.None);

        Assert.False(report.Succeeded);
        PlatformBuildDiagnostic diagnostic = Assert.Single(report.Diagnostics);
        Assert.Equal("DCBUILD_FAILED", diagnostic.Code);
        Assert.Contains("native compile exploded", diagnostic.Message, StringComparison.Ordinal);
        Assert.Equal(PlatformBuildItemOutcomeKind.Failed, Assert.Single(report.SceneOutcomes).OutcomeKind);
        Assert.Single(reporter.Diagnostics);
        Assert.True(File.Exists(Path.Combine(executor.ContentRoot, "cooked", "scenes", "startup.hasset")));
        Assert.True(File.Exists(Path.Combine(executor.CatalogRoot, "dc_runtime_scene_catalog.cpp")));
        Assert.StartsWith(Path.Combine(request.WorkingRoot, "dreamcast-build-"), executor.ContentRoot, StringComparison.OrdinalIgnoreCase);
    }

    /// <summary>Propagates cancellation instead of converting it into a failed report.</summary>
    [Fact]
    public async Task BuildAsyncPropagatesCancellation() {
        using TemporaryDirectory temporary = new();
        RecordingExecutor executor = new(null);
        DreamcastPlatformAssetBuilder builder = new(executor);
        PlatformBuildRequest request = CreateRequest(temporary.Path);
        using CancellationTokenSource cancellation = new();
        cancellation.Cancel();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            builder.BuildAsync(request, new RecordingReporter(), new RecordingReporter(), cancellation.Token));

        Assert.False(executor.Invoked);
    }

    /// <summary>Verifies original font hashes before scaling only the staged runtime metrics.</summary>
    [Fact]
    public async Task BuildAsyncScalesStagedFontsWithoutChangingManifestInputs() {
        using TemporaryDirectory temporary = new();
        RecordingExecutor executor = new(null);
        DreamcastPlatformAssetBuilder builder = new(executor);
        PlatformBuildRequest request = CreateRequest(temporary.Path, includeFont: true);
        string sourceFontPath = Path.Combine(request.WorkingRoot, "package-source", "fonts", "sample.hefont");
        byte[] originalFont = File.ReadAllBytes(sourceFontPath);
        RecordingReporter reporter = new();

        for (int build = 0; build < 2; build++) {
            if (build == 1) {
                // An undeclared file must not participate in the runtime font transformation.
                File.WriteAllText(Path.Combine(request.WorkingRoot, "package-source", "unlisted.hefont"), "invalid font");
            }
            PlatformBuildReport report = await builder.BuildAsync(request, reporter, reporter, CancellationToken.None);

            Assert.True(report.Succeeded, string.Join("\n", report.Diagnostics.Select(value => value.Message)));
            Assert.Equal(originalFont, File.ReadAllBytes(sourceFontPath));
            string stagedFontPath = Path.Combine(executor.ContentRoot, "fonts", "sample.hefont");
            using FileStream input = File.OpenRead(stagedFontPath);
            using helengine.FontAsset font = helengine.files.FontAssetBinarySerializer.Deserialize(input);
            Assert.Equal(8, font.AtlasWidth);
            Assert.Equal(4, font.AtlasHeight);
            Assert.Equal(10f, font.LineHeight);
            Assert.Equal(6f, font.Characters['A'].AdvanceWidth);
            Assert.Equal(new DateTime(2020, 1, 1, 0, 0, 0, DateTimeKind.Utc), File.GetLastWriteTimeUtc(stagedFontPath));
            Assert.Equal(new DateTime(2020, 1, 1, 0, 0, 0, DateTimeKind.Utc), Directory.GetLastWriteTimeUtc(Path.GetDirectoryName(stagedFontPath)!));
        }
    }

    /// <summary>Requires the isolated repository root and its build script before any native process starts.</summary>
    [Fact]
    public void ProductionExecutorRequiresRepositoryRoot() {
        using TemporaryDirectory temporary = new();
        PlatformBuildRequest request = CreateRequest(temporary.Path);
        lock (EnvironmentLock) {
            string previous = Environment.GetEnvironmentVariable("HELENGINE_DC_REPOSITORY_ROOT");
            try {
                Environment.SetEnvironmentVariable("HELENGINE_DC_REPOSITORY_ROOT", null);
                InvalidOperationException missing = Assert.Throws<InvalidOperationException>(() =>
                    new DreamcastNativeBuildExecutor().Build(request, temporary.Path, temporary.Path, CancellationToken.None));
                Assert.Contains("HELENGINE_DC_REPOSITORY_ROOT", missing.Message, StringComparison.Ordinal);

                Environment.SetEnvironmentVariable("HELENGINE_DC_REPOSITORY_ROOT", temporary.Path);
                FileNotFoundException absent = Assert.Throws<FileNotFoundException>(() =>
                    new DreamcastNativeBuildExecutor().Build(request, temporary.Path, temporary.Path, CancellationToken.None));
                Assert.Contains("build.ps1", absent.FileName, StringComparison.OrdinalIgnoreCase);
            } finally {
                Environment.SetEnvironmentVariable("HELENGINE_DC_REPOSITORY_ROOT", previous);
            }
        }
    }

    /// <summary>Rejects a request without a generated Core root before consulting the environment.</summary>
    [Fact]
    public void ProductionExecutorRequiresGeneratedCoreRoot() {
        using TemporaryDirectory temporary = new();
        PlatformBuildRequest request = CreateRequest(temporary.Path, generatedCoreRoot: string.Empty);

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() =>
            new DreamcastNativeBuildExecutor().Build(request, temporary.Path, temporary.Path, CancellationToken.None));

        Assert.Contains("GeneratedCoreCppRootPath", exception.Message, StringComparison.Ordinal);
    }

    static PlatformBuildRequest CreateRequest(string root, string generatedCoreRoot = null, bool includeFont = false) {
        string workingRoot = Path.Combine(root, "working");
        string packageSource = Path.Combine(workingRoot, "package-source");
        Directory.CreateDirectory(packageSource);
        PlatformBuildArtifact[] artifacts = DreamcastCookedTestContent.WriteValidPackage(packageSource);
        PlatformCookWorkItem[] workItems = [];
        if (includeFont) {
            using helengine.FontAsset font = new(new helengine.FontInfo("fixture", 18, 8f), null,
                new Dictionary<char, helengine.FontChar> {
                    ['A'] = new(new helengine.float4(0f, 0f, 0.25f, 0.5f), 6f, 12f, 4f, 8f)
                }, 20f, 16, 8) {
                CookedAtlasTextureRelativePath = "fonts/sample.dctex"
            };
            using MemoryStream fontStream = new();
            helengine.files.FontAssetBinarySerializer.Serialize(fontStream, font);
            byte[] fontBytes = fontStream.ToArray();
            DreamcastCookedTestContent.WriteFile(packageSource, "fonts/sample.hefont", fontBytes);
            artifacts = [.. artifacts, new("fonts/sample.hefont", "font/sample", DreamcastCookedTestContent.Hash(fontBytes), "font", "baseline")];
            string atlasSource = Path.Combine(root, "atlas.hasset");
            using helengine.TextureAsset texture = DreamcastCookedTestContent.CreateTexture(16, 8);
            File.WriteAllBytes(atlasSource, helengine.files.AssetSerializer.SerializeToBytes(texture));
            workItems = [new("font-atlas", atlasSource, "font-atlas-texture", "dc", "runtime-texture",
                "fonts/sample.dctex", "font-atlas", "source-hash", "settings-hash", "{\"maxResolution\":8,\"colorFormat\":\"Rgba32\",\"alphaPrecision\":\"A8\"}", [])];
        }
        string generated = generatedCoreRoot ?? Path.Combine(root, "generated-core");
        if (generatedCoreRoot == null) {
            Directory.CreateDirectory(generated);
        }

        PlatformBuildScene scene = new(
            "startup",
            "Startup",
            "scenes/startup.helen",
            [new PlatformBuildPayloadReference("startup", "scenes/startup.helen")],
            [new KeyValuePair<string, string>(PlatformBuildSceneMetadataKeys.CookedRelativePath, "cooked/scenes/startup.hasset")]);
        PlatformBuildManifest manifest = new(
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
            workItems,
            PlatformBuildRuntimeFeatureManifest.Empty);
        PlatformCookProfile cookProfile = new(
            "dc-default",
            "Dreamcast",
            new PlatformCookProfileCapabilities("dc-standard", "none", "none", "helengine", PlatformSerializationEndianness.LittleEndian));
        PlatformBuildTargetVariant variant = new("dc-release", "dc", "dc-standard", cookProfile.CookProfileId);
        return new PlatformBuildRequest(
            manifest,
            [variant],
            [cookProfile],
            Path.Combine(root, "output"),
            workingRoot,
            "release",
            "dc-standard",
            "default",
            null,
            null,
            null,
            generated);
    }

    sealed class RecordingExecutor : IDreamcastNativeBuildExecutor {
        readonly Exception Failure;

        public RecordingExecutor(Exception failure) {
            Failure = failure;
        }

        public bool Invoked { get; private set; }

        public string ContentRoot { get; private set; } = string.Empty;

        public string CatalogRoot { get; private set; } = string.Empty;

        public void Build(PlatformBuildRequest request, string contentRoot, string catalogRoot, CancellationToken cancellationToken) {
            Invoked = true;
            ContentRoot = contentRoot;
            CatalogRoot = catalogRoot;
            if (Failure != null) {
                throw Failure;
            }
        }
    }

    sealed class RecordingReporter : IPlatformBuildProgressReporter, IPlatformBuildDiagnosticReporter {
        public List<PlatformBuildDiagnostic> Diagnostics { get; } = [];

        public List<PlatformBuildProgressUpdate> Updates { get; } = [];

        public void Report(PlatformBuildProgressUpdate update) {
            Updates.Add(update);
        }

        public void Report(PlatformBuildDiagnostic diagnostic) {
            Diagnostics.Add(diagnostic);
        }
    }

    sealed class TemporaryDirectory : IDisposable {
        public TemporaryDirectory() {
            Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "helengine-dc-failure-" + Guid.NewGuid().ToString("N"));
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
