#include "DreamcastRuntimeSceneCatalog.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "RuntimeSceneCatalog.hpp"
#include "runtime/array.hpp"

#include <stdexcept>

namespace helengine::dreamcast {

::RuntimeSceneCatalog* createProbeSceneCatalog() {
    std::size_t entryCount = 0;
    const HEDcSceneCatalogEntry* abiEntries = he_dc_scene_catalog_entries(&entryCount);
    if (entryCount != 0 || abiEntries != nullptr || he_dc_startup_scene_id() != nullptr) {
        throw std::runtime_error("Dreamcast RuntimeInitProbe requires a zero-entry scene catalog.");
    }

    auto* entries = new ::Array<::RuntimeSceneCatalogEntry*>(0);
    try {
        auto* catalog = new ::RuntimeSceneCatalog(entries);
        delete entries;
        return catalog;
    } catch (...) {
        delete entries;
        throw;
    }
}

} // namespace helengine::dreamcast

#endif
