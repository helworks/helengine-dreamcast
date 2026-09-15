#include "DreamcastBootHost.hpp"

#include "DreamcastCubeMesh.hpp"

#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <kos/dbglog.h>
#include <kos/timer.h>

#include <cmath>
#include <cstdint>

namespace helengine::dreamcast {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kVerticalFovRadians = 1.10f;
constexpr float kNearPlane = 0.5f;
constexpr float kYawRadiansPerSecond = 0.70f;
constexpr float kPitchRadiansPerSecond = 0.43f;

float secondsBetween(std::uint64_t newer, std::uint64_t older) noexcept {
    return static_cast<float>(newer - older) * 1.0e-9f;
}

Matrix4x4 cameraMatrix(float yaw, float pitch, float x, float y, float z) noexcept {
    const float cy = std::cos(yaw);
    const float sy = std::sin(yaw);
    const float cp = std::cos(pitch);
    const float sp = std::sin(pitch);

    // Row-major Rx(pitch) * Ry(yaw), followed by a camera-space translation.
    return Matrix4x4{{
        {cy, 0.0f, sy, x},
        {sp * sy, cp, -sp * cy, y},
        {-cp * sy, sp, cp * cy, z},
        {0.0f, 0.0f, 0.0f, 1.0f},
    }};
}

bool startPressed() noexcept {
    maple_device_t* controller = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (controller == nullptr) {
        return false;
    }
    const cont_state_t* state =
        static_cast<const cont_state_t*>(maple_dev_status(controller));
    return state != nullptr && (state->buttons & CONT_START) != 0;
}

void makeScreenTriangle(const VideoState& video, GeometryFrame& frame) noexcept {
    frame = GeometryFrame{};
    const float width = static_cast<float>(video.width);
    const float height = static_cast<float>(video.height);
    frame.triangleCount = 1;
    frame.emittedVertexCount = 3;
    frame.triangles[0].vertices[0] =
        ProjectedVertex{width * 0.5f, height * 0.20f, 1.0f, {1.0f, 0.1f, 0.1f, 1.0f}};
    frame.triangles[0].vertices[1] =
        ProjectedVertex{width * 0.25f, height * 0.75f, 1.0f, {0.1f, 1.0f, 0.1f, 1.0f}};
    frame.triangles[0].vertices[2] =
        ProjectedVertex{width * 0.75f, height * 0.75f, 1.0f, {0.1f, 0.2f, 1.0f, 1.0f}};
}

GeometryStatus makeGeometry(const HostConfig& config,
                             const VideoState& video,
                             float yaw,
                             float pitch,
                             GeometryFrame& frame) noexcept {
    if (config.scene == HostScene::ClearOnly) {
        frame = GeometryFrame{};
        return GeometryStatus::Ok;
    }
    if (config.scene == HostScene::ScreenTriangle) {
        makeScreenTriangle(video, frame);
        return GeometryStatus::Ok;
    }

    const float x = config.screenEdgeProbe ? 3.8f : 0.0f;
    const float z = config.nearPlaneProbe ? 1.20f : 6.0f;
    const GeometryConfig geometryConfig{
        static_cast<float>(video.width),
        static_cast<float>(video.height),
        kVerticalFovRadians,
        kNearPlane,
    };
    return processGeometry(cubeMesh(), cameraMatrix(yaw, pitch, x, 0.0f, z),
                           geometryConfig, frame);
}

const char* sceneName(HostScene scene) noexcept {
    switch (scene) {
    case HostScene::ClearOnly:
        return "clear";
    case HostScene::ScreenTriangle:
        return "triangle";
    case HostScene::Cube:
        return "cube";
    }
    return "unknown";
}

} // namespace

int runDreamcastHost(const HostConfig& config) noexcept {
    DreamcastVideo video;
    VideoState videoState{};
    if (!video.initialize(VideoConfig{}, videoState)) {
        dbglog(DBG_ERROR, "DreamcastHost: video initialization failed\n");
        return 1;
    }
    dbglog(DBG_INFO, "DreamcastHost: scene=%s mode=%ux%u refresh=%uHz\n",
           sceneName(config.scene), static_cast<unsigned>(videoState.width),
           static_cast<unsigned>(videoState.height),
           static_cast<unsigned>(videoState.refreshHz));

    DreamcastRasterRenderer renderer;
    const RendererConfig rendererConfig{config.disableCulling, config.reverseTriangleOrder};
    if (!renderer.initialize(videoState, rendererConfig)) {
        dbglog(DBG_ERROR, "DreamcastHost: renderer initialization failed\n");
        video.shutdown();
        return 2;
    }

    float yaw = 0.55f;
    float pitch = -0.35f;
    const std::uint64_t startTime = timer_ns_gettime64();
    std::uint64_t previousTime = startTime;
    bool running = true;
    int result = 0;
    bool reportedGeometry = false;

    while (running) {
        const std::uint64_t now = timer_ns_gettime64();
        const float rawDeltaSeconds = secondsBetween(now, previousTime);
        const float deltaSeconds = renderer_detail::clampSimulationDelta(rawDeltaSeconds);
        previousTime = now;

        if (config.animate) {
            yaw += kYawRadiansPerSecond * deltaSeconds;
            pitch += kPitchRadiansPerSecond * deltaSeconds;
            if (yaw > 2.0f * kPi || yaw < -2.0f * kPi) {
                yaw = std::fmod(yaw, 2.0f * kPi);
            }
            if (pitch > 2.0f * kPi || pitch < -2.0f * kPi) {
                pitch = std::fmod(pitch, 2.0f * kPi);
            }
        }

        GeometryFrame geometry{};
        const GeometryStatus status = makeGeometry(config, videoState, yaw, pitch, geometry);
        if (status != GeometryStatus::Ok) {
            dbglog(DBG_ERROR, "DreamcastHost: geometry stage failed (%u)\n",
                   static_cast<unsigned>(status));
            result = 3;
            break;
        }
        if (!renderer.renderFrame(geometry)) {
            dbglog(DBG_ERROR, "DreamcastHost: renderer submission failed\n");
            result = 4;
            break;
        }
        if (!reportedGeometry) {
            dbglog(DBG_INFO, "DreamcastHost: first geometry ready (%u triangles, %u clipped)\n",
                   static_cast<unsigned>(geometry.triangleCount),
                   static_cast<unsigned>(geometry.clippedTriangleCount));
            reportedGeometry = true;
        }

        if (startPressed()) {
            dbglog(DBG_INFO, "DreamcastHost: Start requested\n");
            running = false;
        }
        if (config.stopAfterSeconds > 0.0f &&
            secondsBetween(now, startTime) >= config.stopAfterSeconds) {
            dbglog(DBG_INFO, "DreamcastHost: timed stop reached\n");
            running = false;
        }
    }

    renderer.shutdown();
    video.shutdown();
    dbglog(result == 0 ? DBG_INFO : DBG_ERROR, "DreamcastHost: shutdown complete\n");
    return result;
}

} // namespace helengine::dreamcast
