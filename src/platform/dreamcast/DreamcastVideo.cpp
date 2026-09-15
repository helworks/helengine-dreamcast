#include "DreamcastVideo.hpp"

#include <dc/video.h>

namespace helengine::dreamcast {

bool DreamcastVideo::initialize(const VideoConfig& config, VideoState& state) noexcept {
    state = VideoState{};
    if (initialized_ || config.requestedWidth != 640 || config.requestedHeight != 480) {
        return false;
    }

    // Generic DM_640x480 asks KOS to select the mode for the detected cable;
    // it is deliberately not a VGA-only mode. KOS's vid_set_mode() is void,
    // so validate the resulting vid_mode fields before declaring success.
    vid_init(DM_640x480, PM_RGB565);
    if (vid_mode == nullptr || vid_mode->width == 0 || vid_mode->height == 0 ||
        vid_mode->width != config.requestedWidth ||
        vid_mode->height != config.requestedHeight || vid_mode->pm != PM_RGB565) {
        vid_shutdown();
        return false;
    }

    state.width = vid_mode->width;
    state.height = vid_mode->height;
    // KOS exposes nominal PAL/NTSC timing through VID_PAL. The mode table does
    // not provide a measured refresh counter, so preserve this documented rate.
    state.refreshHz = (vid_mode->flags & VID_PAL) != 0 ? 50u : 60u;
    state.initialized = true;
    initialized_ = true;
    return true;
}

void DreamcastVideo::shutdown() noexcept {
    if (!initialized_) {
        return;
    }
    vid_shutdown();
    initialized_ = false;
}

} // namespace helengine::dreamcast
