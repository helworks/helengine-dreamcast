#pragma once

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "DreamcastGeometryPipeline.hpp"

#include "ModelAsset.hpp"
#include "RuntimeModel.hpp"

namespace helengine::dreamcast {

/// Owns the converted, bounded native mesh arrays and the generated asset
/// retained by RuntimeModel for the generated lifetime contract.
class DreamcastRuntimeModel final : public ::RuntimeModel {
public:
    /// Takes ownership of the deserialized asset after validation succeeds.
    static DreamcastRuntimeModel* CreateOwned(::ModelAsset* asset);

    ~DreamcastRuntimeModel() override;

    void Dispose() override;

    MeshView meshView() const noexcept;

private:
    DreamcastRuntimeModel();

    SourceVertex* vertices_ = nullptr;
    TriangleIndex* triangles_ = nullptr;
    std::uint16_t vertexCount_ = 0;
    std::uint16_t triangleCount_ = 0;
    bool hasNormals_ = false;
    bool disposed_ = false;
};

} // namespace helengine::dreamcast

#endif
