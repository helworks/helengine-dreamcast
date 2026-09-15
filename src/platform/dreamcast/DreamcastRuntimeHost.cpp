#include "DreamcastRuntimeHost.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "Core.hpp"
#include "CoreInitializationOptions.hpp"
#include "DreamcastAuthoredFrame.hpp"
#include "DreamcastContentStreamSource.hpp"
#include "DreamcastInputBackend.hpp"
#include "DreamcastRasterRenderer.hpp"
#include "DreamcastRenderManager2D.hpp"
#include "DreamcastRenderManager3D.hpp"
#include "RuntimeSceneCatalog.hpp"
#include "DreamcastRuntimeSceneCatalog.hpp"
#include "DreamcastSceneBootstrap.hpp"
#include "DreamcastVideo.hpp"
#include "ICamera.hpp"
#include "ObjectManager.hpp"
#include "SceneLoadMode.hpp"
#include "SceneManager.hpp"
#include "PlatformInfo.hpp"
#include "runtime/native_exceptions.hpp"

#include <arch/arch.h>
#include <arch/stack.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <kos/dbglog.h>
#include <kos/mm.h>
#include <kos/thread.h>
#include <kos/timer.h>

#include <cstdint>
#include <cstring>
#include <exception>
#include <malloc.h>

namespace helengine::dreamcast {

extern "C" {
volatile DreamcastRuntimeProbeSnapshot helengine_dreamcast_runtime_probe;
volatile DreamcastAuthoredSnapshot helengine_dreamcast_authored_snapshot;
volatile std::uint32_t helengine_dreamcast_authored_scene_ready;
volatile std::uint32_t helengine_dreamcast_authored_frame_count;
}

namespace {

constexpr std::uint32_t kProbeStatusBooting = 0u;
constexpr std::uint32_t kProbeStatusVideoReady = 1u;
constexpr std::uint32_t kProbeStatusCoreReady = 2u;
constexpr std::uint32_t kProbeStatusFrameComplete = 3u;
constexpr std::uint32_t kProbeStatusCoreDisposed = 4u;
constexpr std::uint32_t kProbeStatusCleanShutdown = 5u;
constexpr std::uint32_t kProbeStatusError = 0xffffffffu;

constexpr std::uint32_t kProbeStageStart = 1u;
constexpr std::uint32_t kProbeStageVideoReady = 2u;
constexpr std::uint32_t kProbeStageCoreReady = 3u;
constexpr std::uint32_t kProbeStageFrameComplete = 4u;
constexpr std::uint32_t kProbeStageCoreDisposed = 5u;
constexpr std::uint32_t kProbeStageShutdown = 6u;

constexpr std::uint32_t kProbeErrorVideoInitialization = 1u;
constexpr std::uint32_t kProbeErrorGeneratedException = 2u;
constexpr std::uint32_t kProbeErrorNativeException = 3u;
constexpr std::uint32_t kProbeErrorUnknownException = 4u;

struct DreamcastHeapSnapshot {
    unsigned long heapLimit;
    unsigned long currentBreak;
    unsigned long untouchedHeap;
    unsigned long allocatorArena;
    unsigned long allocatorAllocated;
    unsigned long allocatorFree;
    unsigned long checkedFreeHeap;
};

void resetProbeSnapshot() noexcept {
    volatile DreamcastRuntimeProbeSnapshot& snapshot = helengine_dreamcast_runtime_probe;
    snapshot.magic = kDreamcastRuntimeProbeMagic;
    snapshot.version = kDreamcastRuntimeProbeVersion;
    snapshot.status = kProbeStatusBooting;
    snapshot.errorCode = 0u;
    snapshot.stage = kProbeStageStart;
    snapshot.startFreeHeap = 0u;
    snapshot.coreReadyFreeHeap = 0u;
    snapshot.coreDisposedFreeHeap = 0u;
    snapshot.shutdownFreeHeap = 0u;
    snapshot.heapLimit = 0u;
    snapshot.currentBreak = 0u;
    snapshot.untouchedHeap = 0u;
    snapshot.allocatorArena = 0u;
    snapshot.allocatorAllocated = 0u;
    snapshot.allocatorFree = 0u;
    snapshot.checkedFreeHeap = 0u;
    snapshot.coreReady = 0u;
    snapshot.frameComplete = 0u;
    snapshot.coreDisposed = 0u;
    snapshot.cleanShutdown = 0u;
    helengine_dreamcast_authored_scene_ready = 0u;
    helengine_dreamcast_authored_frame_count = 0u;
}

void resetAuthoredSnapshot() noexcept {
    volatile DreamcastAuthoredSnapshot& snapshot = helengine_dreamcast_authored_snapshot;
    snapshot.magic = kDreamcastAuthoredSnapshotMagic;
    snapshot.version = kDreamcastAuthoredSnapshotVersion;
    snapshot.stage = kAuthoredStageBooting;
    snapshot.errorCode = kAuthoredErrorNone;
    snapshot.sceneDigest = 0u;
    snapshot.cameraCount = 0u;
    snapshot.drawableCount = 0u;
    snapshot.submittedTriangles = 0u;
    snapshot.clippedTriangles = 0u;
    snapshot.rejectedTriangles = 0u;
    snapshot.presentedFrames = 0u;
    snapshot.firstPresentedFrame = 0u;
    snapshot.clearArgb = 0u;
    snapshot.checkedFreeHeap = 0u;
    snapshot.loopFrames = 0u;
    snapshot.reserved = 0u;
}

void setAuthoredStage(std::uint32_t stage) noexcept {
    helengine_dreamcast_authored_snapshot.stage = stage;
}

void setAuthoredError(std::uint32_t errorCode) noexcept {
    if (helengine_dreamcast_authored_snapshot.errorCode == kAuthoredErrorNone) {
        helengine_dreamcast_authored_snapshot.errorCode = errorCode;
    }
}

std::uint32_t fnv1a(const char* text) noexcept {
    std::uint32_t hash = 2166136261u;
    for (const char* cursor = text; cursor != nullptr && *cursor != '\0'; ++cursor) {
        hash ^= static_cast<std::uint8_t>(*cursor);
        hash *= 16777619u;
    }
    return hash;
}

void publishAuthoredFrame(const AuthoredFrameSummary& summary,
                          std::uint32_t presentedFrames,
                          std::uint32_t loopFrames) noexcept {
    volatile DreamcastAuthoredSnapshot& snapshot = helengine_dreamcast_authored_snapshot;
    snapshot.cameraCount = summary.cameraCount;
    snapshot.drawableCount = summary.drawableCount;
    snapshot.submittedTriangles = summary.triangleCount;
    snapshot.clippedTriangles = summary.clippedTriangleCount;
    snapshot.rejectedTriangles = summary.rejectedTriangleCount;
    snapshot.presentedFrames = presentedFrames;
    snapshot.loopFrames = loopFrames;
    snapshot.clearArgb = renderer_detail::packColor(summary.clearColor);
    if (snapshot.firstPresentedFrame == 0u && presentedFrames != 0u) {
        snapshot.firstPresentedFrame = loopFrames;
    }
}

bool authoredStartPressed() noexcept {
    maple_device_t* controller = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if (controller == nullptr) {
        return false;
    }
    const cont_state_t* state = static_cast<const cont_state_t*>(maple_dev_status(controller));
    return state != nullptr && (state->buttons & CONT_START) != 0;
}

DreamcastHeapSnapshot captureHeapSnapshot() {
    const std::uintptr_t heapLimit = static_cast<std::uintptr_t>(_arch_mem_top) - THD_KERNEL_STACK_SIZE;
    const std::uintptr_t currentBreak = reinterpret_cast<std::uintptr_t>(mm_sbrk(0));
    const std::uintptr_t untouchedHeap = currentBreak < heapLimit ? heapLimit - currentBreak : 0;
    const struct mallinfo allocator = mallinfo();
    const unsigned long allocatorFree = allocator.fordblks > 0
        ? static_cast<unsigned long>(allocator.fordblks)
        : 0ul;

    return DreamcastHeapSnapshot{
        static_cast<unsigned long>(heapLimit),
        static_cast<unsigned long>(currentBreak),
        static_cast<unsigned long>(untouchedHeap),
        allocator.arena > 0 ? static_cast<unsigned long>(allocator.arena) : 0ul,
        allocator.uordblks > 0 ? static_cast<unsigned long>(allocator.uordblks) : 0ul,
        allocatorFree,
        static_cast<unsigned long>(untouchedHeap) + allocatorFree};
}

void publishHeapSnapshot(const char* stage, const DreamcastHeapSnapshot& snapshot) noexcept {
    volatile DreamcastRuntimeProbeSnapshot& probe = helengine_dreamcast_runtime_probe;
    probe.heapLimit = static_cast<std::uint32_t>(snapshot.heapLimit);
    probe.currentBreak = static_cast<std::uint32_t>(snapshot.currentBreak);
    probe.untouchedHeap = static_cast<std::uint32_t>(snapshot.untouchedHeap);
    probe.allocatorArena = static_cast<std::uint32_t>(snapshot.allocatorArena);
    probe.allocatorAllocated = static_cast<std::uint32_t>(snapshot.allocatorAllocated);
    probe.allocatorFree = static_cast<std::uint32_t>(snapshot.allocatorFree);
    probe.checkedFreeHeap = static_cast<std::uint32_t>(snapshot.checkedFreeHeap);
    helengine_dreamcast_authored_snapshot.checkedFreeHeap =
        static_cast<std::uint32_t>(snapshot.checkedFreeHeap);
    if (std::strcmp(stage, "start") == 0) {
        probe.startFreeHeap = static_cast<std::uint32_t>(snapshot.checkedFreeHeap);
        probe.stage = kProbeStageStart;
    } else if (std::strcmp(stage, "core-ready") == 0) {
        probe.coreReadyFreeHeap = static_cast<std::uint32_t>(snapshot.checkedFreeHeap);
        probe.stage = kProbeStageCoreReady;
    } else if (std::strcmp(stage, "core-disposed") == 0) {
        probe.coreDisposedFreeHeap = static_cast<std::uint32_t>(snapshot.checkedFreeHeap);
        probe.stage = kProbeStageCoreDisposed;
    } else if (std::strcmp(stage, "shutdown") == 0) {
        probe.shutdownFreeHeap = static_cast<std::uint32_t>(snapshot.checkedFreeHeap);
        probe.stage = kProbeStageShutdown;
    }
}

void logHeapSnapshot(const char* stage) {
    const DreamcastHeapSnapshot snapshot = captureHeapSnapshot();
    publishHeapSnapshot(stage, snapshot);
    dbglog(DBG_INFO,
           "DreamcastRuntimeProbe: heap stage=%s limit=%lu brk=%lu untouched=%lu arena=%lu uordblks=%lu fordblks=%lu free=%lu\n",
           stage,
           snapshot.heapLimit,
           snapshot.currentBreak,
           snapshot.untouchedHeap,
           snapshot.allocatorArena,
           snapshot.allocatorAllocated,
           snapshot.allocatorFree,
           snapshot.checkedFreeHeap);
}

void setProbeError(std::uint32_t errorCode) noexcept {
    volatile DreamcastRuntimeProbeSnapshot& snapshot = helengine_dreamcast_runtime_probe;
    snapshot.errorCode = errorCode;
    snapshot.status = kProbeStatusError;
    snapshot.stage = 0x80000000u | errorCode;
}

/// Presents assembled authored frames through the PVR raster renderer.
class RasterFrameSink final : public IAuthoredFrameSink {
public:
    explicit RasterFrameSink(DreamcastRasterRenderer& renderer) noexcept : renderer_(renderer) {
    }

    bool present(const GeometryFrame& geometry, const ColorRgba& clearColor) noexcept override {
        return renderer_.renderFrame(geometry, clearColor);
    }

private:
    DreamcastRasterRenderer& renderer_;
};

float secondsBetween(std::uint64_t newer, std::uint64_t older) noexcept {
    return static_cast<float>(newer - older) * 1.0e-9f;
}

} // namespace

int runDreamcastRuntimeInitProbe() noexcept {
    resetProbeSnapshot();
    logHeapSnapshot("start");
    DreamcastVideo video;
    VideoState videoState{};
    if (!video.initialize(VideoConfig{}, videoState)) {
        dbglog(DBG_ERROR, "DreamcastRuntimeProbe: video initialization failed\n");
        setProbeError(kProbeErrorVideoInitialization);
        return 1;
    }

    ::Core* core = nullptr;
    ::CoreInitializationOptions* options = nullptr;
    ::RuntimeSceneCatalog* sceneCatalog = nullptr;
    ::PlatformInfo* platformInfo = nullptr;
    DreamcastContentStreamSource content;
    DreamcastInputBackend input;
    DreamcastRenderManager3D render3D;
    DreamcastRenderManager2D render2D;
    int result = 0;

    try {
        sceneCatalog = createProbeSceneCatalog();
        options = new ::CoreInitializationOptions();
        options->ContentStreamSource = &content;
        options->SceneCatalog = sceneCatalog;
        options->UpdateListInitialCapacity = 16;
        options->RenderList2DInitialCapacity = 16;
        options->RenderList3DInitialCapacity = 16;
        options->DefaultUpdateDeltaSeconds = 1.0 / 60.0;
        options->PhysicsFixedStepSeconds = 1.0 / 60.0;
        options->PhysicsMaxStepsPerUpdate = 2;
        options->CommitPendingSceneOperationsDuringDraw = false;
        platformInfo = new ::PlatformInfo("dc", "0.2.0");
        render3D.AddWindow(0, static_cast<int32_t>(videoState.width),
                           static_cast<int32_t>(videoState.height));
        core = new ::Core(options);
        core->Initialize(&render3D, &render2D, &input, platformInfo, options);
        helengine_dreamcast_runtime_probe.status = kProbeStatusVideoReady;
        helengine_dreamcast_runtime_probe.stage = kProbeStageVideoReady;
        dbglog(DBG_INFO, "DreamcastRuntimeProbe: core-ready platform=%s version=%s\n",
               platformInfo->Name.c_str(), platformInfo->Version.c_str());
        helengine_dreamcast_runtime_probe.coreReady = 1u;
        helengine_dreamcast_runtime_probe.status = kProbeStatusCoreReady;
        logHeapSnapshot("core-ready");
        core->Update(0.0);
        core->Draw();
        core->CompleteFrameBoundary();
        helengine_dreamcast_runtime_probe.frameComplete = 1u;
        helengine_dreamcast_runtime_probe.status = kProbeStatusFrameComplete;
        helengine_dreamcast_runtime_probe.stage = kProbeStageFrameComplete;
        core->Dispose();
        helengine_dreamcast_runtime_probe.coreDisposed = 1u;
        helengine_dreamcast_runtime_probe.status = kProbeStatusCoreDisposed;
        logHeapSnapshot("core-disposed");
    } catch (::Exception* exception) {
        dbglog(DBG_ERROR, "DreamcastRuntimeProbe: generated exception: %s\n",
               exception == nullptr ? "unknown" : exception->what());
        delete exception;
        setProbeError(kProbeErrorGeneratedException);
        result = 2;
        if (core != nullptr) {
            try {
                core->Dispose();
            } catch (...) {
            }
        }
    } catch (const std::exception& exception) {
        dbglog(DBG_ERROR, "DreamcastRuntimeProbe: native exception: %s\n", exception.what());
        setProbeError(kProbeErrorNativeException);
        result = 2;
        if (core != nullptr) {
            try {
                core->Dispose();
            } catch (...) {
            }
        }
    } catch (...) {
        dbglog(DBG_ERROR, "DreamcastRuntimeProbe: generated Core initialization failed\n");
        setProbeError(kProbeErrorUnknownException);
        result = 2;
        if (core != nullptr) {
            try {
                core->Dispose();
            } catch (...) {
            }
        }
    }

    delete core;
    delete platformInfo;
    delete options;
    if (sceneCatalog != nullptr) {
        sceneCatalog->Dispose();
        delete sceneCatalog;
    }
    video.shutdown();
    logHeapSnapshot("shutdown");
    if (result == 0) {
        helengine_dreamcast_runtime_probe.cleanShutdown = 1u;
        helengine_dreamcast_runtime_probe.status = kProbeStatusCleanShutdown;
        helengine_dreamcast_runtime_probe.stage = kProbeStageShutdown;
        dbglog(DBG_INFO, "DreamcastRuntimeProbe: clean-shutdown\n");
    }
    // Keep the completed state available to an emulator-side reader for a
    // bounded interval after all native cleanup has run and before main
    // returns to the KOS launcher.
    thd_sleep(2000);
    return result;
}

int runDreamcastAuthoredScene() noexcept {
    resetProbeSnapshot();
    resetAuthoredSnapshot();
    renderer_detail::resetDiagnosticSnapshot();
    logHeapSnapshot("start");
    DreamcastVideo video;
    VideoState videoState{};
    if (!video.initialize(VideoConfig{}, videoState)) {
        dbglog(DBG_ERROR, "DreamcastAuthored: video initialization failed\n");
        setProbeError(kProbeErrorVideoInitialization);
        setAuthoredError(kAuthoredErrorVideoInitialization);
        return 1;
    }
    setAuthoredStage(kAuthoredStageVideoReady);

    // The generated camera-to-native Z reflection reverses projected winding,
    // so authored front faces are clockwise on screen. Diagnostic keeps its
    // own counterclockwise contract through runDreamcastHost.
    DreamcastRasterRenderer renderer;
    RendererConfig rendererConfig{};
    rendererConfig.frontFaceWinding = renderer_detail::CullWinding::Clockwise;
    if (!renderer.initialize(videoState, rendererConfig)) {
        dbglog(DBG_ERROR, "DreamcastAuthored: renderer initialization failed\n");
        video.shutdown();
        setProbeError(kProbeErrorVideoInitialization);
        setAuthoredError(kAuthoredErrorRendererInitialization);
        return 1;
    }
    setAuthoredStage(kAuthoredStageRendererReady);
    RasterFrameSink frameSink(renderer);

    ::Core* core = nullptr;
    ::CoreInitializationOptions* options = nullptr;
    ::PlatformInfo* platformInfo = nullptr;
    DreamcastSceneCatalogHandle sceneCatalog;
    DreamcastContentStreamSource content;
    DreamcastInputBackend input;
    DreamcastRenderManager3D render3D;
    DreamcastRenderManager2D render2D;
    int result = 0;
    bool coreDisposed = false;
    bool sceneLoaded = false;
    ::SceneManager* sceneManager = nullptr;
    std::uint32_t authoredFrameCount = 0u;

    try {
        sceneCatalog = createAuthoredSceneCatalog();
        helengine_dreamcast_authored_snapshot.sceneDigest = fnv1a(sceneCatalog.startupSceneId.c_str());
        options = new ::CoreInitializationOptions();
        options->ContentStreamSource = &content;
        options->SceneCatalog = sceneCatalog.catalog;
        options->UpdateListInitialCapacity = 16;
        options->RenderList2DInitialCapacity = 16;
        options->RenderList3DInitialCapacity = 16;
        options->DefaultUpdateDeltaSeconds = 1.0 / 60.0;
        options->PhysicsFixedStepSeconds = 1.0 / 60.0;
        options->PhysicsMaxStepsPerUpdate = 2;
        options->CommitPendingSceneOperationsDuringDraw = false;
        // Generated runtime startup metadata is 1.0.0; the builder contract
        // remains 0.2.0 and the required engine version remains 0.4.0.
        platformInfo = new ::PlatformInfo("dc", "1.0.0");
        render3D.AddWindow(0, static_cast<int32_t>(videoState.width),
                           static_cast<int32_t>(videoState.height));
        render3D.installFrameSink(&frameSink);
        core = new ::Core(options);
        core->Initialize(&render3D, &render2D, &input, platformInfo, options);
        helengine_dreamcast_runtime_probe.status = kProbeStatusCoreReady;
        helengine_dreamcast_runtime_probe.stage = kProbeStageCoreReady;
        helengine_dreamcast_runtime_probe.coreReady = 1u;
        setAuthoredStage(kAuthoredStageCoreReady);
        logHeapSnapshot("core-ready");

        sceneManager = core->get_SceneManager();
        ::ObjectManager* objectManager = core->get_ObjectManager();
        if (sceneManager == nullptr || objectManager == nullptr) {
            throw std::runtime_error("Dreamcast authored Core did not expose scene/object managers.");
        }
        sceneManager->LoadScene(sceneCatalog.startupSceneId, ::SceneLoadMode::Single);
        core->Update(0.0);
        core->CompleteFrameBoundary();
        const int32_t loadedScenes = sceneManager->get_LoadedScenes()->get_Count();
        const int32_t cameras = objectManager->get_Cameras()->get_Count();
        const int32_t drawables3D = objectManager->get_Drawables3D()->get_Count();
        const int32_t models = sceneManager->get_ActiveOwnedModelReferenceCount();
        const int32_t materials = sceneManager->get_ActiveOwnedMaterialReferenceCount();
        if (loadedScenes != 1 || cameras != 1 || drawables3D != 1 || models != 1 || materials != 1) {
            throw std::runtime_error("Dreamcast startup scene cardinality did not match the v0.2 contract.");
        }
        sceneLoaded = true;
        ::ICamera* camera = objectManager->get_Cameras()->get_Item(0);
        if (camera == nullptr) {
            throw std::runtime_error("Dreamcast startup scene camera was null.");
        }
        ::float4 clear = camera->get_ClearSettings().get_ClearColor();
        dbglog(DBG_INFO,
               "DreamcastAuthored: scene=%s loaded=%d cameras=%d drawables3d=%d models=%d materials=%d near=%f far=%f clear=%f,%f,%f,%f\n",
               sceneCatalog.startupSceneId.c_str(), static_cast<int>(loadedScenes), static_cast<int>(cameras),
               static_cast<int>(drawables3D), static_cast<int>(models), static_cast<int>(materials),
               static_cast<double>(camera->get_NearPlaneDistance()), static_cast<double>(camera->get_FarPlaneDistance()),
               static_cast<double>(clear.X), static_cast<double>(clear.Y), static_cast<double>(clear.Z), static_cast<double>(clear.W));
        helengine_dreamcast_authored_scene_ready = 1u;
        setAuthoredStage(kAuthoredStageSceneReady);

        // One lifecycle per frame: monotonic bounded delta -> Core.Update ->
        // Core.Draw (authored gather, geometry, one PVR scene) -> pending scene
        // commit at the explicit boundary. renderFrame paces on PVR readiness.
        bool running = true;
        bool reportedFirstFrame = false;
        std::uint64_t previousTime = timer_ns_gettime64();
        setAuthoredStage(kAuthoredStageRunning);
        while (running) {
            const std::uint64_t now = timer_ns_gettime64();
            const float deltaSeconds =
                renderer_detail::clampSimulationDelta(secondsBetween(now, previousTime));
            previousTime = now;
            core->Update(static_cast<double>(deltaSeconds));
            core->Draw();
            core->CompleteFrameBoundary();
            ++authoredFrameCount;
            helengine_dreamcast_authored_frame_count = authoredFrameCount;
            helengine_dreamcast_runtime_probe.frameComplete = 1u;
            helengine_dreamcast_runtime_probe.status = kProbeStatusFrameComplete;
            helengine_dreamcast_runtime_probe.stage = kProbeStageFrameComplete;
            publishAuthoredFrame(render3D.lastFrameSummary(), render3D.presentedFrameCount(),
                                 authoredFrameCount);
            if (!reportedFirstFrame) {
                const AuthoredFrameSummary& summary = render3D.lastFrameSummary();
                dbglog(DBG_INFO,
                       "DreamcastAuthored: first frame cameras=%u drawables=%u triangles=%u clipped=%u rejected=%u clear=%08lx\n",
                       static_cast<unsigned>(summary.cameraCount),
                       static_cast<unsigned>(summary.drawableCount),
                       static_cast<unsigned>(summary.triangleCount),
                       static_cast<unsigned>(summary.clippedTriangleCount),
                       static_cast<unsigned>(summary.rejectedTriangleCount),
                       static_cast<unsigned long>(renderer_detail::packColor(summary.clearColor)));
                reportedFirstFrame = true;
            }
            if ((authoredFrameCount % 60u) == 0u) {
                logHeapSnapshot("running");
            }
            if (authoredStartPressed()) {
                dbglog(DBG_INFO, "DreamcastAuthored: Start requested\n");
                running = false;
            }
#if defined(HELENGINE_DREAMCAST_AUTHORED_TEST_FRAMES)
            if (helengine_dreamcast_authored_frame_count >= HELENGINE_DREAMCAST_AUTHORED_TEST_FRAMES) {
                running = false;
            }
#endif
        }
    } catch (::Exception* exception) {
        dbglog(DBG_ERROR, "DreamcastAuthored: generated exception: %s\n",
               exception == nullptr ? "unknown" : exception->what());
        delete exception;
        setProbeError(kProbeErrorGeneratedException);
        setAuthoredError(kAuthoredErrorGeneratedException);
        result = 2;
    } catch (const std::exception& exception) {
        dbglog(DBG_ERROR, "DreamcastAuthored: native exception: %s\n", exception.what());
        setProbeError(kProbeErrorNativeException);
        setAuthoredError(kAuthoredErrorNativeException);
        result = 2;
    } catch (...) {
        dbglog(DBG_ERROR, "DreamcastAuthored: startup failed\n");
        setProbeError(kProbeErrorUnknownException);
        setAuthoredError(kAuthoredErrorUnknownException);
        result = 2;
    }

    // Wait for the PVR to finish the last submitted scene before any owned
    // model or material storage can be released by the scene unload below.
    if (!renderer.waitIdle()) {
        dbglog(DBG_WARNING, "DreamcastAuthored: renderer drain did not complete cleanly\n");
    }
    setAuthoredStage(kAuthoredStageDrained);

    if (core != nullptr && sceneLoaded && sceneManager != nullptr) {
        try {
            sceneManager->UnloadScene(sceneCatalog.startupSceneId);
            core->Update(0.0);
            core->CompleteFrameBoundary();
            if (sceneManager->get_LoadedScenes()->get_Count() != 0 ||
                sceneManager->get_ActiveOwnedModelReferenceCount() != 0 ||
                sceneManager->get_ActiveOwnedMaterialReferenceCount() != 0) {
                throw std::runtime_error("Dreamcast authored scene unload did not release all owned references.");
            }
            sceneLoaded = false;
            setAuthoredStage(kAuthoredStageSceneUnloaded);
        } catch (...) {
            setAuthoredError(kAuthoredErrorSceneUnload);
            result = result == 0 ? 2 : result;
        }
    }
    if (core != nullptr && !coreDisposed) {
        try {
            core->Dispose();
            coreDisposed = true;
            helengine_dreamcast_runtime_probe.coreDisposed = 1u;
            helengine_dreamcast_runtime_probe.status = kProbeStatusCoreDisposed;
            setAuthoredStage(kAuthoredStageCoreDisposed);
            logHeapSnapshot("core-disposed");
        } catch (...) {
            result = result == 0 ? 2 : result;
        }
    }
    delete core;
    delete platformInfo;
    delete options;
    sceneCatalog.dispose();
    render3D.installFrameSink(nullptr);
    renderer.shutdown();
    video.shutdown();
    logHeapSnapshot("shutdown");
    if (result == 0) {
        helengine_dreamcast_runtime_probe.cleanShutdown = 1u;
        helengine_dreamcast_runtime_probe.status = kProbeStatusCleanShutdown;
        helengine_dreamcast_runtime_probe.stage = kProbeStageShutdown;
        setAuthoredStage(kAuthoredStageShutdown);
        dbglog(DBG_INFO, "DreamcastAuthored: clean-shutdown frames=%u presented=%u\n",
               static_cast<unsigned>(helengine_dreamcast_authored_frame_count),
               static_cast<unsigned>(helengine_dreamcast_authored_snapshot.presentedFrames));
    } else {
        setProbeError(helengine_dreamcast_runtime_probe.errorCode == 0u
                          ? kProbeErrorUnknownException
                          : helengine_dreamcast_runtime_probe.errorCode);
        dbglog(DBG_ERROR, "DreamcastAuthored: shutdown with error stage=%lu error=%lu\n",
               static_cast<unsigned long>(helengine_dreamcast_authored_snapshot.stage),
               static_cast<unsigned long>(helengine_dreamcast_authored_snapshot.errorCode));
    }
    return result;
}

} // namespace helengine::dreamcast

#endif
