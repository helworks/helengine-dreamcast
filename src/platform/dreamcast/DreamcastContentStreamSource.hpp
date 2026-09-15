#pragma once

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include <string>

#include "IContentStreamSource.hpp"
#include "system/io/file-stream.hpp"

namespace helengine::dreamcast {

/// Bridges generated Core content reads to the KOS ROMFS mounted at /rd.
class DreamcastContentStreamSource final : public ::IContentStreamSource {
public:
    explicit DreamcastContentStreamSource(std::string contentRootPath = "/rd/");

    ::Stream* OpenRead(std::string assetPath) override;

    /// File-system seam used by generated File::OpenRead for /rd paths.
    static bool CanHandlePath(const char* path);
    static bool Exists(const char* path);
    static ::FileStream* OpenRead(const char* path);

private:
    std::string contentRootPath_;

    static std::string normalizeRootPath(const std::string& path);
    std::string buildAssetPath(const std::string& assetPath) const;
};

} // namespace helengine::dreamcast

#endif
