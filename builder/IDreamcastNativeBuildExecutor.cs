using helengine.baseplatform.Requests;

namespace helengine.dreamcast.builder;

/// <summary>Abstracts native Dreamcast compilation so builder tests can inject a deterministic executor.</summary>
public interface IDreamcastNativeBuildExecutor {
    /// <summary>Builds the native Dreamcast output for a validated graph request.</summary>
    /// <param name="request">Complete platform build request.</param>
    /// <param name="contentRoot">Fresh staged content root.</param>
    /// <param name="catalogRoot">Builder-owned startup catalog root.</param>
    /// <param name="cancellationToken">Cooperative cancellation token.</param>
    void Build(PlatformBuildRequest request, string contentRoot, string catalogRoot, CancellationToken cancellationToken);
}
