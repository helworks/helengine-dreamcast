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
        PlatformCookWorkItem workItem = CreateWorkItem(sourceImagePath, "audio-clip", "audio/sound.dctex");

        InvalidOperationException exception = Assert.Throws<InvalidOperationException>(() =>
            new DreamcastPlatformCookWorkItemExecutor().Execute(workItem, packageSourceRoot));

        Assert.Contains("audio-clip", exception.Message, StringComparison.Ordinal);
    }

    /// <summary>Writes a valid cooked texture for the dedicated font-atlas work-item kind.</summary>
    [Fact]
    public void Execute_WritesACookedPayloadForFontAtlasKind() {
        using TemporaryDirectory temporary = new();
        string sourceImagePath = WriteSolidPng(Path.Combine(temporary.Path, "source.png"), 4, 4);
        string packageSourceRoot = Path.Combine(temporary.Path, "package-source");
        Directory.CreateDirectory(packageSourceRoot);
        PlatformCookWorkItem workItem = CreateWorkItem(sourceImagePath, "font-atlas-texture", "fonts/default.dctex", maxResolution: 256);

        new DreamcastPlatformCookWorkItemExecutor().Execute(workItem, packageSourceRoot);

        byte[] cooked = File.ReadAllBytes(Path.Combine(packageSourceRoot, "fonts", "default.dctex"));
        Assert.Equal(DreamcastTextureSchemaIds.Magic, BitConverter.ToUInt32(cooked, 0));
        Assert.Equal((ushort)4, BitConverter.ToUInt16(cooked, 10));
        Assert.Equal((ushort)4, BitConverter.ToUInt16(cooked, 12));
    }

    /// <summary>Font-atlas resizing preserves a non-square atlas ratio under its 256-pixel cap.</summary>
    [Fact]
    public void Execute_CapsFontAtlasAndPreservesAspectRatio() {
        using TemporaryDirectory temporary = new();
        string sourceImagePath = WriteSolidPng(Path.Combine(temporary.Path, "source.png"), 512, 256);
        string packageSourceRoot = Path.Combine(temporary.Path, "package-source");
        Directory.CreateDirectory(packageSourceRoot);
        PlatformCookWorkItem workItem = CreateWorkItem(sourceImagePath, "font-atlas-texture", "fonts/default.dctex", maxResolution: 256);

        new DreamcastPlatformCookWorkItemExecutor().Execute(workItem, packageSourceRoot);

        byte[] cooked = File.ReadAllBytes(Path.Combine(packageSourceRoot, "fonts", "default.dctex"));
        Assert.Equal((ushort)256, BitConverter.ToUInt16(cooked, 10));
        Assert.Equal((ushort)128, BitConverter.ToUInt16(cooked, 12));
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

    /// <summary>Confirms the maximum-resolution setting reaches the downscale: a source above the cap is resized down to it.</summary>
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

    /// <summary>
    /// Confirms the downscale that runs inside the cook is the platform's own box average and not the shared
    /// engine processor's nearest-neighbour resize. The source is a horizontal ramp of red = x * 16 across
    /// sixteen columns, cooked with a cap of four, which is two exact halvings. Averaging gives 24, 88, 152 and
    /// 216; nearest-neighbour would keep 0, 64, 128 and 192. The first cooked texel therefore reads 0xF100 under
    /// the box average and 0xF000 under nearest-neighbour, a difference the ARGB4444 red nibble preserves.
    /// </summary>
    [Fact]
    public void Execute_BoxAveragesTheDownscaleRatherThanSamplingOnePixelInSixteen() {
        using TemporaryDirectory temporary = new();
        string sourceImagePath = WriteHorizontalRampPng(Path.Combine(temporary.Path, "ramp.png"), 16, 16);
        string packageSourceRoot = Path.Combine(temporary.Path, "package-source");
        Directory.CreateDirectory(packageSourceRoot);
        PlatformCookWorkItem workItem = CreateWorkItem(sourceImagePath, "texture", "textures/ramp.dctex", maxResolution: 4);

        new DreamcastPlatformCookWorkItemExecutor().Execute(workItem, packageSourceRoot);

        byte[] cooked = File.ReadAllBytes(Path.Combine(packageSourceRoot, "textures", "ramp.dctex"));
        Assert.Equal((ushort)4, BitConverter.ToUInt16(cooked, 10));
        Assert.Equal((ushort)4, BitConverter.ToUInt16(cooked, 12));
        Assert.Equal((ushort)0xF100, BitConverter.ToUInt16(cooked, DreamcastTextureSchemaIds.HeaderBytes));
    }

    /// <summary>
    /// Cooks from a serialized engine texture asset, the form the editor's generated font atlases arrive in.
    /// The decoder releases the deserialized source asset, which nulls its pixel buffer, so this also pins that
    /// the decoded asset owns its own copy: a cooked payload carrying the source pixel would be impossible if
    /// the returned asset were aliasing a buffer the source just released.
    /// </summary>
    [Fact]
    public void Execute_CooksFromASerializedEngineTextureAssetSource() {
        using TemporaryDirectory temporary = new();
        string sourceAssetPath = Path.Combine(temporary.Path, "atlas.hasset");
        using (helengine.TextureAsset source = new() {
            Id = "textures/atlas",
            Width = 2,
            Height = 2,
            // Four opaque pixels of 0xF0, 0x80, 0x10, which the cooker packs as ARGB4444 0xFF81.
            Colors = [
                0xF0, 0x80, 0x10, 0xFF, 0xF0, 0x80, 0x10, 0xFF,
                0xF0, 0x80, 0x10, 0xFF, 0xF0, 0x80, 0x10, 0xFF
            ],
            PaletteColors = [],
            ColorFormat = TextureAssetColorFormat.Rgba32,
            AlphaPrecision = TextureAssetAlphaPrecision.A8
        }) {
            File.WriteAllBytes(sourceAssetPath, helengine.files.AssetSerializer.SerializeToBytes(source));
        }
        string packageSourceRoot = Path.Combine(temporary.Path, "package-source");
        Directory.CreateDirectory(packageSourceRoot);
        PlatformCookWorkItem workItem = CreateWorkItem(sourceAssetPath, "texture", "textures/atlas.dctex");

        new DreamcastPlatformCookWorkItemExecutor().Execute(workItem, packageSourceRoot);

        byte[] cooked = File.ReadAllBytes(Path.Combine(packageSourceRoot, "textures", "atlas.dctex"));
        Assert.Equal(DreamcastTextureSchemaIds.Magic, BitConverter.ToUInt32(cooked, 0));
        Assert.Equal((ushort)2, BitConverter.ToUInt16(cooked, 10));
        Assert.Equal((ushort)2, BitConverter.ToUInt16(cooked, 12));
        // Padded up to the eight-pixel minimum side; texel (0,0) is twiddled index 0 either way.
        Assert.Equal((ushort)8, BitConverter.ToUInt16(cooked, 8));
        Assert.Equal((ushort)0xFF81, BitConverter.ToUInt16(cooked, DreamcastTextureSchemaIds.HeaderBytes));
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

    static string WriteHorizontalRampPng(string path, int width, int height) {
        using Bitmap bitmap = new(width, height);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                bitmap.SetPixel(x, y, Color.FromArgb(255, x * 16, 0, 0));
            }
        }
        bitmap.Save(path, ImageFormat.Png);
        return path;
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
