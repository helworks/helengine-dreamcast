using helengine.baseplatform.Builders;
using helengine.baseplatform.Descriptors;
using helengine.baseplatform.Reporting;
using helengine.baseplatform.Requests;
using helengine.baseplatform.Results;

namespace helengine.dreamcast.builder;

/// <summary>Publishes the Dreamcast platform contract and delegates native execution behind an injectable seam.</summary>
public sealed class DreamcastPlatformAssetBuilder : IPlatformAssetBuilder {
    readonly DreamcastMaterialCooker MaterialCooker = new();
    readonly IDreamcastNativeBuildExecutor NativeBuildExecutor;

    /// <summary>Creates a builder using the production native executor.</summary>
    public DreamcastPlatformAssetBuilder()
        : this(new DreamcastNativeBuildExecutor()) {
    }

    /// <summary>Creates a builder using a caller-provided native executor.</summary>
    /// <param name="nativeBuildExecutor">Executor used for the native build phase.</param>
    public DreamcastPlatformAssetBuilder(IDreamcastNativeBuildExecutor nativeBuildExecutor) {
        NativeBuildExecutor = nativeBuildExecutor ?? throw new ArgumentNullException(nameof(nativeBuildExecutor));
        Descriptor = new PlatformBuilderDescriptor(
            "helengine.dreamcast.builder",
            "0.2.0",
            "dc",
            new EngineCompatibilityRange("1.0.0", "999.0.0"),
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
        string contentRoot = Path.Combine(request.WorkingRoot, "dreamcast-content");
        string catalogRoot = Path.Combine(request.WorkingRoot, "dreamcast-catalog");
        try {
            cancellationToken.ThrowIfCancellationRequested();
            if (!string.Equals(request.Manifest.PlatformName, "dc", StringComparison.OrdinalIgnoreCase)) {
                throw new InvalidOperationException($"Dreamcast builder received manifest for platform '{request.Manifest.PlatformName}'.");
            }
            Directory.CreateDirectory(request.OutputRoot);
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
