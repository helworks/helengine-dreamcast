#include "DreamcastSceneBootstrap.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "DreamcastRuntimeSceneCatalog.hpp"

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace helengine::dreamcast {
namespace {

void disposeEntries(::Array<::RuntimeSceneCatalogEntry*>* entries) noexcept {
    if (entries == nullptr) {
        return;
    }
    for (int32_t index = 0; index < entries->get_Length(); ++index) {
        delete (*entries)[index];
        (*entries)[index] = nullptr;
    }
    delete entries;
}

} // namespace

DreamcastSceneCatalogHandle::DreamcastSceneCatalogHandle(DreamcastSceneCatalogHandle&& other) noexcept
    : catalog(other.catalog), entries(other.entries), startupSceneId(std::move(other.startupSceneId)) {
    other.catalog = nullptr;
    other.entries = nullptr;
}

DreamcastSceneCatalogHandle& DreamcastSceneCatalogHandle::operator=(DreamcastSceneCatalogHandle&& other) noexcept {
    if (this != &other) {
        dispose();
        catalog = other.catalog;
        entries = other.entries;
        startupSceneId = std::move(other.startupSceneId);
        other.catalog = nullptr;
        other.entries = nullptr;
    }
    return *this;
}

DreamcastSceneCatalogHandle::~DreamcastSceneCatalogHandle() {
    dispose();
}

void DreamcastSceneCatalogHandle::dispose() noexcept {
    if (catalog != nullptr) {
        catalog->Dispose();
        delete catalog;
        catalog = nullptr;
    }
    if (entries != nullptr) {
        disposeEntries(entries);
        entries = nullptr;
    }
    startupSceneId.clear();
}

DreamcastSceneCatalogHandle createAuthoredSceneCatalog() {
    std::size_t count = 0;
    const HEDcSceneCatalogEntry* abiEntries = he_dc_scene_catalog_entries(&count);
    const char* startupSceneId = he_dc_startup_scene_id();
    if (startupSceneId == nullptr || *startupSceneId == '\0') {
        throw std::runtime_error("Dreamcast authored catalog did not provide a startup scene id.");
    }
    if (count == 0 || abiEntries == nullptr || count > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())) {
        throw std::runtime_error("Dreamcast authored catalog must contain a bounded non-empty entry list.");
    }

    DreamcastSceneCatalogHandle result;
    result.startupSceneId = startupSceneId;
    result.entries = new ::Array<::RuntimeSceneCatalogEntry*>(static_cast<int32_t>(count));
    try {
        bool startupFound = false;
        for (std::size_t index = 0; index < count; ++index) {
            const HEDcSceneCatalogEntry& source = abiEntries[index];
            if (source.sceneId == nullptr || *source.sceneId == '\0' ||
                source.cookedRelativePath == nullptr || *source.cookedRelativePath == '\0') {
                throw std::runtime_error("Dreamcast authored catalog contains a blank entry.");
            }
            if (std::string(source.sceneId) == result.startupSceneId) {
                startupFound = true;
            }
            (*result.entries)[static_cast<int32_t>(index)] =
                new ::RuntimeSceneCatalogEntry(source.sceneId, source.cookedRelativePath);
        }
        if (!startupFound) {
            throw std::runtime_error("Dreamcast authored catalog startup id is not present in its entries.");
        }
        result.catalog = new ::RuntimeSceneCatalog(result.entries);
        return result;
    } catch (...) {
        result.dispose();
        throw;
    }
}

} // namespace helengine::dreamcast

#endif
