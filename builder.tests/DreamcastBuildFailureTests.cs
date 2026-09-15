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

    static PlatformBuildRequest CreateRequest(string root, string generatedCoreRoot = null) {
        string workingRoot = Path.Combine(root, "working");
        string packageSource = Path.Combine(workingRoot, "package-source", "cooked", "scenes");
        Directory.CreateDirectory(packageSource);
        byte[] content = Encoding.UTF8.GetBytes("startup-scene");
        File.WriteAllBytes(Path.Combine(packageSource, "startup.hasset"), content);
        string generated = generatedCoreRoot ?? Path.Combine(root, "generated-core");
        if (generatedCoreRoot == null) {
            Directory.CreateDirectory(generated);
        }

        PlatformBuildArtifact artifact = new(
            "cooked/scenes/startup.hasset",
            "scene/startup",
            "sha256:" + Convert.ToHexString(SHA256.HashData(content)).ToLowerInvariant(),
            "scene",
            "baseline");
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
            [artifact],
            [],
            [],
            new PlatformContainerWritePlan("dc-romfs-package", []),
            [],
            PlatformBuildRuntimeFeatureManifest.Empty);
        PlatformCookProfile cookProfile = new(
            "dc-default",
            "Dreamcast",
            new PlatformCookProfileCapabilities("dc-unlit", "none", "none", "helengine", PlatformSerializationEndianness.LittleEndian));
        PlatformBuildTargetVariant variant = new("dc-release", "dc", "dc-unlit", cookProfile.CookProfileId);
        return new PlatformBuildRequest(
            manifest,
            [variant],
            [cookProfile],
            Path.Combine(root, "output"),
            workingRoot,
            "release",
            "dc-unlit",
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
