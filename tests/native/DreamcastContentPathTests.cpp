#include "platform/dreamcast/DreamcastContentPath.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace {

bool contentPathRejected(const std::string& path) {
    try {
        (void)helengine::dreamcast::normalizeContentPath(path);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

} // namespace

int main() {
    using helengine::dreamcast::normalizeContentPath;
    assert(normalizeContentPath("assets/models/cube.model") == "/rd/assets/models/cube.model");
    assert(normalizeContentPath("assets\\models\\cube.model") == "/rd/assets/models/cube.model");
    for (const char* path : {"", "../x", "a/../x", "/x", "C:/x", "a//b", "a/./b", "a/"}) {
        assert(contentPathRejected(path));
    }
    const std::string embeddedNul("assets/a\0b", 9);
    assert(contentPathRejected(embeddedNul));
    std::cout << "Dreamcast content-path tests passed\n";
}
