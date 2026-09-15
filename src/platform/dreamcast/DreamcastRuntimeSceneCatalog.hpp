#pragma once

#include <cstddef>

/// Stable C ABI shared by the probe catalog and the Task 4 startup catalog.
struct HEDcSceneCatalogEntry {
    const char* sceneId;
    const char* cookedRelativePath;
};

extern "C" const char* he_dc_startup_scene_id();
extern "C" const HEDcSceneCatalogEntry* he_dc_scene_catalog_entries(std::size_t* count);

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

class RuntimeSceneCatalog;

namespace helengine::dreamcast {

/// Returns the deliberately empty catalog used only by RuntimeInitProbe.
/// Authored builds must replace it with a validated non-empty Task 4 catalog.
::RuntimeSceneCatalog* createProbeSceneCatalog();

} // namespace helengine::dreamcast

#endif
