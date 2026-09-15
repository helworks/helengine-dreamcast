using helengine.baseplatform.Manifest;

namespace helengine.dreamcast.builder;

/// <summary>Validates the small scene subset that the Dreamcast v0.2 runtime can load.</summary>
public sealed class DreamcastSceneCompatibilityValidator {
    const string PlatformId = "dc";

    /// <summary>Validates the manifest and resolves its startup scene to cooked artifacts.</summary>
    /// <param name="manifest">Resolved editor build manifest.</param>
    /// <returns>Deterministically ordered catalog entries.</returns>
    public IReadOnlyList<DreamcastStartupCatalogEntry> Validate(PlatformBuildManifest manifest) {
        if (manifest == null) {
            throw new ArgumentNullException(nameof(manifest));
        }
        if (!string.Equals(manifest.PlatformName, PlatformId, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Dreamcast builder received platform '{manifest.PlatformName}'.");
        }
        // Engine compatibility is enforced by platform discovery against the descriptor
        // range; the manifest must still name the engine it was produced for.
        if (string.IsNullOrWhiteSpace(manifest.RequiredEngineVersion)) {
            throw new InvalidOperationException("Dreamcast manifests must declare the required engine version.");
        }
        if (manifest.Scenes.Length == 0) {
            throw new InvalidOperationException("Dreamcast authored builds require one resolved startup scene.");
        }

        Dictionary<string, PlatformBuildArtifact> artifactsByPath = new(StringComparer.OrdinalIgnoreCase);
        foreach (PlatformBuildArtifact artifact in manifest.CookedArtifacts) {
            string path = NormalizeRelativePath(artifact.RelativePath, "cooked artifact");
            if (!artifactsByPath.TryAdd(path, artifact)) {
                throw new InvalidOperationException($"Cooked artifact path collision: '{path}'.");
            }
        }

        Dictionary<string, PlatformBuildScene> scenesById = new(StringComparer.OrdinalIgnoreCase);
        List<DreamcastStartupCatalogEntry> entries = [];
        foreach (PlatformBuildScene scene in manifest.Scenes) {
            if (!scenesById.TryAdd(scene.SceneId, scene)) {
                throw new InvalidOperationException($"Duplicate scene id '{scene.SceneId}'.");
            }

            string cookedPath = ResolveCookedPath(scene);
            if (!artifactsByPath.ContainsKey(cookedPath)) {
                throw new InvalidOperationException($"Scene '{scene.SceneId}' declares missing cooked artifact '{cookedPath}'.");
            }
            entries.Add(new DreamcastStartupCatalogEntry(scene.SceneId, cookedPath));
        }

        if (!entries.Any(entry => string.Equals(entry.SceneId, manifest.StartupSceneId, StringComparison.Ordinal))) {
            throw new InvalidOperationException($"Startup scene '{manifest.StartupSceneId}' is not present in the resolved scene manifest.");
        }
        if (manifest.RuntimeFeatureManifest.RequiredFeatures.Length != 0) {
            string unsupported = string.Join(", ", manifest.RuntimeFeatureManifest.RequiredFeatures.Select(feature => feature.FeatureId));
            throw new InvalidOperationException($"Dreamcast v0.2 does not support required runtime features: {unsupported}.");
        }

        return entries.OrderBy(entry => entry.SceneId, StringComparer.Ordinal).ToArray();
    }

    /// <summary>Normalizes a runtime-relative path and rejects paths outside the ROMFS root.</summary>
    public static string NormalizeRelativePath(string path, string valueName = "path") {
        if (string.IsNullOrWhiteSpace(path)) {
            throw new InvalidOperationException($"{valueName} is required.");
        }
        if (path.IndexOf('\0') >= 0 || path.Contains(':', StringComparison.Ordinal)) {
            throw new InvalidOperationException($"{valueName} contains an invalid character.");
        }

        string normalized = path.Replace('\\', '/');
        if (normalized.StartsWith("/", StringComparison.Ordinal) ||
            normalized.StartsWith("//", StringComparison.Ordinal) ||
            Path.IsPathRooted(path)) {
            throw new InvalidOperationException($"{valueName} must be relative: '{path}'.");
        }

        string[] segments = normalized.Split('/', StringSplitOptions.None);
        if (segments.Any(segment => segment.Length == 0 || segment == "." || segment == "..")) {
            throw new InvalidOperationException($"{valueName} contains an empty or traversal segment: '{path}'.");
        }

        return string.Join('/', segments);
    }

    static string ResolveCookedPath(PlatformBuildScene scene) {
        KeyValuePair<string, string>[] matches = scene.ResolvedMetadata
            .Where(metadata => string.Equals(metadata.Key, PlatformBuildSceneMetadataKeys.CookedRelativePath, StringComparison.OrdinalIgnoreCase))
            .ToArray();
        if (matches.Length != 1) {
            throw new InvalidOperationException($"Scene '{scene.SceneId}' must have exactly one cooked-relative-path metadata value.");
        }
        return NormalizeRelativePath(matches[0].Value, $"Scene '{scene.SceneId}' cooked path");
    }
}
