#include "DreamcastInputBackend.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "InputFrameState.hpp"
#include "InputGamepadState.hpp"
#include "runtime/native_list.hpp"
#include <dc/maple.h>
#include <dc/maple/controller.h>

namespace helengine::dreamcast {

DreamcastInputBackend::DreamcastInputBackend() {
    gamepadFrames_[0] = new ::Array<::InputGamepadState>(4);
    gamepadFrames_[1] = new ::Array<::InputGamepadState>(4);
}

::InputFrameState DreamcastInputBackend::CaptureFrame() {
    auto* states = gamepadFrames_[nextFrame_];
    for (int32_t index = 0; index < 4; ++index) {
        maple_device_t* device = maple_enum_type(index, MAPLE_FUNC_CONTROLLER);
        const cont_state_t* pad = device != nullptr
            ? static_cast<const cont_state_t*>(maple_dev_status(device)) : nullptr;
        ::InputGamepadState state{};
        state.set_Connected(pad != nullptr);
        if (pad != nullptr) {
            std::uint64_t buttons = 0;
            const auto map = [&](std::uint32_t native, unsigned core) {
                if ((pad->buttons & native) != 0) buttons |= std::uint64_t{1} << core;
            };
            map(CONT_A, 0); map(CONT_B, 1); map(CONT_X, 2); map(CONT_Y, 3);
            map(CONT_DPAD_UP, 10); map(CONT_DPAD_DOWN, 11);
            map(CONT_DPAD_LEFT, 12); map(CONT_DPAD_RIGHT, 13); map(CONT_START, 14);
            if (pad->ltrig > 64) buttons |= std::uint64_t{1} << 8;
            if (pad->rtrig > 64) buttons |= std::uint64_t{1} << 9;
            state.set_Buttons(buttons);
            state.set_LeftStickX(static_cast<std::int16_t>(pad->joyx * 256));
            state.set_LeftStickY(static_cast<std::int16_t>(-pad->joyy * 255));
            state.set_LeftTrigger(static_cast<std::int16_t>(pad->ltrig * 128));
            state.set_RightTrigger(static_cast<std::int16_t>(pad->rtrig * 128));
        }
        (*states)[index] = state;
    }
    ::InputFrameState frame{};
    frame.set_Gamepads(states); frame.set_GamepadCount(4);
    nextFrame_ ^= 1u;
    return frame;
}

} // namespace helengine::dreamcast

#endif
