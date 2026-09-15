#include "DreamcastInputBackend.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "InputFrameState.hpp"

namespace helengine::dreamcast {

::InputFrameState DreamcastInputBackend::CaptureFrame() {
    // Keep ownership with the generated frame value. A null Gamepads pointer
    // and zero count are the generated InputSystem's documented empty state.
    return ::InputFrameState();
}

} // namespace helengine::dreamcast

#endif
