#include "DreamcastContentPath.hpp"

#include <stdexcept>

namespace helengine::dreamcast {
namespace {

[[noreturn]] void reject(const char* reason) {
    throw std::invalid_argument(reason);
}

} // namespace

std::string normalizeContentPath(const std::string& path) {
    if (path.empty()) {
        reject("Dreamcast content path cannot be empty.");
    }
    if (path.find('\0') != std::string::npos) {
        reject("Dreamcast content path cannot contain an embedded NUL.");
    }

    std::string normalized = path;
    for (char& character : normalized) {
        if (character == '\\') {
            character = '/';
        }
    }
    if (normalized.empty() || normalized.front() == '/' || normalized.find(':') != std::string::npos) {
        reject("Dreamcast content path must be relative and unrooted.");
    }

    std::string result = "/rd/";
    std::size_t segmentStart = 0;
    while (segmentStart < normalized.size()) {
        const std::size_t separator = normalized.find('/', segmentStart);
        const std::size_t segmentLength =
            separator == std::string::npos ? normalized.size() - segmentStart : separator - segmentStart;
        if (segmentLength == 0) {
            reject("Dreamcast content path cannot contain empty segments.");
        }
        const std::string segment = normalized.substr(segmentStart, segmentLength);
        if (segment == "." || segment == "..") {
            reject("Dreamcast content path cannot contain dot segments.");
        }
        result.append(segment);
        if (separator == std::string::npos) {
            break;
        }
        result.push_back('/');
        segmentStart = separator + 1;
        if (segmentStart == normalized.size()) {
            reject("Dreamcast content path cannot end with a separator.");
        }
    }
    return result;
}

} // namespace helengine::dreamcast
