using helengine.files;

namespace helengine.dreamcast.builder;

/// <summary>Validates staged cooked scene, model and material data with the normal engine deserializers before native compilation.</summary>
public sealed class DreamcastCookedContentValidator {
    const int MaximumPositions = 64;
    const int MaximumTriangles = 128;
    const string CameraTypeId = "helengine.CameraComponent";
    const string MeshTypeId = "helengine.MeshComponent";
    const string RendererFamilyId = "dc-unlit";

    /// <summary>Validates the startup scene cardinality and every staged model and material in the ROMFS tree.</summary>
    /// <remarks>
    /// The cooked scene's reference list still carries authored paths while the runtime resolves
    /// component payloads to cooked paths, so every staged file is classified by its deserialized
    /// type instead of by reference. Exact one-model/one-material usage is re-checked at runtime
    /// through the owned reference counts after the startup scene loads.
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
        Asset sceneAsset = ReadAsset(root, scenePath);
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
        if (meshCount != 1) {
            throw new InvalidOperationException($"Dreamcast v0.2 requires exactly one enabled mesh component in the startup scene; found {meshCount}.");
        }

        int modelCount = 0;
        int materialCount = 0;
        foreach (string fullPath in Directory.EnumerateFiles(root, "*", SearchOption.AllDirectories).OrderBy(path => path, StringComparer.Ordinal)) {
            string relativePath = Path.GetRelativePath(root, fullPath).Replace('\\', '/');
            if (string.Equals(relativePath, scenePath, StringComparison.Ordinal)) {
                continue;
            }
            Asset staged = ReadAsset(root, relativePath);
            if (staged is ModelAsset model) {
                ValidateModel(model, relativePath);
                modelCount++;
            } else if (staged is PlatformMaterialAsset material) {
                ValidateMaterial(material, relativePath);
                materialCount++;
            } else {
                throw new InvalidOperationException($"Staged package contains unsupported cooked asset type '{staged.GetType().Name}' at '{relativePath}'.");
            }
        }
        if (modelCount < 1) {
            throw new InvalidOperationException("Staged package contains no model; the startup mesh has nothing to draw.");
        }
        if (materialCount < 1) {
            throw new InvalidOperationException("Staged package contains no dc-unlit material; the startup mesh has no material.");
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
                }
            }
        }
        foreach (SceneEntityAsset child in entity.Children) {
            CountComponents(child, enabled, ref cameraCount, ref meshCount);
        }
    }

    static Asset ReadAsset(string root, string relativePath) {
        string fullPath = Path.GetFullPath(Path.Combine(root, relativePath.Replace('/', Path.DirectorySeparatorChar)));
        if (!fullPath.StartsWith(Path.TrimEndingDirectorySeparator(root) + Path.DirectorySeparatorChar, StringComparison.OrdinalIgnoreCase)) {
            throw new InvalidOperationException($"Cooked reference escapes the staged root: '{relativePath}'.");
        }
        if (!File.Exists(fullPath)) {
            throw new InvalidOperationException($"Startup scene references a cooked asset that is missing from the staged package: '{relativePath}'.");
        }
        Asset asset;
        try {
            asset = AssetSerializer.DeserializeFromBytes(File.ReadAllBytes(fullPath));
        } catch (Exception exception) when (exception is not InvalidOperationException) {
            throw new InvalidOperationException($"Cooked asset '{relativePath}' could not be deserialized: {exception.Message}", exception);
        }
        return asset ?? throw new InvalidOperationException($"Cooked asset '{relativePath}' deserialized to null.");
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
        if (model.Submeshes == null || model.Submeshes.Length != 1 || model.Submeshes[0] == null) {
            throw new InvalidOperationException($"Model '{path}' must contain exactly one submesh.");
        }
        ModelSubmeshAsset submesh = model.Submeshes[0];
        if (submesh.IndexStart != 0 || submesh.IndexCount != indexCount || string.IsNullOrWhiteSpace(submesh.MaterialSlotName)) {
            throw new InvalidOperationException($"Model '{path}' submesh must cover the complete index buffer with a named material slot.");
        }
        foreach (float3 position in model.Positions) {
            if (!float.IsFinite(position.X) || !float.IsFinite(position.Y) || !float.IsFinite(position.Z)) {
                throw new InvalidOperationException($"Model '{path}' contains a non-finite position.");
            }
        }
    }

    static void ValidateMaterial(PlatformMaterialAsset material, string path) {
        if (!string.Equals(material.RendererFamilyId, RendererFamilyId, StringComparison.Ordinal)) {
            throw new InvalidOperationException($"Material '{path}' targets renderer family '{material.RendererFamilyId}' instead of '{RendererFamilyId}'.");
        }
        if (material.Lit) {
            throw new InvalidOperationException($"Material '{path}' is lit; Dreamcast v0.2 supports unlit materials only.");
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
