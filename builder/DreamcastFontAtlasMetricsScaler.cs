using System.Buffers.Binary;

namespace helengine.dreamcast.builder;

/// <summary>Rescales externalized FontAsset metrics to match their cooked Dreamcast atlas extents.</summary>
public static class DreamcastFontAtlasMetricsScaler {
    /// <summary>Applies a cooked atlas extent to a font, scaling pixel metrics independently on X and Y.</summary>
    public static void RescaleMetrics(helengine.FontAsset font, ushort usedWidth, ushort usedHeight) {
        if (font == null) throw new ArgumentNullException(nameof(font));
        if (usedWidth == 0 || usedHeight == 0)
            throw new ArgumentOutOfRangeException(nameof(usedWidth), "Cooked font atlas dimensions must be positive.");
        if (font.AtlasWidth < 1 || font.AtlasHeight < 1)
            throw new InvalidOperationException("Font atlas source dimensions must be positive before rescaling.");
        if (font.AtlasWidth == usedWidth && font.AtlasHeight == usedHeight) return;

        var processedExtent = new helengine.TextureAsset {
            Width = usedWidth,
            Height = usedHeight,
            ColorFormat = helengine.TextureAssetColorFormat.Rgba32,
            AlphaPrecision = helengine.TextureAssetAlphaPrecision.A8,
            Colors = Array.Empty<byte>(),
            PaletteColors = Array.Empty<byte>()
        };
        try {
            // Reuse the engine's canonical per-axis metric scaling (line height,
            // spacing, glyph offset and advance) without embedding placeholder
            // texture bytes in the externalized font payload.
            font.ApplyProcessedSourceTextureAsset(processedExtent);
            font.SourceTextureAsset = null;
        } finally {
            processedExtent.Dispose();
        }
    }

    /// <summary>Updates externalized staged fonts whose cooked atlas work item belongs to this package, preserving staging timestamps.</summary>
    public static void RescalePackagedFonts(string packageSourceRoot,
                                            IReadOnlyList<helengine.baseplatform.Manifest.PlatformCookWorkItem> workItems) {
        if (string.IsNullOrWhiteSpace(packageSourceRoot))
            throw new ArgumentException("Package source root is required.", nameof(packageSourceRoot));
        ArgumentNullException.ThrowIfNull(workItems);
        string root = Path.GetFullPath(packageSourceRoot);
        if (!Directory.Exists(root)) throw new DirectoryNotFoundException(root);

        var atlasOutputs = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var item in workItems) {
            if (item != null && string.Equals(item.SourceAssetKind, "font-atlas-texture", StringComparison.OrdinalIgnoreCase))
                atlasOutputs.Add(NormalizeRelativePath(item.OutputRelativePath));
        }
        if (atlasOutputs.Count == 0) return;

        foreach (string fontPath in Directory.EnumerateFiles(root, "*.hefont", SearchOption.AllDirectories)) {
            helengine.FontAsset font;
            using (var input = new FileStream(fontPath, FileMode.Open, FileAccess.Read, FileShare.Read))
                font = helengine.files.FontAssetBinarySerializer.Deserialize(input);
            try {
                string cookedPath = NormalizeRelativePath(font.CookedAtlasTextureRelativePath);
                if (!atlasOutputs.Contains(cookedPath)) continue;
                string cookedFile = ResolveUnderRoot(root, cookedPath);
                byte[] cooked = File.ReadAllBytes(cookedFile);
                (ushort usedWidth, ushort usedHeight) = ReadCookedExtent(cooked, cookedFile);
                if (font.AtlasWidth == usedWidth && font.AtlasHeight == usedHeight) continue;
                RescaleMetrics(font, usedWidth, usedHeight);
                string temporaryPath = fontPath + ".dreamcast.tmp";
                string directoryPath = Path.GetDirectoryName(fontPath)!;
                DateTime fontTimestamp = File.GetLastWriteTimeUtc(fontPath);
                DateTime directoryTimestamp = Directory.GetLastWriteTimeUtc(directoryPath);
                try {
                    using (var output = new FileStream(temporaryPath, FileMode.Create, FileAccess.Write, FileShare.None))
                        helengine.files.FontAssetBinarySerializer.Serialize(output, font);
                    File.Move(temporaryPath, fontPath, true);
                } finally {
                    if (File.Exists(temporaryPath)) File.Delete(temporaryPath);
                    File.SetLastWriteTimeUtc(fontPath, fontTimestamp);
                    Directory.SetLastWriteTimeUtc(directoryPath, directoryTimestamp);
                }
            } finally {
                font.Dispose();
            }
        }
    }

    /// <summary>Reads used atlas dimensions after applying the runtime's shared versioned texture validation.</summary>
    static (ushort Width, ushort Height) ReadCookedExtent(byte[] bytes, string path) {
        if (bytes == null || bytes.Length < DreamcastTextureSchemaIds.HeaderBytes ||
            BinaryPrimitives.ReadUInt32LittleEndian(bytes.AsSpan(0, 4)) != DreamcastTextureSchemaIds.Magic)
            throw new InvalidDataException($"Cooked font atlas '{path}' has an invalid Dreamcast texture header.");
        DreamcastCookedContentValidator.ValidateCookedTexture(bytes, path);
        ushort width = BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(10, 2));
        ushort height = BinaryPrimitives.ReadUInt16LittleEndian(bytes.AsSpan(12, 2));
        return (width, height);
    }

    static string NormalizeRelativePath(string path) {
        if (string.IsNullOrWhiteSpace(path)) return string.Empty;
        string normalized = path.Replace('\\', '/').TrimStart('/');
        if (normalized.Split('/').Any(part => part == ".."))
            throw new InvalidDataException($"Font atlas path escapes the package root: '{path}'.");
        return normalized;
    }

    static string ResolveUnderRoot(string root, string relativePath) {
        string resolved = Path.GetFullPath(Path.Combine(root, relativePath.Replace('/', Path.DirectorySeparatorChar)));
        string prefix = Path.EndsInDirectorySeparator(root) ? root : root + Path.DirectorySeparatorChar;
        if (!resolved.StartsWith(prefix, StringComparison.OrdinalIgnoreCase))
            throw new InvalidDataException($"Font atlas path escapes the package root: '{relativePath}'.");
        if (!File.Exists(resolved)) throw new FileNotFoundException("Cooked font atlas is missing.", resolved);
        return resolved;
    }
}
