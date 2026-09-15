#pragma once

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include <cstddef>
#include <cstdint>

namespace helengine::dreamcast {

/// Versioned runtime-only probe state. This is separate from the v0.1
/// renderer diagnostic snapshot and exists so an emulator-side probe can
/// distinguish a successful Core lifecycle from a process reset.
struct DreamcastRuntimeProbeSnapshot {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t status;
    std::uint32_t errorCode;
    std::uint32_t stage;
    std::uint32_t startFreeHeap;
    std::uint32_t coreReadyFreeHeap;
    std::uint32_t coreDisposedFreeHeap;
    std::uint32_t shutdownFreeHeap;
    std::uint32_t heapLimit;
    std::uint32_t currentBreak;
    std::uint32_t untouchedHeap;
    std::uint32_t allocatorArena;
    std::uint32_t allocatorAllocated;
    std::uint32_t allocatorFree;
    std::uint32_t checkedFreeHeap;
    std::uint32_t coreReady;
    std::uint32_t frameComplete;
    std::uint32_t coreDisposed;
    std::uint32_t cleanShutdown;
};

static_assert(sizeof(DreamcastRuntimeProbeSnapshot) == 80,
              "runtime probe snapshot layout must remain stable");
static_assert(offsetof(DreamcastRuntimeProbeSnapshot, startFreeHeap) == 20,
              "runtime probe heap layout changed");

extern "C" {
extern volatile DreamcastRuntimeProbeSnapshot helengine_dreamcast_runtime_probe;
}

constexpr std::uint32_t kDreamcastRuntimeProbeMagic = 0x48455250u; // "HERP"
constexpr std::uint32_t kDreamcastRuntimeProbeVersion = 1u;

/// Versioned authored-scene diagnostic state. It is a separate symbol from
/// both the v0.1 renderer snapshot and the runtime probe snapshot so neither
/// existing ABI changes. An emulator-side reader resolves its address per ELF.
struct DreamcastAuthoredSnapshot {
    std::uint32_t magic;
    std::uint32_t version;
    std::uint32_t stage;
    std::uint32_t errorCode;
    std::uint32_t sceneDigest;
    std::uint32_t cameraCount;
    std::uint32_t drawableCount;
    std::uint32_t submittedTriangles;
    std::uint32_t clippedTriangles;
    std::uint32_t rejectedTriangles;
    std::uint32_t presentedFrames;
    std::uint32_t firstPresentedFrame;
    std::uint32_t clearArgb;
    std::uint32_t checkedFreeHeap;
    std::uint32_t loopFrames;
    std::uint32_t reserved;
};

static_assert(sizeof(DreamcastAuthoredSnapshot) == 64,
              "authored snapshot layout must remain stable");

extern "C" {
extern volatile DreamcastAuthoredSnapshot helengine_dreamcast_authored_snapshot;
}

constexpr std::uint32_t kDreamcastAuthoredSnapshotMagic = 0x48454153u; // "HEAS"
constexpr std::uint32_t kDreamcastAuthoredSnapshotVersion = 1u;

enum DreamcastAuthoredStage : std::uint32_t {
    kAuthoredStageBooting = 0,
    kAuthoredStageVideoReady = 1,
    kAuthoredStageRendererReady = 2,
    kAuthoredStageCoreReady = 3,
    kAuthoredStageSceneReady = 4,
    kAuthoredStageRunning = 5,
    kAuthoredStageDrained = 6,
    kAuthoredStageSceneUnloaded = 7,
    kAuthoredStageCoreDisposed = 8,
    kAuthoredStageShutdown = 9,
};

enum DreamcastAuthoredError : std::uint32_t {
    kAuthoredErrorNone = 0,
    kAuthoredErrorVideoInitialization = 1,
    kAuthoredErrorRendererInitialization = 2,
    kAuthoredErrorGeneratedException = 3,
    kAuthoredErrorNativeException = 4,
    kAuthoredErrorUnknownException = 5,
    kAuthoredErrorSceneUnload = 6,
};

/// Initializes the generated Core, performs one empty frame, and shuts down.
/// This is an explicit compatibility probe; it is never an authored-scene pass.
int runDreamcastRuntimeInitProbe() noexcept;

/// Boots the normal authored runtime, loads the manifest startup scene from /rd,
/// and enters the bounded native update loop used by Gate B.
int runDreamcastAuthoredScene() noexcept;

} // namespace helengine::dreamcast

#endif
