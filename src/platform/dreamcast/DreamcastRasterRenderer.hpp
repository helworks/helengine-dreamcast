#pragma once

#include "DreamcastGeometryPipeline.hpp"

#include <cmath>
#include <cstdint>

namespace helengine::dreamcast {

struct VideoState;

struct RendererConfig {
    bool disableCulling = false;
    bool reverseTriangleOrder = false;
};

struct RendererDiagnostics {
    std::uint32_t submittedTriangles = 0;
    std::uint32_t submittedVertices = 0;
    std::uint32_t clippedTriangles = 0;
    std::uint64_t waitReadyNanoseconds = 0;
    std::uint64_t lastFrameNanoseconds = 0;
    std::uint32_t availableVramBytes = 0;
    std::uint32_t parameterBufferHighWaterBytes = 0;
};

namespace renderer_detail {

// These are the fixed KOS TA command words for a generic packed-color vertex.
// Keeping the conversion in a platform-neutral type makes the packet contract
// host-testable without linking the Dreamcast runtime.
constexpr std::uint32_t kVertexCommand = 0xe0000000u;
constexpr std::uint32_t kVertexCommandEol = 0xf0000000u;
constexpr float kMaxSimulationDeltaSeconds = 0.10f;

struct alignas(32) VertexPacket {
    std::uint32_t flags;
    float x;
    float y;
    float z;
    float u;
    float v;
    std::uint32_t argb;
    std::uint32_t oargb;
};

static_assert(sizeof(VertexPacket) == 32, "PVR vertex packet must be 32 bytes");
static_assert(alignof(VertexPacket) == 32, "PVR vertex packet must be 32-byte aligned");

inline std::uint8_t toColorComponent(float component) noexcept {
    if (!std::isfinite(component) || component <= 0.0f) {
        return 0;
    }
    if (component >= 1.0f) {
        return 255;
    }
    return static_cast<std::uint8_t>(component * 255.0f + 0.5f);
}

inline std::uint32_t packColor(const ColorRgba& color) noexcept {
    return (static_cast<std::uint32_t>(toColorComponent(color.a)) << 24) |
           (static_cast<std::uint32_t>(toColorComponent(color.r)) << 16) |
           (static_cast<std::uint32_t>(toColorComponent(color.g)) << 8) |
           static_cast<std::uint32_t>(toColorComponent(color.b));
}

inline VertexPacket makeVertexPacket(const ProjectedVertex& vertex,
                                     bool endOfLine) noexcept {
    VertexPacket packet{};
    packet.flags = endOfLine ? kVertexCommandEol : kVertexCommand;
    packet.x = vertex.x;
    packet.y = vertex.y;
    packet.z = vertex.z;
    packet.u = 0.0f;
    packet.v = 0.0f;
    packet.argb = packColor(vertex.color);
    packet.oargb = 0;
    return packet;
}

inline float clampSimulationDelta(float deltaSeconds) noexcept {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) {
        return 0.0f;
    }
    return deltaSeconds > kMaxSimulationDeltaSeconds ? kMaxSimulationDeltaSeconds
                                                      : deltaSeconds;
}

} // namespace renderer_detail

class DreamcastRasterRenderer {
public:
    bool initialize(const VideoState& video, const RendererConfig& config) noexcept;
    bool renderFrame(const GeometryFrame& geometry) noexcept;
    RendererDiagnostics diagnostics() const noexcept;
    void shutdown() noexcept;

private:
    bool initialized_ = false;
    RendererConfig config_{};
    RendererDiagnostics diagnostics_{};
    std::uint32_t frameCounter_ = 0;
    alignas(32) std::uint32_t cachedHeader_[8]{};
};

} // namespace helengine::dreamcast
