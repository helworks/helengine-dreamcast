using helengine.baseplatform.Manifest;
using helengine.editor;

namespace helengine.dreamcast.builder;

/// <summary>
/// Executes builder-owned Dreamcast platform cook work items by writing cooked runtime payloads into the package
/// source root.
/// </summary>
public sealed class DreamcastPlatformCookWorkItemExecutor {
    /// <summary>
    /// Source asset kinds this executor accepts. Dreamcast declares no separate <c>font-atlas-texture</c>
    /// capability, so the editor build graph falls the two authored font atlases back onto the ordinary
    /// <c>"texture"</c> capability; both arrive here under the same source asset kind.
    /// </summary>
    static readonly HashSet<string> SupportedSourceAssetKinds = new(StringComparer.OrdinalIgnoreCase) { "texture" };

    /// <summary>
    /// Decodes authored source images into raw texture assets.
    /// </summary>
    readonly DreamcastSourceTextureDecoder SourceTextureDecoder = new();

    /// <summary>
    /// Shared engine texture processor, used for format and alpha-precision work only. The image reaching it has
    /// already been brought within the settings' maximum resolution by
    /// <see cref="DreamcastTextureDownscaler"/>, so the processor's own single-step nearest-neighbour resize
    /// never runs. See that class for why this platform owns its filtering.
    /// </summary>
    readonly TextureAssetProcessor TextureProcessor = new();

    /// <summary>
    /// Twiddles the already-resized texture into the Dreamcast's cooked ARGB4444 runtime form.
    /// </summary>
    readonly DreamcastTextureCooker TextureCooker = new();

    /// <summary>
    /// Decodes and cooks one source texture work item into the package source root. The settings used for the
    /// cook are the ones the work item's owning "texture" capability already resolved; this method only decides
    /// which source asset kinds are accepted, downscales to the settings' maximum resolution through the
    /// platform's own <see cref="DreamcastTextureDownscaler"/>, reformats through the shared engine
    /// <see cref="TextureAssetProcessor"/>, and hands the result to the Dreamcast-specific twiddle cook.
    /// </summary>
    /// <param name="workItem">Work item describing the source asset, settings, and cooked output path.</param>
    /// <param name="packageSourceRootPath">Package source root that the ROMFS stager reads.</param>
    public void Execute(PlatformCookWorkItem workItem, string packageSourceRootPath) {
        if (workItem == null) {
            throw new ArgumentNullException(nameof(workItem));
        }
        if (string.IsNullOrWhiteSpace(packageSourceRootPath)) {
            throw new ArgumentException("Package source root path must be provided.", nameof(packageSourceRootPath));
        }
        if (!SupportedSourceAssetKinds.Contains(workItem.SourceAssetKind)) {
            throw new InvalidOperationException($"Dreamcast does not support source asset kind '{workItem.SourceAssetKind}' for work item '{workItem.WorkItemId}' at '{workItem.SourceAssetPath}'.");
        }

        TextureAssetProcessorSettings settings = DreamcastTextureCookSettingsSerializer.Deserialize(workItem.SerializedPlatformSettings);
        // Every TextureAsset below owns native pixel and palette buffers and implements IDisposable, so each one
        // is released once the cooked bytes have been produced, on the exceptional path as well. The processor
        // returns its input unchanged when no work is needed, so the two references can alias and a reference
        // comparison decides whether there are one or two objects to release.
        byte[] cookedBytes;
        helengine.TextureAsset decodedTexture = null;
        helengine.TextureAsset downscaledTexture = null;
        helengine.TextureAsset processedTexture = null;
        try {
            decodedTexture = SourceTextureDecoder.Decode(workItem.SourceAssetPath);
            decodedTexture.Id = workItem.OutputLogicalArtifactId;
            downscaledTexture = DreamcastTextureDownscaler.Downscale(decodedTexture, settings.MaxResolution);
            processedTexture = TextureProcessor.Apply(downscaledTexture, settings);
            cookedBytes = TextureCooker.Cook(processedTexture);
        } finally {
            if (!ReferenceEquals(processedTexture, downscaledTexture) && !ReferenceEquals(processedTexture, decodedTexture)) {
                processedTexture?.Dispose();
            }
            if (!ReferenceEquals(downscaledTexture, decodedTexture)) {
                downscaledTexture?.Dispose();
            }
            decodedTexture?.Dispose();
        }

        string destinationPath = Path.Combine(
            Path.GetFullPath(packageSourceRootPath),
            DreamcastSceneCompatibilityValidator.NormalizeRelativePath(workItem.OutputRelativePath, "cook work item output path").Replace('/', Path.DirectorySeparatorChar));
        string destinationDirectory = Path.GetDirectoryName(destinationPath)
            ?? throw new InvalidOperationException($"Dreamcast cooked texture destination directory could not be resolved for '{destinationPath}'.");
        Directory.CreateDirectory(destinationDirectory);
        File.WriteAllBytes(destinationPath, cookedBytes);
    }
}
