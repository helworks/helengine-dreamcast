#include "DreamcastRasterRenderer.hpp"

#include "DreamcastVideo.hpp"

#include <dc/pvr.h>
#include <kos/dbglog.h>
#include <kos/timer.h>

#include <cstddef>
#include <cstring>

namespace helengine::dreamcast {

extern "C" {
volatile DreamcastDiagnosticSnapshot helengine_dreamcast_diagnostics
    __attribute__((used)) = {
        renderer_detail::kDiagnosticMagic,
        renderer_detail::kDiagnosticVersion,
        kDiagnosticBooting,
        kDiagnosticErrorNone,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
    };
}

namespace {

static_assert(sizeof(pvr_vertex_t) == sizeof(renderer_detail::VertexPacket),
              "renderer packet must match KOS pvr_vertex_t");
static_assert(alignof(pvr_vertex_t) == alignof(renderer_detail::VertexPacket),
              "renderer packet alignment must match KOS pvr_vertex_t");

constexpr std::size_t kConservativeVertexBufferBytes = 64u * 1024u;
constexpr std::uint32_t kLogEveryFrames = 300u;

void writeCounter(volatile std::uint32_t& low,
                  volatile std::uint32_t& high,
                  std::uint64_t value) noexcept {
    high = static_cast<std::uint32_t>(value >> 32);
    low = static_cast<std::uint32_t>(value & 0xffffffffu);
}

std::uint64_t readCounter(const volatile std::uint32_t& low,
                          const volatile std::uint32_t& high) noexcept {
    return (static_cast<std::uint64_t>(high) << 32) | low;
}

bool finite(const ColorRgba& color) noexcept {
    return std::isfinite(color.r) && std::isfinite(color.g) &&
           std::isfinite(color.b) && std::isfinite(color.a);
}

bool finite(const ProjectedVertex& vertex) noexcept {
    return std::isfinite(vertex.x) && std::isfinite(vertex.y) &&
           std::isfinite(vertex.z) && vertex.z > 0.0f && finite(vertex.color);
}

bool validGeometryFrame(const GeometryFrame& geometry) noexcept {
    if (geometry.overflow || geometry.triangleCount > GeometryFrame::kMaxTriangles ||
        geometry.emittedVertexCount != geometry.triangleCount * 3u) {
        return false;
    }
    for (std::uint16_t i = 0; i < geometry.triangleCount; ++i) {
        for (const ProjectedVertex& vertex : geometry.triangles[i].vertices) {
            if (!finite(vertex)) {
                return false;
            }
        }
    }
    return true;
}

float clampUnit(float value) noexcept {
    if (!std::isfinite(value) || value <= 0.0f) {
        return 0.0f;
    }
    return value >= 1.0f ? 1.0f : value;
}

std::uint32_t toUint32(std::size_t value) noexcept {
    constexpr std::size_t kMax = 0xffffffffu;
    return static_cast<std::uint32_t>(value > kMax ? kMax : value);
}

} // namespace

namespace renderer_detail {

void resetDiagnosticSnapshot() noexcept {
    volatile DreamcastDiagnosticSnapshot& snapshot = helengine_dreamcast_diagnostics;
    snapshot.magic = kDiagnosticMagic;
    snapshot.version = kDiagnosticVersion;
    snapshot.status = kDiagnosticBooting;
    snapshot.errorCode = kDiagnosticErrorNone;
    snapshot.frameCount = 0;
    snapshot.submittedTriangles = 0;
    snapshot.submittedVertices = 0;
    snapshot.clippedTriangles = 0;
    snapshot.frameIntervalNanosecondsLow = 0;
    snapshot.frameIntervalNanosecondsHigh = 0;
    snapshot.geometryNanosecondsLow = 0;
    snapshot.geometryNanosecondsHigh = 0;
    snapshot.submissionNanosecondsLow = 0;
    snapshot.submissionNanosecondsHigh = 0;
    snapshot.waitReadyNanosecondsLow = 0;
    snapshot.waitReadyNanosecondsHigh = 0;
    snapshot.availableVramBytes = 0;
    snapshot.parameterBufferHighWaterBytes = 0;
    snapshot.shutdownResult = 0;
    snapshot.reserved = 0;
}

void setDiagnosticError(std::uint32_t errorCode) noexcept {
    helengine_dreamcast_diagnostics.errorCode = errorCode;
    helengine_dreamcast_diagnostics.status = kDiagnosticError;
}

void setDiagnosticFrameInterval(std::uint64_t nanoseconds) noexcept {
    writeCounter(helengine_dreamcast_diagnostics.frameIntervalNanosecondsLow,
                 helengine_dreamcast_diagnostics.frameIntervalNanosecondsHigh, nanoseconds);
}

void setDiagnosticGeometryTime(std::uint64_t nanoseconds) noexcept {
    writeCounter(helengine_dreamcast_diagnostics.geometryNanosecondsLow,
                 helengine_dreamcast_diagnostics.geometryNanosecondsHigh, nanoseconds);
}

void setDiagnosticShutdownResult(std::uint32_t result) noexcept {
    helengine_dreamcast_diagnostics.shutdownResult = result;
    helengine_dreamcast_diagnostics.status = kDiagnosticShutdown;
}

} // namespace renderer_detail

bool DreamcastRasterRenderer::initialize(const VideoState& video,
                                         const RendererConfig& config) noexcept {
    if (initialized_ || !video.initialized || video.width == 0 || video.height == 0) {
        return false;
    }

    // KOS's pvr_default_params has 512 KiB of vertex storage and enables the
    // translucent list. Copy it, then keep only the opaque list with a small
    // fixed buffer suitable for this renderer. PVR direct pvr_prim() submission
    // is used below; no DMA or store-queue calls are issued by the application.
    pvr_init_params_t params = pvr_default_params;
    params.opb_sizes[0] = PVR_BINSIZE_16;
    params.opb_sizes[1] = PVR_BINSIZE_0;
    params.opb_sizes[2] = PVR_BINSIZE_0;
    params.opb_sizes[3] = PVR_BINSIZE_0;
    params.opb_sizes[4] = PVR_BINSIZE_0;
    params.vertex_buf_size = static_cast<int>(kConservativeVertexBufferBytes);
    params.dma_enabled = 0;
    params.fsaa_enabled = 0;
    params.autosort_disabled = 0;
    params.opb_overflow_count = 1;
    params.vbuf_doublebuf_disabled = 0;

    if (pvr_init(&params) != 0) {
        dbglog(DBG_ERROR, "DreamcastRasterRenderer: pvr_init failed\n");
        renderer_detail::setDiagnosticError(kDiagnosticErrorRendererInitialization);
        return false;
    }

    pvr_set_bg_color(0.015f, 0.020f, 0.040f);

    pvr_poly_cxt_t context{};
    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    // The geometry pipeline flips pixel Y. A fixed pose proves that the near
    // -Z green face is the counterclockwise screen winding for KOS; use
    // PVR_CULLING_CCW to cull the opposite back-facing winding. Diagnostics
    // can disable only culling.
    context.gen.culling =
        config.disableCulling
            ? PVR_CULLING_NONE
            : (config.frontFaceWinding ==
                       renderer_detail::CullWinding::CounterClockwise
                   ? PVR_CULLING_CCW
                   : PVR_CULLING_CW);
    context.gen.shading = PVR_SHADE_GOURAUD;
    context.gen.clip_mode = PVR_USERCLIP_DISABLE;
    context.gen.specular = false;
    context.gen.fog_type = PVR_FOG_DISABLE;
    context.gen.color_clamp = false;
    context.gen.alpha = false;
    context.fmt.color = PVR_CLRFMT_ARGBPACKED;
    context.fmt.uv = false;
    context.fmt.modifier = false;
    context.depth.comparison = PVR_DEPTHCMP_GREATER;
    context.depth.write = PVR_DEPTHWRITE_ENABLE;
    context.blend.src = PVR_BLEND_ONE;
    context.blend.dst = PVR_BLEND_ZERO;
    context.blend.src_enable = false;
    context.blend.dst_enable = false;
    context.txr.enable = false;

    // pvr_poly_hdr_t is a 32-byte cache-line object and pvr_poly_compile()
    // allocates/writes its cache line. Clear the complete storage first, then
    // compile one immutable opaque header for every frame.
    std::memset(cachedHeader_, 0, sizeof(cachedHeader_));
    pvr_poly_compile(reinterpret_cast<pvr_poly_hdr_t*>(cachedHeader_), &context);

    config_ = config;
    diagnostics_ = RendererDiagnostics{};
    frameCounter_ = 0;
    initialized_ = true;
    dbglog(DBG_INFO, "DreamcastRasterRenderer: PVR initialized (%ux%u, culling=%s)\n",
           static_cast<unsigned>(video.width), static_cast<unsigned>(video.height),
           config.disableCulling
               ? "off"
               : (config.frontFaceWinding == renderer_detail::CullWinding::CounterClockwise
                      ? "ccw"
                      : "cw"));
    return true;
}

bool DreamcastRasterRenderer::renderFrame(const GeometryFrame& geometry) noexcept {
    return renderFrame(geometry, kDiagnosticClearColor);
}

bool DreamcastRasterRenderer::waitIdle() noexcept {
    if (!initialized_) {
        return true;
    }
    bool ok = true;
    if (pvr_wait_render_done() != 0) {
        dbglog(DBG_WARNING, "DreamcastRasterRenderer: idle render drain timed out\n");
        ok = false;
    }
    if (pvr_wait_ready() != 0) {
        dbglog(DBG_WARNING, "DreamcastRasterRenderer: idle ready drain timed out\n");
        ok = false;
    }
    return ok;
}

bool DreamcastRasterRenderer::renderFrame(const GeometryFrame& geometry,
                                          const ColorRgba& clearColor) noexcept {
    if (!initialized_ || !validGeometryFrame(geometry) || !finite(clearColor)) {
        renderer_detail::setDiagnosticError(kDiagnosticErrorSubmission);
        return false;
    }

    // Validate the entire frame before opening a scene. This guarantees that a
    // malformed pointer/index result can never leave a partially submitted TA
    // scene. The geometry pipeline already stages atomically; this is the native
    // boundary check for diagnostics and future callers.
    const std::uint64_t frameStart = timer_ns_gettime64();
    const std::uint64_t waitStart = frameStart;
    if (pvr_wait_ready() != 0) {
        dbglog(DBG_ERROR, "DreamcastRasterRenderer: pvr_wait_ready failed\n");
        renderer_detail::setDiagnosticError(kDiagnosticErrorSubmission);
        return false;
    }
    const std::uint64_t waitEnd = timer_ns_gettime64();
    const std::uint64_t submissionStart = waitEnd;

    // The background color is the authored clear for this frame. It is set
    // after wait-ready so it cannot apply to a scene that is still rendering.
    pvr_set_bg_color(clampUnit(clearColor.r), clampUnit(clearColor.g), clampUnit(clearColor.b));
    pvr_scene_begin();
    bool sceneStarted = true;
    if (pvr_list_begin(PVR_LIST_OP_POLY) != 0) {
        dbglog(DBG_ERROR, "DreamcastRasterRenderer: pvr_list_begin failed\n");
        pvr_scene_finish();
        renderer_detail::setDiagnosticError(kDiagnosticErrorSubmission);
        return false;
    }
    bool listStarted = true;

    bool ok = pvr_prim(cachedHeader_, sizeof(cachedHeader_)) == 0;
    const std::uint16_t count = geometry.triangleCount;
    for (std::uint16_t ordinal = 0; ok && ordinal < count; ++ordinal) {
        const std::uint16_t triangleIndex =
            config_.reverseTriangleOrder ? static_cast<std::uint16_t>(count - ordinal - 1)
                                          : ordinal;
        const ProjectedTriangle& triangle = geometry.triangles[triangleIndex];
        for (std::uint8_t vertexIndex = 0; vertexIndex < 3; ++vertexIndex) {
            const renderer_detail::VertexPacket packet = renderer_detail::makeVertexPacket(
                triangle.vertices[vertexIndex], vertexIndex == 2);
            if (pvr_prim(&packet, sizeof(packet)) != 0) {
                ok = false;
                break;
            }
        }
    }

    if (listStarted && pvr_list_finish() != 0) {
        ok = false;
    }
    listStarted = false;
    if (sceneStarted && pvr_scene_finish() != 0) {
        ok = false;
    }
    sceneStarted = false;
    if (!ok) {
        dbglog(DBG_ERROR, "DreamcastRasterRenderer: scene submission failed\n");
        renderer_detail::setDiagnosticError(kDiagnosticErrorSubmission);
        return false;
    }

    const std::uint64_t frameEnd = timer_ns_gettime64();
    pvr_stats_t stats{};
    const bool statsValid = pvr_get_stats(&stats) == 0;
    if (statsValid) {
        diagnostics_.parameterBufferHighWaterBytes = toUint32(stats.vtx_buffer_used_max);
    }
    diagnostics_.submittedTriangles = geometry.triangleCount;
    diagnostics_.submittedVertices = geometry.emittedVertexCount;
    diagnostics_.clippedTriangles = geometry.clippedTriangleCount;
    diagnostics_.waitReadyNanoseconds = waitEnd - waitStart;
    diagnostics_.lastFrameNanoseconds = frameEnd - frameStart;
    diagnostics_.availableVramBytes = toUint32(pvr_mem_available());
    writeCounter(helengine_dreamcast_diagnostics.submissionNanosecondsLow,
                 helengine_dreamcast_diagnostics.submissionNanosecondsHigh,
                 frameEnd - submissionStart);
    writeCounter(helengine_dreamcast_diagnostics.waitReadyNanosecondsLow,
                 helengine_dreamcast_diagnostics.waitReadyNanosecondsHigh,
                 diagnostics_.waitReadyNanoseconds);
    helengine_dreamcast_diagnostics.frameCount = frameCounter_ + 1;
    helengine_dreamcast_diagnostics.submittedTriangles = geometry.triangleCount;
    helengine_dreamcast_diagnostics.submittedVertices = geometry.emittedVertexCount;
    helengine_dreamcast_diagnostics.clippedTriangles = geometry.clippedTriangleCount;
    helengine_dreamcast_diagnostics.availableVramBytes = diagnostics_.availableVramBytes;
    helengine_dreamcast_diagnostics.parameterBufferHighWaterBytes =
        diagnostics_.parameterBufferHighWaterBytes;
    helengine_dreamcast_diagnostics.status = kDiagnosticFrame;
    ++frameCounter_;
    if (frameCounter_ == 1) {
        dbglog(DBG_INFO, "DreamcastRasterRenderer: first submission (%u triangles)\n",
               static_cast<unsigned>(geometry.triangleCount));
    } else if (frameCounter_ % kLogEveryFrames == 0) {
        const std::uint64_t snapshotFrameInterval = readCounter(
            helengine_dreamcast_diagnostics.frameIntervalNanosecondsLow,
            helengine_dreamcast_diagnostics.frameIntervalNanosecondsHigh);
        const std::uint64_t snapshotGeometry = readCounter(
            helengine_dreamcast_diagnostics.geometryNanosecondsLow,
            helengine_dreamcast_diagnostics.geometryNanosecondsHigh);
        const std::uint64_t snapshotSubmission = readCounter(
            helengine_dreamcast_diagnostics.submissionNanosecondsLow,
            helengine_dreamcast_diagnostics.submissionNanosecondsHigh);
        const std::uint64_t kosFrameMilliseconds =
            statsValid ? stats.frame_last_time / 1000000u : 0u;
        const std::uint64_t registrationMilliseconds =
            statsValid ? stats.reg_last_time / 1000000u : 0u;
        const std::uint64_t renderingMilliseconds =
            statsValid ? stats.rnd_last_time / 1000000u : 0u;
        dbglog(DBG_INFO,
               "DreamcastRasterRenderer: frame=%u tris=%u verts=%u clipped=%u "
               "wall_ms=%llu interval_ms=%llu geometry_ms=%llu submission_ms=%llu "
               "wait_ms=%llu kos_frame_ms=%llu reg_ms=%llu render_ms=%llu vram=%u "
               "param_high=%u\n",
               static_cast<unsigned>(frameCounter_),
               static_cast<unsigned>(diagnostics_.submittedTriangles),
               static_cast<unsigned>(diagnostics_.submittedVertices),
               static_cast<unsigned>(diagnostics_.clippedTriangles),
               static_cast<unsigned long long>(diagnostics_.lastFrameNanoseconds / 1000000u),
               static_cast<unsigned long long>(snapshotFrameInterval / 1000000u),
               static_cast<unsigned long long>(snapshotGeometry / 1000000u),
               static_cast<unsigned long long>(snapshotSubmission / 1000000u),
               static_cast<unsigned long long>(diagnostics_.waitReadyNanoseconds / 1000000u),
               static_cast<unsigned long long>(kosFrameMilliseconds),
               static_cast<unsigned long long>(registrationMilliseconds),
               static_cast<unsigned long long>(renderingMilliseconds),
               static_cast<unsigned>(diagnostics_.availableVramBytes),
               static_cast<unsigned>(diagnostics_.parameterBufferHighWaterBytes));
    }
    return true;
}

RendererDiagnostics DreamcastRasterRenderer::diagnostics() const noexcept {
    return diagnostics_;
}

void DreamcastRasterRenderer::shutdown() noexcept {
    if (!initialized_) {
        return;
    }
    // The scene has already been closed by renderFrame(). Drain both the
    // renderer completion and TA readiness paths before pvr_shutdown().
    if (pvr_wait_render_done() != 0) {
        dbglog(DBG_WARNING, "DreamcastRasterRenderer: render drain timed out\n");
    }
    if (pvr_wait_ready() != 0) {
        dbglog(DBG_WARNING, "DreamcastRasterRenderer: ready drain timed out\n");
    }
    if (pvr_shutdown() != 0) {
        dbglog(DBG_ERROR, "DreamcastRasterRenderer: pvr_shutdown failed\n");
    }
    initialized_ = false;
}

} // namespace helengine::dreamcast
