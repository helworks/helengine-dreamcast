using System.Text;

namespace helengine.dreamcast.builder;

/// <summary>Emits the external C ABI consumed by the native Dreamcast scene bootstrap.</summary>
public sealed class DreamcastStartupCatalogWriter {
    /// <summary>Writes a validated deterministic startup catalog outside generated Core output.</summary>
    public void Write(
        string catalogRoot,
        string startupSceneId,
        IReadOnlyList<DreamcastStartupCatalogEntry> entries) {
        if (string.IsNullOrWhiteSpace(catalogRoot)) {
            throw new ArgumentException("Catalog root is required.", nameof(catalogRoot));
        }
        if (string.IsNullOrWhiteSpace(startupSceneId)) {
            throw new ArgumentException("Startup scene id is required.", nameof(startupSceneId));
        }
        if (entries == null || entries.Count == 0) {
            throw new InvalidOperationException("A non-probe startup catalog must contain at least one entry.");
        }

        List<DreamcastStartupCatalogEntry> sortedEntries = entries
            .Select(entry => new DreamcastStartupCatalogEntry(
                entry.SceneId,
                DreamcastSceneCompatibilityValidator.NormalizeRelativePath(entry.CookedRelativePath, "catalog cooked path")))
            .OrderBy(entry => entry.SceneId, StringComparer.Ordinal)
            .ToList();
        HashSet<string> sceneIds = new(StringComparer.OrdinalIgnoreCase);
        HashSet<string> cookedPaths = new(StringComparer.OrdinalIgnoreCase);
        foreach (DreamcastStartupCatalogEntry entry in sortedEntries) {
            if (!sceneIds.Add(entry.SceneId)) {
                throw new InvalidOperationException($"Duplicate catalog scene id '{entry.SceneId}'.");
            }
            if (!cookedPaths.Add(entry.CookedRelativePath)) {
                throw new InvalidOperationException($"Duplicate catalog cooked path '{entry.CookedRelativePath}'.");
            }
        }
        if (!sortedEntries.Any(entry => string.Equals(entry.SceneId, startupSceneId, StringComparison.Ordinal))) {
            throw new InvalidOperationException($"Catalog does not contain startup scene '{startupSceneId}'.");
        }

        Directory.CreateDirectory(catalogRoot);
        string headerPath = Path.Combine(catalogRoot, "dc_runtime_scene_catalog.hpp");
        string sourcePath = Path.Combine(catalogRoot, "dc_runtime_scene_catalog.cpp");
        Encoding encoding = new UTF8Encoding(encoderShouldEmitUTF8Identifier: false);
        WriteIfChanged(headerPath, "#pragma once\n\n#include <cstddef>\n\nstruct HEDcSceneCatalogEntry {\n    const char* sceneId;\n    const char* cookedRelativePath;\n};\n\nextern \"C\" const char* he_dc_startup_scene_id();\nextern \"C\" const HEDcSceneCatalogEntry* he_dc_scene_catalog_entries(std::size_t* count);\n", encoding);

        StringBuilder source = new();
        source.AppendLine("#include \"dc_runtime_scene_catalog.hpp\"");
        source.AppendLine();
        source.AppendLine("namespace {");
        source.AppendLine("const HEDcSceneCatalogEntry kEntries[] = {");
        foreach (DreamcastStartupCatalogEntry entry in sortedEntries) {
            source.Append("    {\"").Append(Escape(entry.SceneId)).Append("\", \"")
                .Append(Escape(entry.CookedRelativePath)).AppendLine("\"},");
        }
        source.AppendLine("};");
        source.Append("const char kStartupSceneId[] = \"");
        source.Append(Escape(startupSceneId)).AppendLine("\";");
        source.AppendLine("}");
        source.AppendLine();
        source.AppendLine("extern \"C\" const char* he_dc_startup_scene_id() { return kStartupSceneId; }");
        source.AppendLine("extern \"C\" const HEDcSceneCatalogEntry* he_dc_scene_catalog_entries(std::size_t* count) {");
        source.AppendLine("    if (count != nullptr) { *count = sizeof(kEntries) / sizeof(kEntries[0]); }");
        source.AppendLine("    return kEntries;");
        source.AppendLine("}");
        WriteIfChanged(sourcePath, source.ToString(), encoding);
    }

    static void WriteIfChanged(string path, string content, Encoding encoding) {
        if (File.Exists(path) && string.Equals(File.ReadAllText(path, encoding), content, StringComparison.Ordinal)) return;
        File.WriteAllText(path, content, encoding);
    }

    static string Escape(string value) => value.Replace("\\", "\\\\", StringComparison.Ordinal).Replace("\"", "\\\"", StringComparison.Ordinal);
}
