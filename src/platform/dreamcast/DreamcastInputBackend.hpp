#pragma once

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "IInputBackend.hpp"
#include <cstdint>

template <typename T> class Array;
class InputGamepadState;

namespace helengine::dreamcast {

/// Captures native Maple controllers into reusable Core gamepad snapshots.
class DreamcastInputBackend final : public ::IInputBackend {
public:
    DreamcastInputBackend();
    ::InputFrameState CaptureFrame() override;
private:
    ::Array<::InputGamepadState>* gamepadFrames_[2]{};
    std::uint8_t nextFrame_ = 0;
};

} // namespace helengine::dreamcast

#endif
