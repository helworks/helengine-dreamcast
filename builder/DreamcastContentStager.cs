using System.Security.Cryptography;
using helengine.baseplatform.Manifest;
using helengine.files;

namespace helengine.dreamcast.builder;

/// <summary>Copies only manifest-declared, runtime-loadable cooked artifacts into a deterministic ROMFS staging tree.</summary>
/// <remarks>
/// The Dreamcast v0.2 runtime can load scenes, models and platform materials only. Declared artifacts of any
/// other serialized type (textures, fonts, audio, imported blobs) are still required to exist but are not
/// staged, because the runtime could never open them and the ROMFS is resident in console RAM. The omitted
/// paths are exposed through <see cref="LastSkippedRelativePaths"/> for diagnostics.
/// </remarks>
public sealed class DreamcastContentStager {
    const long MaximumPayloadBytes = 1024 * 1024;
    static readonly DateTime BuildEpochUtc = new(2020, 1, 1, 0, 0, 0, DateTimeKind.Utc);
    readonly DreamcastSceneCompatibilityValidator Validator;

    /// <summary>Creates a stager with the default Dreamcast scene validator.</summary>
    public DreamcastContentStager()
        : this(new DreamcastSceneCompatibilityValidator()) {
    }

    /// <summary>Creates a stager with an injected validator.</summary>
    public DreamcastContentStager(DreamcastSceneCompatibilityValidator validator) {
        Validator = validator ?? throw new ArgumentNullException(nameof(validator));
    }

    /// <summary>Gets the declared artifacts the last <see cref="Stage"/> call left out because the runtime cannot load their type.</summary>
    public IReadOnlyList<string> LastSkippedRelativePaths { get; private set; } = [];

    /// <summary>Stages declared cooked files in stable order and returns their normalized paths.</summary>
    public IReadOnlyList<string> Stage(
        PlatformBuildManifest manifest,
        string cookedRoot,
        string freshStagingRoot,
        CancellationToken cancellationToken) {
        if (manifest == null) {
            throw new ArgumentNullException(nameof(manifest));
        }
        if (string.IsNullOrWhiteSpace(cookedRoot)) {
            throw new ArgumentException("Cooked root is required.", nameof(cookedRoot));
        }
        if (string.IsNullOrWhiteSpace(freshStagingRoot)) {
            throw new ArgumentException("Fresh staging root is required.", nameof(freshStagingRoot));
        }

        Validator.Validate(manifest);
        string sourceRoot = Path.GetFullPath(cookedRoot);
        string destinationRoot = Path.GetFullPath(freshStagingRoot);
        if (!Directory.Exists(sourceRoot)) {
            throw new DirectoryNotFoundException($"Cooked package source does not exist: {sourceRoot}");
        }
        RejectReparseRootOrAncestor(sourceRoot);
        RejectReparseRootOrAncestor(destinationRoot);
        if (IsSameOrNested(destinationRoot, sourceRoot) || IsSameOrNested(sourceRoot, destinationRoot)) {
            throw new InvalidOperationException("Cooked source and ROMFS staging roots must not overlap.");
        }
        if (Directory.Exists(destinationRoot) && Directory.EnumerateFileSystemEntries(destinationRoot).Any()) {
            throw new InvalidOperationException($"Staging root must be fresh and empty: {destinationRoot}");
        }
        Directory.CreateDirectory(destinationRoot);

        long totalBytes = 0;
        List<string> stagedPaths = [];
        List<string> skippedPaths = [];
        foreach (PlatformBuildArtifact artifact in manifest.CookedArtifacts
                     .OrderBy(value => DreamcastSceneCompatibilityValidator.NormalizeRelativePath(value.RelativePath), StringComparer.Ordinal)) {
            cancellationToken.ThrowIfCancellationRequested();
            string relativePath = DreamcastSceneCompatibilityValidator.NormalizeRelativePath(artifact.RelativePath, "cooked artifact");
            string sourcePath = ResolveUnderRoot(sourceRoot, relativePath);
            string destinationPath = ResolveUnderRoot(destinationRoot, relativePath);
            RejectReparsePoints(sourceRoot, relativePath);
            if (!File.Exists(sourcePath)) {
                throw new FileNotFoundException($"Declared cooked artifact is missing: {relativePath}", sourcePath);
            }

            if (!IsRuntimeLoadable(sourcePath)) {
                skippedPaths.Add(relativePath);
                continue;
            }

            FileInfo sourceInfo = new(sourcePath);
            if (sourceInfo.Length > MaximumPayloadBytes || sourceInfo.Length > MaximumPayloadBytes - totalBytes) {
                throw new InvalidOperationException("Dreamcast ROMFS payload exceeds the one MiB limit.");
            }
            Directory.CreateDirectory(Path.GetDirectoryName(destinationPath)!);
            using (FileStream input = new(sourcePath, FileMode.Open, FileAccess.Read, FileShare.Read, 64 * 1024, FileOptions.SequentialScan))
            using (FileStream output = new(destinationPath, FileMode.CreateNew, FileAccess.Write, FileShare.None, 64 * 1024, FileOptions.SequentialScan)) {
                input.CopyTo(output);
            }
            VerifyHash(artifact, destinationPath);
            long copiedLength = new FileInfo(destinationPath).Length;
            if (copiedLength > MaximumPayloadBytes || copiedLength > MaximumPayloadBytes - totalBytes) {
                throw new InvalidOperationException("Dreamcast ROMFS payload exceeds the one MiB limit.");
            }
            totalBytes += copiedLength;
            File.SetLastWriteTimeUtc(destinationPath, BuildEpochUtc);
            stagedPaths.Add(relativePath);
        }

        foreach (string directory in Directory.EnumerateDirectories(destinationRoot, "*", SearchOption.AllDirectories).Append(destinationRoot)) {
            Directory.SetLastWriteTimeUtc(directory, BuildEpochUtc);
        }

        LastSkippedRelativePaths = skippedPaths;
        return stagedPaths;
    }

    /// <summary>Returns true when the cooked file deserializes as a scene, model or platform material.</summary>
    static bool IsRuntimeLoadable(string sourcePath) {
        byte[] bytes = File.ReadAllBytes(sourcePath);
        if (bytes.Length < 4 || bytes[0] != (byte)'H' || bytes[1] != (byte)'E' || bytes[2] != (byte)'L' || bytes[3] != (byte)'E') {
            return false;
        }
        try {
            Asset asset = AssetSerializer.DeserializeFromBytes(bytes);
            return asset is SceneAsset || asset is ModelAsset || asset is PlatformMaterialAsset;
        } catch (Exception exception) when (exception is not OutOfMemoryException) {
            return false;
        }
    }

    static string ResolveUnderRoot(string root, string relativePath) {
        string fullPath = Path.GetFullPath(Path.Combine(root, relativePath.Replace('/', Path.DirectorySeparatorChar)));
        if (!IsSameOrNested(fullPath, root)) {
            throw new InvalidOperationException($"Resolved path escapes root: {relativePath}");
        }
        return fullPath;
    }

    static bool IsSameOrNested(string candidate, string root) {
        string normalizedCandidate = Path.TrimEndingDirectorySeparator(Path.GetFullPath(candidate));
        string normalizedRoot = Path.TrimEndingDirectorySeparator(Path.GetFullPath(root));
        return normalizedCandidate.Equals(normalizedRoot, StringComparison.OrdinalIgnoreCase) ||
            normalizedCandidate.StartsWith(normalizedRoot + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase);
    }

    static void RejectReparsePoints(string sourceRoot, string relativePath) {
        string current = sourceRoot;
        foreach (string segment in relativePath.Split('/')) {
            current = Path.Combine(current, segment);
            if (File.Exists(current) || Directory.Exists(current)) {
                FileAttributes attributes = File.GetAttributes(current);
                if ((attributes & FileAttributes.ReparsePoint) != 0) {
                    throw new InvalidOperationException($"Cooked package contains a reparse point: {relativePath}");
                }
            }
        }
    }

    static void RejectReparseRootOrAncestor(string path) {
        string current = Path.GetFullPath(path);
        while (!string.IsNullOrEmpty(current)) {
            if (File.Exists(current) || Directory.Exists(current)) {
                FileAttributes attributes = File.GetAttributes(current);
                if ((attributes & FileAttributes.ReparsePoint) != 0) {
                    throw new InvalidOperationException($"Package root or ancestor is a reparse point: {current}");
                }
            }
            string parent = Path.GetDirectoryName(Path.TrimEndingDirectorySeparator(current));
            if (string.Equals(parent, current, StringComparison.OrdinalIgnoreCase)) {
                break;
            }
            current = parent;
        }
    }

    static void VerifyHash(PlatformBuildArtifact artifact, string sourcePath) {
        const string prefix = "sha256:";
        if (!artifact.ContentHash.StartsWith(prefix, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Artifact '{artifact.RelativePath}' does not declare a sha256 content hash.");
        }
        string expected = artifact.ContentHash[prefix.Length..].ToLowerInvariant();
        if (expected.Length != 64 || expected.Any(character => !Uri.IsHexDigit(character))) {
            throw new InvalidOperationException($"Artifact '{artifact.RelativePath}' has an invalid sha256 content hash.");
        }
        using FileStream input = File.OpenRead(sourcePath);
        string actual = Convert.ToHexString(SHA256.HashData(input)).ToLowerInvariant();
        if (!string.Equals(expected, actual, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"Cooked artifact hash mismatch for '{artifact.RelativePath}'.");
        }
    }
}
