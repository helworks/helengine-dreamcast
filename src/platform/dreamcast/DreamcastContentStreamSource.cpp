#include "DreamcastContentStreamSource.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "DreamcastContentPath.hpp"

#include <cstdio>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace helengine::dreamcast {
namespace {

std::string checkedRomPath(const char* path) {
    if (path == nullptr || std::string(path).rfind("/rd/", 0) != 0) {
        throw std::invalid_argument("Dreamcast file-system hook requires an /rd/ path.");
    }
    return normalizeContentPath(std::string(path).substr(4));
}

::FileStream* readRomFile(const std::string& path) {
    constexpr std::size_t kMaxContentFileBytes = 1024u * 1024u;
    using FileHandle = std::unique_ptr<std::FILE, decltype(&std::fclose)>;
    FileHandle file(std::fopen(path.c_str(), "rb"), &std::fclose);
    if (file == nullptr) {
        throw std::runtime_error("Dreamcast ROMFS file open failed: " + path);
    }
    if (std::fseek(file.get(), 0, SEEK_END) != 0) {
        throw std::runtime_error("Dreamcast ROMFS file seek failed: " + path);
    }
    const long end = std::ftell(file.get());
    if (end < 0 || static_cast<unsigned long long>(end) > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("Dreamcast ROMFS file length failed: " + path);
    }
    const std::size_t length = static_cast<std::size_t>(end);
    if (length > kMaxContentFileBytes) {
        throw std::runtime_error("Dreamcast ROMFS file exceeds the 1 MiB cooked payload limit: " + path);
    }
    if (std::fseek(file.get(), 0, SEEK_SET) != 0) {
        throw std::runtime_error("Dreamcast ROMFS file rewind failed: " + path);
    }
    std::vector<std::uint8_t> bytes(length);
    if (length != 0 && std::fread(bytes.data(), 1, length, file.get()) != length) {
        throw std::runtime_error("Dreamcast ROMFS file read failed: " + path);
    }
    return new ::FileStream(bytes.data(), bytes.size());
}

} // namespace

DreamcastContentStreamSource::DreamcastContentStreamSource(std::string contentRootPath)
    : contentRootPath_(normalizeRootPath(contentRootPath)) {
}

::Stream* DreamcastContentStreamSource::OpenRead(std::string assetPath) {
    return OpenRead(buildAssetPath(assetPath).c_str());
}

bool DreamcastContentStreamSource::CanHandlePath(const char* path) {
    return path != nullptr && std::string(path).rfind("/rd/", 0) == 0;
}

bool DreamcastContentStreamSource::Exists(const char* path) {
    if (!CanHandlePath(path)) {
        return false;
    }
    std::string normalized;
    try {
        normalized = checkedRomPath(path);
    } catch (const std::invalid_argument&) {
        return false;
    }
    std::FILE* file = std::fopen(normalized.c_str(), "rb");
    if (file == nullptr) {
        return false;
    }
    std::fclose(file);
    return true;
}

::FileStream* DreamcastContentStreamSource::OpenRead(const char* path) {
    return readRomFile(checkedRomPath(path));
}

std::string DreamcastContentStreamSource::normalizeRootPath(const std::string& path) {
    if (path != "/rd/") {
        throw std::invalid_argument("Dreamcast content root must be /rd/.");
    }
    return path;
}

std::string DreamcastContentStreamSource::buildAssetPath(const std::string& assetPath) const {
    if (assetPath.rfind("/rd/", 0) == 0) {
        return normalizeContentPath(assetPath.substr(4));
    }
    return normalizeContentPath(assetPath);
}

} // namespace helengine::dreamcast

#endif
