#pragma once

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "RenderManager3D.hpp"

namespace helengine::dreamcast {

/// Minimum Core-compatible 3D adapter. Task 3 adds the native mesh/material bridge.
class DreamcastRenderManager3D final : public ::RenderManager3D {
public:
    ::RuntimeModel* BuildModelFromRaw(::ModelAsset* data) override;
};

} // namespace helengine::dreamcast

#endif
