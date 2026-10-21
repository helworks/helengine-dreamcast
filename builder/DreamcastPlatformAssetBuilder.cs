using helengine.baseplatform.Builders;
using helengine.baseplatform.Descriptors;
using helengine.baseplatform.Manifest;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.dreamcast.builder;

/// <summary>Publishes the Dreamcast platform contract and delegates native execution behind an injectable seam.</summary>
public sealed class DreamcastPlatformAssetBuilder : IPlatformAssetBuilder, IPlatformLayoutTargetProvider {
    /// <summary>Returns the 640 by 480 framebuffer required and validated by DreamcastVideo.</summary>
    public PlatformLayoutTarget ResolveLayoutTarget(PlatformLayoutTargetRequest request) {
        ArgumentNullException.ThrowIfNull(request);
        return new PlatformLayoutTarget(640, 480, 1d, false, 0d, 1d);
    }
    readonly DreamcastMaterialCooker MaterialCooker = new();
    readonly DreamcastSceneCompatibilityValidator SceneValidator = new();
    readonly DreamcastContentStager ContentStager;
    readonly DreamcastStartupCatalogWriter StartupCatalogWriter = new();
    readonly DreamcastCookedContentValidator CookedContentValidator = new();

    /// <summary>Executes builder-owned platform cook work items declared by the manifest, such as textures.</summary>
    readonly DreamcastPlatformCookWorkItemExecutor CookWorkItemExecutor = new();
    readonly IDreamcastNativeBuildExecutor NativeBuildExecutor;

    /// <summary>Creates a builder using the production native executor.</summary>
    public DreamcastPlatformAssetBuilder()
        : this(new DreamcastNativeBuildExecutor()) {
    }

    /// <summary>Creates a builder using a caller-provided native executor.</summary>
    /// <param name="nativeBuildExecutor">Executor used for the native build phase.</param>
    public DreamcastPlatformAssetBuilder(IDreamcastNativeBuildExecutor nativeBuildExecutor) {
        NativeBuildExecutor = nativeBuildExecutor ?? throw new ArgumentNullException(nameof(nativeBuildExecutor));
        ContentStager = new DreamcastContentStager(SceneValidator);
        Descriptor = new PlatformBuilderDescriptor(
            "helengine.dreamcast.builder",
            "0.2.0",
            "dc",
            // The isolated v0.2 engine reports 0.4.0; the shared main engine reports
            // 1.0.0+<commit>. Discovery enforces this range against the running engine.
            new EngineCompatibilityRange("0.4.0", "999.0.0"),
            new ManifestCompatibilityRange(1, 1),
            ["dc"],
            ["dc"]);
        Definition = DreamcastPlatformDefinitionFactory.Create();
    }

    /// <summary>Gets the discovery descriptor for this builder.</summary>
    public PlatformBuilderDescriptor Descriptor { get; }

    /// <summary>Gets the typed editor-facing platform definition.</summary>
    public helengine.baseplatform.Definitions.PlatformDefinition Definition { get; }

    /// <summary>Cooks one platform-owned material through the Dreamcast schema validator.</summary>
    /// <param name="request">Material request from the build graph.</param>
    /// <returns>Cooked material result.</returns>
    public PlatformMaterialCookResult CookMaterial(PlatformMaterialCookRequest request) {
        return MaterialCooker.Cook(request);
    }

    /// <summary>Runs the initial Dreamcast build boundary and returns structured diagnostics on failure.</summary>
    /// <param name="request">Resolved graph build request.</param>
    /// <param name="progressReporter">Progress callback.</param>
    /// <param name="diagnosticReporter">Diagnostic callback.</param>
    /// <param name="cancellationToken">Cooperative cancellation token.</param>
    /// <returns>Final scene and loose-asset outcomes.</returns>
    public async Task<PlatformBuildReport> BuildAsync(
        PlatformBuildRequest request,
        IPlatformBuildProgressReporter progressReporter,
        IPlatformBuildDiagnosticReporter diagnosticReporter,
        CancellationToken cancellationToken) {
        if (request == null) {
            throw new ArgumentNullException(nameof(request));
        } else if (progressReporter == null) {
            throw new ArgumentNullException(nameof(progressReporter));
        } else if (diagnosticReporter == null) {
            throw new ArgumentNullException(nameof(diagnosticReporter));
        }

        List<PlatformBuildDiagnostic> diagnostics = [];
        string invocationRoot = Path.Combine(request.WorkingRoot,
            string.IsNullOrWhiteSpace(Environment.GetEnvironmentVariable("HELENGINE_DC_BUILD_RUN_ROOT"))
                ? $"dreamcast-build-{Guid.NewGuid():N}" : "dreamcast-build");
        string contentRoot = Path.Combine(invocationRoot, "content");
        string catalogRoot = string.IsNullOrWhiteSpace(request.NativeObjectCacheRoot)
            ? Path.Combine(invocationRoot, "catalog")
            : Path.Combine(Path.GetFullPath(request.NativeObjectCacheRoot), "dreamcast-catalog");
        try {
            cancellationToken.ThrowIfCancellationRequested();
            if (!string.Equals(request.Manifest.PlatformName, "dc", StringComparison.OrdinalIgnoreCase)) {
                throw new InvalidOperationException($"Dreamcast builder received manifest for platform '{request.Manifest.PlatformName}'.");
            }
            Directory.CreateDirectory(request.OutputRoot);
            IReadOnlyList<DreamcastStartupCatalogEntry> catalogEntries = SceneValidator.Validate(request.Manifest);
            string packageSourceRoot = Path.Combine(request.WorkingRoot, "package-source");

            PlatformCookWorkItem[] cookWorkItems = request.Manifest.PlatformCookWorkItems ?? [];
            // The editor leaves builder-owned outputs (currently textures, including font atlases that fell back
            // onto the texture capability) out of the manifest's cooked artifacts, so collect their staged paths
            // here: they still have to reach the ROMFS or the runtime cannot open its sprite textures.
            List<string> builderCookedRelativePaths = new(cookWorkItems.Length);
            for (int workItemIndex = 0; workItemIndex < cookWorkItems.Length; workItemIndex++) {
                cancellationToken.ThrowIfCancellationRequested();
                CookWorkItemExecutor.Execute(cookWorkItems[workItemIndex], packageSourceRoot);
                builderCookedRelativePaths.Add(cookWorkItems[workItemIndex].OutputRelativePath);
                progressReporter.Report(new PlatformBuildProgressUpdate(
                    "Cook Platform Artifacts",
                    cookWorkItems[workItemIndex].OutputLogicalArtifactId,
                    workItemIndex + 1,
                    cookWorkItems.Length,
                    $"Cooked platform-owned artifact '{cookWorkItems[workItemIndex].OutputRelativePath}'."));
            }

            ContentStager.Stage(request.Manifest, packageSourceRoot, contentRoot, builderCookedRelativePaths, cancellationToken);
            // Verify manifest hashes against the editor's original bytes first. Only the
            // declared, staged font copies are transformed for their cooked atlas; package
            // inputs retain their hashes and remain reusable by later builds.
            DreamcastFontAtlasMetricsScaler.RescalePackagedFonts(contentRoot, cookWorkItems);
            DreamcastStartupCatalogEntry startupEntry = catalogEntries.Single(entry => string.Equals(entry.SceneId, request.Manifest.StartupSceneId, StringComparison.Ordinal));
            CookedContentValidator.Validate(contentRoot, startupEntry.CookedRelativePath);
            StartupCatalogWriter.Write(catalogRoot, request.Manifest.StartupSceneId, catalogEntries);
            progressReporter.Report(new PlatformBuildProgressUpdate("Build Native ELF", "dreamcast", 0, 1, "Preparing the Dreamcast native executor."));
            NativeBuildExecutor.Build(request, contentRoot, catalogRoot, cancellationToken);
            progressReporter.Report(new PlatformBuildProgressUpdate("Build Native ELF", "dreamcast", 1, 1, "Built the Dreamcast native runtime."));
        } catch (OperationCanceledException) {
            throw;
        } catch (Exception exception) {
            PlatformBuildDiagnostic diagnostic = new(
                PlatformBuildDiagnosticSeverity.Error,
                "DCBUILD_FAILED",
                exception.Message,
                request.Manifest.StartupSceneId,
                string.Empty,
                "dreamcast-builder");
            diagnostics.Add(diagnostic);
            diagnosticReporter.Report(diagnostic);
        }

        PlatformBuildItemOutcomeKind outcomeKind = diagnostics.Count == 0
            ? PlatformBuildItemOutcomeKind.Succeeded
            : PlatformBuildItemOutcomeKind.Failed;
        return await Task.FromResult(new PlatformBuildReport(
            diagnostics.Count == 0,
            [.. diagnostics],
            request.Manifest.Scenes.Select(scene => new PlatformBuildItemOutcome(scene.SceneId, outcomeKind)).ToArray(),
            request.Manifest.LooseAssets.Select(asset => new PlatformBuildItemOutcome(asset.AssetId, outcomeKind)).ToArray()));
    }
}
