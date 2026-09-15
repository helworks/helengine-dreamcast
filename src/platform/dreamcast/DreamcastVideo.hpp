#pragma once

#include <cstdint>

namespace helengine::dreamcast {

struct VideoConfig {
    std::uint16_t requestedWidth = 640;
    std::uint16_t requestedHeight = 480;
};

struct VideoState {
    std::uint16_t width = 0;
    std::uint16_t height = 0;
    std::uint32_t refreshHz = 0;
    bool initialized = false;
};

class DreamcastVideo {
public:
    bool initialize(const VideoConfig& config, VideoState& state) noexcept;
    void shutdown() noexcept;

private:
    bool initialized_ = false;
};

} // namespace helengine::dreamcast
