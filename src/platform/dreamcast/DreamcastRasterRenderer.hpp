#pragma once

#include "DreamcastGeometryPipeline.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace helengine::dreamcast {

struct VideoState;

namespace renderer_detail {

enum class CullWinding : std::uint8_t { Clockwise, CounterClockwise };
constexpr CullWinding kDefaultCullWinding = CullWinding::CounterClockwise;

} // namespace renderer_detail

struct RendererConfig {
    bool disableCulling = false;
    bool reverseTriangleOrder = false;
    // The generated camera-to-native Z reflection reverses projected winding
    // for authored meshes. Diagnostic geometry keeps the native positive
    // screen-space contract; authored submission selects clockwise front
    // faces explicitly instead of changing authored indices or flipping X.
    renderer_detail::CullWinding frontFaceWinding = renderer_detail::kDefaultCullWinding;
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

struct DreamcastDiagnosticSnapshot {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t status;
    std::uint32_t errorCode;
    std::uint32_t frameCount;
    std::uint32_t submittedTriangles;
    std::uint32_t submittedVertices;
    std::uint32_t clippedTriangles;
    std::uint32_t frameIntervalNanosecondsLow;
    std::uint32_t frameIntervalNanosecondsHigh;
    std::uint32_t geometryNanosecondsLow;
    std::uint32_t geometryNanosecondsHigh;
    std::uint32_t submissionNanosecondsLow;
    std::uint32_t submissionNanosecondsHigh;
    std::uint32_t waitReadyNanosecondsLow;
    std::uint32_t waitReadyNanosecondsHigh;
    std::uint32_t availableVramBytes;
    std::uint32_t parameterBufferHighWaterBytes;
    std::uint32_t shutdownResult;
    std::uint32_t reserved;
};

static_assert(sizeof(DreamcastDiagnosticSnapshot) == 80,
              "diagnostic snapshot layout must remain stable");
static_assert(offsetof(DreamcastDiagnosticSnapshot, frameIntervalNanosecondsLow) == 32,
              "diagnostic snapshot timing offset changed");
static_assert(offsetof(DreamcastDiagnosticSnapshot, availableVramBytes) == 64,
              "diagnostic snapshot resource offset changed");

enum DiagnosticStatus : std::uint32_t {
    kDiagnosticBooting = 0,
    kDiagnosticInitialized = 1,
    kDiagnosticFrame = 2,
    kDiagnosticError = 3,
    kDiagnosticShutdown = 4,
};

enum DiagnosticErrorCode : std::uint32_t {
    kDiagnosticErrorNone = 0,
    kDiagnosticErrorVideoInitialization = 1,
    kDiagnosticErrorRendererInitialization = 2,
    kDiagnosticErrorGeometry = 3,
    kDiagnosticErrorSubmission = 4,
};

extern "C" {
extern volatile DreamcastDiagnosticSnapshot helengine_dreamcast_diagnostics;
}

namespace renderer_detail {

constexpr std::uint32_t kDiagnosticMagic = 0x48454443u; // "HEDC"
constexpr std::uint32_t kDiagnosticVersion = 1u;

void resetDiagnosticSnapshot() noexcept;
void setDiagnosticError(std::uint32_t errorCode) noexcept;
void setDiagnosticFrameInterval(std::uint64_t nanoseconds) noexcept;
void setDiagnosticGeometryTime(std::uint64_t nanoseconds) noexcept;
void setDiagnosticShutdownResult(std::uint32_t result) noexcept;

} // namespace renderer_detail

/// The diagnostic host's fixed dark clear color; authored frames supply their own.
constexpr ColorRgba kDiagnosticClearColor{0.015f, 0.020f, 0.040f, 1.0f};

class DreamcastRasterRenderer {
public:
    bool initialize(const VideoState& video, const RendererConfig& config) noexcept;
    /// Presents one validated frame with the diagnostic clear color.
    bool renderFrame(const GeometryFrame& geometry) noexcept;
    /// Presents one validated frame, clearing to the given authored color first.
    /// Exactly one scene begin/end pair is issued per call.
    bool renderFrame(const GeometryFrame& geometry, const ColorRgba& clearColor) noexcept;
    /// Blocks until the PVR has finished the last submitted scene, so owned
    /// geometry can be released without a pending render referencing it.
    bool waitIdle() noexcept;
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
