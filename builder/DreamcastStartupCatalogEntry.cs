namespace helengine.dreamcast.builder;

/// <summary>Maps one logical startup-scene id to its cooked runtime path.</summary>
public sealed class DreamcastStartupCatalogEntry {
    /// <summary>Initializes one validated catalog entry.</summary>
    public DreamcastStartupCatalogEntry(string sceneId, string cookedRelativePath) {
        if (string.IsNullOrWhiteSpace(sceneId)) {
            throw new ArgumentException("Scene id is required.", nameof(sceneId));
        }
        if (string.IsNullOrWhiteSpace(cookedRelativePath)) {
            throw new ArgumentException("Cooked relative path is required.", nameof(cookedRelativePath));
        }

        RejectControlCharacters(sceneId, nameof(sceneId));
        RejectControlCharacters(cookedRelativePath, nameof(cookedRelativePath));
        SceneId = sceneId;
        CookedRelativePath = cookedRelativePath;
    }

    /// <summary>Gets the stable logical scene id.</summary>
    public string SceneId { get; }

    /// <summary>Gets the normalized cooked path stored in the ROMFS.</summary>
    public string CookedRelativePath { get; }

    static void RejectControlCharacters(string value, string valueName) {
        if (value.Any(character => character == '\0' || (character < 0x20 && character != '\t') || character == 0x7f)) {
            throw new ArgumentException($"{valueName} contains a control character.", valueName);
        }
    }
}
