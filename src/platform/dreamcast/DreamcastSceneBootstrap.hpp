#pragma once

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include <cstddef>
#include <string>

#include "RuntimeSceneCatalog.hpp"
#include "RuntimeSceneCatalogEntry.hpp"
#include "runtime/array.hpp"

namespace helengine::dreamcast {

/// Owns the generated catalog and the individual entry objects copied by it.
struct DreamcastSceneCatalogHandle {
    ::RuntimeSceneCatalog* catalog = nullptr;
    ::Array<::RuntimeSceneCatalogEntry*>* entries = nullptr;
    std::string startupSceneId;

    DreamcastSceneCatalogHandle() = default;
    DreamcastSceneCatalogHandle(const DreamcastSceneCatalogHandle&) = delete;
    DreamcastSceneCatalogHandle& operator=(const DreamcastSceneCatalogHandle&) = delete;
    DreamcastSceneCatalogHandle(DreamcastSceneCatalogHandle&& other) noexcept;
    DreamcastSceneCatalogHandle& operator=(DreamcastSceneCatalogHandle&& other) noexcept;
    ~DreamcastSceneCatalogHandle();

    /// Disposes the generated catalog and then its independently owned entries.
    void dispose() noexcept;
};

/// Converts the external C catalog ABI into the generated RuntimeSceneCatalog.
DreamcastSceneCatalogHandle createAuthoredSceneCatalog();

} // namespace helengine::dreamcast

#endif
