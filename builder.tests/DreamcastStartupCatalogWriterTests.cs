using helengine.dreamcast.builder;

namespace helengine.dreamcast.builder.tests;

/// <summary>Verifies deterministic external startup catalog generation.</summary>
public sealed class DreamcastStartupCatalogWriterTests {
    /// <summary>Writes the stable ABI declaration and ordinal entry order.</summary>
    [Fact]
    public void WriteEmitsSortedExternalCatalog() {
        string catalogRoot = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "helengine-dc-catalog-" + Guid.NewGuid().ToString("N"));
        try {
            new DreamcastStartupCatalogWriter().Write(catalogRoot, "startup", [
                new DreamcastStartupCatalogEntry("zeta", "cooked/zeta.hasset"),
                new DreamcastStartupCatalogEntry("startup", "cooked/scenes/startup.hasset")
            ]);

            string header = File.ReadAllText(System.IO.Path.Combine(catalogRoot, "dc_runtime_scene_catalog.hpp"));
            string source = File.ReadAllText(System.IO.Path.Combine(catalogRoot, "dc_runtime_scene_catalog.cpp"));
            Assert.Contains("struct HEDcSceneCatalogEntry", header);
            Assert.Contains("he_dc_scene_catalog_entries", header);
            Assert.Contains("const char kStartupSceneId[] = \"startup\";", source);
            Assert.True(source.IndexOf("startup", StringComparison.Ordinal) < source.IndexOf("zeta", StringComparison.Ordinal));
            Assert.DoesNotContain("kStartupSceneId[] = \"\n", source, StringComparison.Ordinal);
        } finally {
            if (Directory.Exists(catalogRoot)) {
                Directory.Delete(catalogRoot, recursive: true);
            }
        }
    }

    /// <summary>Requires exact startup mapping and rejects normalized id collisions.</summary>
    [Fact]
    public void WriteRejectsMissingStartupAndCollisions() {
        DreamcastStartupCatalogWriter writer = new();
        Assert.Throws<InvalidOperationException>(() => writer.Write(
            System.IO.Path.Combine(System.IO.Path.GetTempPath(), Guid.NewGuid().ToString("N")),
            "startup",
            [new DreamcastStartupCatalogEntry("other", "cooked/other.hasset")]));
        Assert.Throws<InvalidOperationException>(() => writer.Write(
            System.IO.Path.Combine(System.IO.Path.GetTempPath(), Guid.NewGuid().ToString("N")),
            "startup",
            [new DreamcastStartupCatalogEntry("startup", "cooked/a.hasset"), new DreamcastStartupCatalogEntry("STARTUP", "cooked/b.hasset")]));
    }
}
