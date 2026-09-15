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

namespace boot_detail {

inline void makeScreenTriangleGeometry(float width,
                                       float height,
                                       GeometryFrame& frame) noexcept {
    frame = GeometryFrame{};
    frame.triangleCount = 1;
    frame.emittedVertexCount = 3;
    frame.triangles[0].vertices[0] =
        ProjectedVertex{width * 0.5f, height * 0.20f, 1.0f, {1.0f, 0.1f, 0.1f, 1.0f}};
    // The renderer's front-face contract is positive screen-space winding.
    frame.triangles[0].vertices[1] =
        ProjectedVertex{width * 0.75f, height * 0.75f, 1.0f, {0.1f, 0.2f, 1.0f, 1.0f}};
    frame.triangles[0].vertices[2] =
        ProjectedVertex{width * 0.25f, height * 0.75f, 1.0f, {0.1f, 1.0f, 0.1f, 1.0f}};
}

} // namespace boot_detail

int runDreamcastHost(const HostConfig& config) noexcept;

} // namespace helengine::dreamcast
