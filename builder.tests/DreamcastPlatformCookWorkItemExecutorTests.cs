using System.Drawing;
using System.Drawing.Imaging;
using helengine.baseplatform.Manifest;
using helengine.dreamcast.builder;
using helengine.editor;

namespace helengine.dreamcast.builder.tests;

/// <summary>Verifies the Dreamcast builder-owned cook work item executor.</summary>
public sealed class DreamcastPlatformCookWorkItemExecutorTests {
    /// <summary>Rejects a source asset kind this platform does not support, naming it.</summary>
    [Fact]
    public void Execute_RejectsAnUnsupportedSourceAssetKindByName() {
        using TemporaryDirectory temporary = new();
        string sourceImagePath = WriteSolidPng(Path.Combine(temporary.Path, "source.png"), 4, 4);
        string packageSourceRoot = Path.Combine(temporary.Path, "package-source");
        Directory.CreateDirectory(packageSourceRoot);
        PlatformCookWorkItem workItem = CreateWorkItem(sourceImagePath, "font-atlas-texture", "textures/icon.dctex");

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() =>
            new DreamcastPlatformCookWorkItemExecutor().Execute(workItem, packageSourceRoot));

        Assert.Contains("font-atlas-texture", exception.Message, StringComparison.Ordinal);
    }

    /// <summary>Writes a cooked payload to the expected path for a supported source asset kind.</summary>
    [Fact]
    public void Execute_WritesACookedPayloadForASupportedKind() {
        using TemporaryDirectory temporary = new();
        string sourceImagePath = WriteSolidPng(Path.Combine(temporary.Path, "source.png"), 4, 4);
        string packageSourceRoot = Path.Combine(temporary.Path, "package-source");
        Directory.CreateDirectory(packageSourceRoot);
        PlatformCookWorkItem workItem = CreateWorkItem(sourceImagePath, "texture", "textures/icon.dctex");

        new DreamcastPlatformCookWorkItemExecutor().Execute(workItem, packageSourceRoot);

        string expectedPath = Path.Combine(packageSourceRoot, "textures", "icon.dctex");
        Assert.True(File.Exists(expectedPath));
        byte[] cooked = File.ReadAllBytes(expectedPath);
        Assert.Equal(DreamcastTextureSchemaIds.Magic, BitConverter.ToUInt32(cooked, 0));
    }

    /// <summary>Confirms the maximum-resolution setting reaches the shared engine processor: a source above the cap is resized down to it.</summary>
    [Fact]
    public void Execute_CapsTheCookedTextureToTheConfiguredMaximumResolution() {
        using TemporaryDirectory temporary = new();
        string sourceImagePath = WriteSolidPng(Path.Combine(temporary.Path, "source.png"), 256, 256);
        string packageSourceRoot = Path.Combine(temporary.Path, "package-source");
        Directory.CreateDirectory(packageSourceRoot);
        PlatformCookWorkItem workItem = CreateWorkItem(sourceImagePath, "texture", "textures/icon.dctex", maxResolution: 64);

        new DreamcastPlatformCookWorkItemExecutor().Execute(workItem, packageSourceRoot);

        byte[] cooked = File.ReadAllBytes(Path.Combine(packageSourceRoot, "textures", "icon.dctex"));
        ushort usedWidth = BitConverter.ToUInt16(cooked, 10);
        ushort usedHeight = BitConverter.ToUInt16(cooked, 12);
        Assert.Equal((ushort)64, usedWidth);
        Assert.Equal((ushort)64, usedHeight);
    }

    static PlatformCookWorkItem CreateWorkItem(string sourceAssetPath, string sourceAssetKind, string outputRelativePath, int maxResolution = 64) {
        string serializedSettings = DreamcastTextureCookSettingsSerializer.Serialize(new TextureAssetProcessorSettings {
            MaxResolution = maxResolution,
            ColorFormat = TextureAssetColorFormat.Rgba32,
            AlphaPrecision = TextureAssetAlphaPrecision.A8
        });
        return new PlatformCookWorkItem(
            "dc:" + sourceAssetKind + ":" + outputRelativePath,
            sourceAssetPath,
            sourceAssetKind,
            "dc",
            "runtime-texture",
            outputRelativePath,
            "runtime-texture:" + outputRelativePath,
            "sha256:" + new string('0', 64),
            "sha256:" + new string('1', 64),
            serializedSettings,
            []);
    }

    static string WriteSolidPng(string path, int width, int height) {
        using Bitmap bitmap = new(width, height);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                bitmap.SetPixel(x, y, Color.FromArgb(255, 208, 32, 32));
            }
        }
        bitmap.Save(path, ImageFormat.Png);
        return path;
    }

    sealed class TemporaryDirectory : IDisposable {
        public TemporaryDirectory() {
            Path = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "helengine-dc-cook-executor-" + Guid.NewGuid().ToString("N"));
            Directory.CreateDirectory(Path);
        }

        public string Path { get; }

        public void Dispose() {
            if (Directory.Exists(Path)) {
                Directory.Delete(Path, recursive: true);
            }
        }
    }
}
