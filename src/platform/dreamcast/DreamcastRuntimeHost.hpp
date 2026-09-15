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

/// Initializes the generated Core, performs one empty frame, and shuts down.
/// This is an explicit compatibility probe; it is never an authored-scene pass.
int runDreamcastRuntimeInitProbe() noexcept;

} // namespace helengine::dreamcast

#endif
