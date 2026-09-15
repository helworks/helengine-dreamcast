#pragma once

#include "DreamcastRasterRenderer.hpp"
#include "DreamcastVideo.hpp"

#include <cstdint>

namespace helengine::dreamcast {

enum class HostScene : std::uint8_t { ClearOnly, ScreenTriangle, Cube };

struct HostConfig {
    HostScene scene = HostScene::Cube;
    bool animate = true;
    bool disableCulling = false;
    bool reverseTriangleOrder = false;
    bool nearPlaneProbe = false;
    bool screenEdgeProbe = false;
    float stopAfterSeconds = 0.0f;
};

int runDreamcastHost(const HostConfig& config) noexcept;

} // namespace helengine::dreamcast
