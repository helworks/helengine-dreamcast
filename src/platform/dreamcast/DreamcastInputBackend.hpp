#pragma once

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "IInputBackend.hpp"

namespace helengine::dreamcast {

/// Supplies a deterministic empty frame until the native Maple adapter exists.
/// The generated Core contract permits an input backend with no connected devices.
class DreamcastInputBackend final : public ::IInputBackend {
public:
    ::InputFrameState CaptureFrame() override;
};

} // namespace helengine::dreamcast

#endif
