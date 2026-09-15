#include "DreamcastRenderManager3D.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include <stdexcept>

namespace helengine::dreamcast {

::RuntimeModel* DreamcastRenderManager3D::BuildModelFromRaw(::ModelAsset*) {
    throw std::runtime_error("Dreamcast 3D raw model support is not implemented.");
}

} // namespace helengine::dreamcast

#endif
