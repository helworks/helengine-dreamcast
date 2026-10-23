using helengine.files;

namespace helengine.dreamcast.builder;

/// <summary>Validates staged cooked scene, model, material and texture data before native compilation.</summary>
public sealed class DreamcastCookedContentValidator {
    const int MaximumPositions = 64;
    const int MaximumTriangles = 128;
    const string CameraTypeId = "helengine.CameraComponent";
    const string MeshTypeId = "helengine.MeshComponent";
    const string ViewportTypeId = "helengine.ViewportComponent";
    const string SpriteTypeId = "helengine.SpriteComponent";
    const string TextTypeId = "helengine.TextComponent";
    const string RoundedRectTypeId = "helengine.RoundedRectComponent";
    const string DirectionalLightTypeId = "helengine.DirectionalLightComponent";
    const string AmbientLightTypeId = "helengine.AmbientLightComponent";
    const string RendererFamilyId = "dc-standard";

    /// <summary>Validates the startup scene cardinality and every staged model, material and texture in the ROMFS tree.</summary>
    /// <remarks>
    /// <para>
    /// The cooked scene's reference list still carries authored paths while the runtime resolves
    /// component payloads to cooked paths, so every staged file is classified by its own bytes
    /// instead of by reference. Exact one-model/one-material usage is re-checked at runtime
    /// through the owned reference counts after the startup scene loads.
    /// </para>
    /// <para>
    /// Two kinds of staged file exist and they are not both engine assets. Scenes, models and materials are
    /// serialized engine assets and are validated through the engine deserializers. Textures are the platform's
    /// own <c>dc-texture-v1</c> payloads, written by <see cref="DreamcastPlatformCookWorkItemExecutor"/> and
    /// carrying the <c>DCTX</c> magic rather than the engine's, so they are recognized by that magic and
    /// validated against the same header rules the runtime parser enforces. Handing them to the engine
    /// deserializer would fail every build that cooks even one texture.
    /// </para>
    /// </remarks>
    /// <param name="stagedContentRoot">Fresh staging root produced by the content stager.</param>
    /// <param name="startupSceneCookedRelativePath">Normalized cooked path of the startup scene.</param>
    public void Validate(string stagedContentRoot, string startupSceneCookedRelativePath) {
        if (string.IsNullOrWhiteSpace(stagedContentRoot)) {
            throw new ArgumentException("Staged content root is required.", nameof(stagedContentRoot));
        }
        string root = Path.GetFullPath(stagedContentRoot);
        if (!Directory.Exists(root)) {
            throw new DirectoryNotFoundException($"Staged content root does not exist: {root}");
        }
        string scenePath = DreamcastSceneCompatibilityValidator.NormalizeRelativePath(startupSceneCookedRelativePath, "startup scene path");
        Asset sceneAsset = DeserializeAsset(scenePath, ReadStagedFile(root, scenePath));
        if (sceneAsset is not SceneAsset scene) {
            throw new InvalidOperationException($"Startup scene '{scenePath}' did not deserialize as a scene asset.");
        }

        int cameraCount = 0;
        int meshCount = 0;
        foreach (SceneEntityAsset entity in scene.RootEntities) {
            CountComponents(entity, true, ref cameraCount, ref meshCount);
        }
        if (cameraCount != 1) {
            throw new InvalidOperationException($"Dreamcast v0.2 requires exactly one enabled camera component in the startup scene; found {cameraCount}.");
        }
        if (meshCount > 1) {
            throw new InvalidOperationException($"Dreamcast supports at most one enabled mesh component in the startup scene; found {meshCount}.");
        }

        int modelCount = 0;
        int materialCount = 0;
        int textureCount = 0;
        long textureVideoMemoryBytes = 0;
        int paletteEntries = 0;
        foreach (string fullPath in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories).OrderBy(path => path, StringComparer.Ordinal)) {
            string relativePath = Path.GetRelativePath(root, fullPath).Replace('\\', '/');
            if (string.Equals(relativePath, scenePath, StringComparison.Ordinal)) {
                continue;
            }
            byte[] bytes = ReadStagedFile(root, relativePath);
            if (HasCookedTextureMagic(bytes)) {
                textureVideoMemoryBytes += ValidateCookedTexture(bytes, relativePath);
                if (BitConverter.ToUInt16(bytes, 4) == 2) paletteEntries += BitConverter.ToUInt16(bytes, 14);
                textureCount++;
                continue;
            }
            if (string.Equals(Path.GetExtension(relativePath), ".hefont", StringComparison.OrdinalIgnoreCase)) {
                ValidateFontAsset(root, relativePath, bytes);
                continue;
            }
            Asset staged = DeserializeAsset(relativePath, bytes);
            if (staged is ModelAsset model) {
                ValidateModel(model, relativePath);
                modelCount++;
            } else if (staged is PlatformMaterialAsset material) {
                ValidateMaterial(material, relativePath);
                materialCount++;
            } else if (staged is SceneAsset || staged is AnimationClipAsset) {
                // Additional scenes and transform clips are loaded by generated Core.
                continue;
            } else {
                throw new InvalidOperationException($"Staged package contains unsupported cooked asset type '{staged.GetType().Name}' at '{relativePath}'.");
            }
        }
        if (paletteEntries > 1024) throw new InvalidOperationException($"Staged package requires {paletteEntries} palette entries; Dreamcast palette RAM holds 1024.");
        if (meshCount > 0 && modelCount < 1) {
            throw new InvalidOperationException("Staged package contains no model; the startup mesh has nothing to draw.");
        }
        if (meshCount > 0 && materialCount < 1) {
            throw new InvalidOperationException("Staged package contains no dc-standard material; the startup mesh has no material.");
        }
        // Every cooked texture is uploaded into the pool the tile accelerator leaves behind and held for the
        // scene's lifetime, so the whole set has to fit at once. A runtime allocation failure fails the load on
        // the console with nothing to fall back to, so the total is checked here instead. This is a separate
        // budget from the ROMFS payload cap, which bounds main RAM, not video memory.
        if (textureVideoMemoryBytes > DreamcastTextureSchemaIds.MaximumTotalVideoMemoryBytes) {
            throw new InvalidOperationException(
                $"Staged package cooks {textureCount} textures totalling {textureVideoMemoryBytes} bytes of video memory, above the Dreamcast limit of {DreamcastTextureSchemaIds.MaximumTotalVideoMemoryBytes} bytes.");
        }
    }

    static void CountComponents(SceneEntityAsset entity, bool parentEnabled, ref int cameraCount, ref int meshCount) {
        bool enabled = parentEnabled && entity.Enabled;
        if (enabled) {
            foreach (SceneComponentAssetRecord component in entity.Components) {
                if (string.Equals(component.ComponentTypeId, CameraTypeId, StringComparison.Ordinal)) {
                    cameraCount++;
                } else if (string.Equals(component.ComponentTypeId, MeshTypeId, StringComparison.Ordinal)) {
                    meshCount++;
                } else if (string.Equals(component.ComponentTypeId, SpriteTypeId, StringComparison.Ordinal)) {
                    // Sprites are gathered by the 2D manager and submitted in the translucent overlay pass.
                    continue;
                } else if (string.Equals(component.ComponentTypeId, ViewportTypeId, StringComparison.Ordinal)) {
                    // The viewport scales the authored screen-space subtree before its 2D drawables are gathered.
                    continue;
                } else if (string.Equals(component.ComponentTypeId, TextTypeId, StringComparison.Ordinal) ||
                           string.Equals(component.ComponentTypeId, RoundedRectTypeId, StringComparison.Ordinal)) {
                    // Text and rounded rectangles are gathered into the ordered Dreamcast 2D overlay.
                    continue;
                } else if (string.Equals(component.ComponentTypeId, DirectionalLightTypeId, StringComparison.Ordinal) ||
                           string.Equals(component.ComponentTypeId, AmbientLightTypeId, StringComparison.Ordinal)) {
                    continue;
                } else if (component.ComponentTypeId is "helengine.AnimationPlayerComponent" or
                           "helengine.LayoutComponent" or "helengine.ScrollComponent" or "helengine.ClipRectComponent") {
                    // Lifecycle, transforms and UI layout run in generated Core.
                    continue;
                } else if (component.ComponentTypeId != null && component.ComponentTypeId.StartsWith("helengine.", StringComparison.Ordinal)) {
                    // Other engine audio and physics components have no Dreamcast adapter;
                    // project code components run inside generated Core and are allowed.
                    throw new InvalidOperationException($"Dreamcast supports camera, mesh, viewport, sprite, text and rounded-rectangle engine components in the startup scene; entity '{entity.Name}' carries '{component.ComponentTypeId}'.");
                }
            }
        }
        foreach (SceneEntityAsset child in entity.Children) {
            CountComponents(child, enabled, ref cameraCount, ref meshCount);
        }
    }

    /// <summary>Reads one staged file's bytes after confirming it resolves inside the staging root.</summary>
    /// <param name="root">Absolute staging root.</param>
    /// <param name="relativePath">Normalized path within the staging root.</param>
    /// <returns>The file's bytes.</returns>
    static byte[] ReadStagedFile(string root, string relativePath) {
        string fullPath = Path.GetFullPath(Path.Combine(root, relativePath.Replace('/', Path.DirectorySeparatorChar)));
        if (!fullPath.StartsWith(Path.TrimEndingDirectorySeparator(root) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Cooked reference escapes the staged root: '{relativePath}'.");
        }
        if (!File.Exists(fullPath)) {
            throw new InvalidOperationException($"Startup scene references a cooked asset that is missing from the staged package: '{relativePath}'.");
        }
        return File.ReadAllBytes(fullPath);
    }

    /// <summary>Deserializes one staged file through the engine asset serializer.</summary>
    /// <param name="relativePath">Staged path used in failure messages.</param>
    /// <param name="bytes">Serialized engine asset bytes.</param>
    /// <returns>The deserialized asset.</returns>
    static Asset DeserializeAsset(string relativePath, byte[] bytes) {
        Asset asset;
        try {
            asset = AssetSerializer.DeserializeFromBytes(bytes);
        } catch (Exception exception) when (exception is not InvalidOperationException) {
            throw new InvalidOperationException($"Cooked asset '{relativePath}' could not be deserialized: {exception.Message}", exception);
        }
        return asset ?? throw new InvalidOperationException($"Cooked asset '{relativePath}' deserialized to null.");
    }

    /// <summary>Returns whether one staged file begins with the Dreamcast cooked texture magic.</summary>
    /// <param name="bytes">Staged file bytes.</param>
    /// <returns><c>true</c> when the file is a <c>dc-texture-v1</c> payload.</returns>
    static bool HasCookedTextureMagic(byte[] bytes) {
        return bytes.Length >= 4
            && BitConverter.ToUInt32(bytes, 0) == DreamcastTextureSchemaIds.Magic;
    }

    /// <summary>
    /// Validates one staged <c>dc-texture-v1</c> payload against the rules the runtime parser enforces, so a
    /// payload the console would refuse at load fails on the developer's machine instead.
    /// </summary>
    /// <param name="bytes">Staged cooked texture bytes, already known to carry the <c>DCTX</c> magic.</param>
    /// <param name="path">Staged path used in failure messages.</param>
    /// <returns>Video memory bytes the payload's pixels occupy once uploaded.</returns>
    internal static long ValidateCookedTexture(byte[] bytes, string path) {
        if (bytes.Length < DreamcastTextureSchemaIds.HeaderBytes) {
            throw new InvalidOperationException(
                $"Cooked texture '{path}' is {bytes.Length} bytes, shorter than the {DreamcastTextureSchemaIds.HeaderBytes}-byte {DreamcastTextureSchemaIds.FormatId} header.");
        }
        ushort version = BitConverter.ToUInt16(bytes, 4);
        if (version != DreamcastTextureSchemaIds.Version && version != 2) {
            throw new InvalidOperationException(
                $"Cooked texture '{path}' declares unsupported version {version}; the runtime loads versions 1 and 2.");
        }
        ushort flags = BitConverter.ToUInt16(bytes, 6);
        if (version == 1 && flags != 0) {
            throw new InvalidOperationException($"Cooked texture '{path}' sets reserved flags 0x{flags:X4}; the runtime rejects a nonzero flags field.");
        }
        ushort paddedSide = BitConverter.ToUInt16(bytes, 8);
        if (paddedSide < DreamcastTextureSchemaIds.MinimumSide ||
            paddedSide > DreamcastTextureSchemaIds.MaximumSide ||
            (paddedSide & (paddedSide - 1)) != 0) {
            throw new InvalidOperationException(
                $"Cooked texture '{path}' has padded side {paddedSide}; it must be a power of two between {DreamcastTextureSchemaIds.MinimumSide} and {DreamcastTextureSchemaIds.MaximumSide}.");
        }
        ushort usedWidth = BitConverter.ToUInt16(bytes, 10);
        ushort usedHeight = BitConverter.ToUInt16(bytes, 12);
        if (usedWidth < 1 || usedHeight < 1 || usedWidth > paddedSide || usedHeight > paddedSide) {
            throw new InvalidOperationException(
                $"Cooked texture '{path}' records a used extent of {usedWidth}x{usedHeight}, which does not fit inside its {paddedSide} padded side.");
        }
        // The reserved halfword at offset 14 is deliberately not checked: the version field is the compatibility
        // mechanism, and enforcing reserved bytes would turn any future use of them into a breaking change.
        long pixelBytes = (long)paddedSide * paddedSide * 2L;
        int paletteBytes = 0;
        if (version == 2) {
            DreamcastTextureFormat format = DreamcastTextureFormatCatalog.ResolveFlags(flags);
            if (BitConverter.ToUInt16(bytes, 14) != format.PaletteEntries) throw new InvalidOperationException($"Cooked texture '{path}' has an invalid palette count.");
            pixelBytes = format.GetPixelBytes(paddedSide);
            paletteBytes = format.PaletteEntries * 4;
        }
        if (bytes.LongLength != DreamcastTextureSchemaIds.HeaderBytes + pixelBytes + paletteBytes) {
            throw new InvalidOperationException(
                $"Cooked texture '{path}' is {bytes.LongLength} bytes; a {paddedSide} square {(version == 1 ? "ARGB4444" : "native PVR")} payload is {DreamcastTextureSchemaIds.HeaderBytes + pixelBytes + paletteBytes}.");
        }
        return pixelBytes;
    }

    static void ValidateFontAsset(string root, string path, byte[] bytes) {
        FontAsset font;
        try {
            using MemoryStream stream = new(bytes, writable: false);
            font = helengine.files.FontAssetBinarySerializer.Deserialize(stream);
        } catch (Exception exception) when (exception is not OutOfMemoryException) {
            throw new InvalidOperationException($"Cooked font '{path}' could not be deserialized: {exception.Message}", exception);
        }

        using (font) {
            if (font.FontInfo == null || font.Characters == null || font.Characters.Count == 0 || font.Characters.Count > 256) {
                throw new InvalidOperationException($"Cooked font '{path}' must contain font metrics and between 1 and 256 glyphs.");
            }
            if (font.AtlasWidth < 1 || font.AtlasWidth > DreamcastTextureSchemaIds.MaximumSide || font.AtlasHeight < 1 || font.AtlasHeight > DreamcastTextureSchemaIds.MaximumSide ||
                !RealEvaluation.IsFinite(font.LineHeight) || font.LineHeight <= 0f ||
                !RealEvaluation.IsFinite(font.FontInfo.SpaceWidth) || font.FontInfo.SpaceWidth < 0f) {
                throw new InvalidOperationException($"Cooked font '{path}' has invalid Dreamcast atlas dimensions or layout metrics.");
            }

            string atlasPath = DreamcastSceneCompatibilityValidator.NormalizeRelativePath(
                font.CookedAtlasTextureRelativePath, $"Font '{path}' atlas path");
            byte[] atlas = ReadStagedFile(root, atlasPath);
            ValidateCookedTexture(atlas, atlasPath);
            ushort atlasWidth = BitConverter.ToUInt16(atlas, 10);
            ushort atlasHeight = BitConverter.ToUInt16(atlas, 12);
            if (font.AtlasWidth != atlasWidth || font.AtlasHeight != atlasHeight) {
                throw new InvalidOperationException(
                    $"Cooked font '{path}' records a {font.AtlasWidth}x{font.AtlasHeight} atlas, but '{atlasPath}' contains {atlasWidth}x{atlasHeight} pixels.");
            }
        }
    }

    static void ValidateModel(ModelAsset model, string path) {
        int positionCount = model.Positions?.Length ?? 0;
        if (positionCount < 1 || positionCount > MaximumPositions) {
            throw new InvalidOperationException($"Model '{path}' has {positionCount} positions; Dreamcast v0.2 supports 1 to {MaximumPositions}.");
        }
        bool has16 = model.Indices16 != null && model.Indices16.Length != 0;
        bool has32 = model.Indices32 != null && model.Indices32.Length != 0;
        if (has16 == has32) {
            throw new InvalidOperationException($"Model '{path}' must declare exactly one nonempty index buffer.");
        }
        int indexCount = has16 ? model.Indices16.Length : model.Indices32.Length;
        if (indexCount % 3 != 0 || indexCount / 3 < 1 || indexCount / 3 > MaximumTriangles) {
            throw new InvalidOperationException($"Model '{path}' has {indexCount / 3} triangles; Dreamcast v0.2 supports 1 to {MaximumTriangles} complete triangles.");
        }
        for (int index = 0; index < indexCount; index++) {
            uint value = has16 ? model.Indices16[index] : model.Indices32[index];
            if (value >= (uint)positionCount) {
                throw new InvalidOperationException($"Model '{path}' index {index} references position {value} outside the {positionCount} positions.");
            }
        }
        // The engine resolves a model without explicit submeshes as one implicit submesh that
        // covers every index (ModelSubmeshResolver); the engine's built-in cube is authored that
        // way. Dreamcast accepts that form or exactly one explicit covering submesh.
        if (model.Submeshes != null && model.Submeshes.Length != 0) {
            if (model.Submeshes.Length != 1 || model.Submeshes[0] == null) {
                throw new InvalidOperationException($"Model '{path}' must contain at most one submesh; found {model.Submeshes.Length}.");
            }
            ModelSubmeshAsset submesh = model.Submeshes[0];
            if (submesh.IndexStart != 0 || submesh.IndexCount != indexCount) {
                throw new InvalidOperationException($"Model '{path}' submesh must cover the complete index buffer.");
            }
        }
        foreach (float3 position in model.Positions) {
            if (!RealEvaluation.IsFinite(position.X) || !RealEvaluation.IsFinite(position.Y) || !RealEvaluation.IsFinite(position.Z)) {
                throw new InvalidOperationException($"Model '{path}' contains a non-finite position.");
            }
        }
    }

    static void ValidateMaterial(PlatformMaterialAsset material, string path) {
        if (!string.Equals(material.RendererFamilyId, RendererFamilyId, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"Material '{path}' targets renderer family '{material.RendererFamilyId}' instead of '{RendererFamilyId}'.");
        }
        if (!string.IsNullOrEmpty(material.TextureRelativePath)) {
            throw new InvalidOperationException($"Material '{path}' binds a texture; Dreamcast v0.2 supports untextured materials only.");
        }
        if (material.UseVertexColor || material.DoubleSided) {
            throw new InvalidOperationException($"Material '{path}' uses vertex colors or double-sided rendering, which Dreamcast v0.2 does not support.");
        }
        if (material.BaseColorA != byte.MaxValue) {
            throw new InvalidOperationException($"Material '{path}' is not opaque (alpha {material.BaseColorA}); Dreamcast v0.2 supports opaque materials only.");
        }
    }
}
