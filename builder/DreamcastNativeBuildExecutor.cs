namespace helengine.dreamcast.builder;

/// <summary>Reports the explicit Task 1 native-build boundary until the Dreamcast native bridge is connected.</summary>
public sealed class DreamcastNativeBuildExecutor : IDreamcastNativeBuildExecutor {
    /// <summary>Fails clearly because Task 1 only establishes the platform contract and cooked inputs.</summary>
    /// <param name="request">Complete platform request.</param>
    /// <param name="contentRoot">Staged content root.</param>
    /// <param name="catalogRoot">Startup catalog root.</param>
    /// <param name="cancellationToken">Cooperative cancellation token.</param>
    public void Build(
        helengine.baseplatform.Requests.PlatformBuildRequest request,
        string contentRoot,
        string catalogRoot,
        CancellationToken cancellationToken) {
        if (request == null) {
            throw new ArgumentNullException(nameof(request));
        }
        cancellationToken.ThrowIfCancellationRequested();
        throw new InvalidOperationException("Dreamcast native executor is not ready; Task 1 only validates the platform contract and cooked content.");
    }
}
