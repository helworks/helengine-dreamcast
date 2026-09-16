#pragma once

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "DreamcastGeometryPipeline.hpp"

#include "PlatformMaterialAsset.hpp"
#include "RuntimeMaterial.hpp"

namespace helengine::dreamcast {

class DreamcastRuntimeMaterial final : public ::RuntimeMaterial {
public:
    /// Copies the validated borrowed platform asset; the caller retains ownership.
    static DreamcastRuntimeMaterial* Create(const ::PlatformMaterialAsset& asset);

    ~DreamcastRuntimeMaterial() override;

    void Dispose() override;

    ColorRgba color() const noexcept;

    /// True when the authored material asks for per-vertex lighting.
    bool lit() const noexcept;

private:
    DreamcastRuntimeMaterial();

    ColorRgba color_{};
    bool disposed_ = false;
    bool lit_ = false;
};

} // namespace helengine::dreamcast

#endif
