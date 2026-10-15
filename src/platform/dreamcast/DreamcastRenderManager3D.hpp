#pragma once

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "DreamcastAuthoredFrame.hpp"

#include "RenderManager3D.hpp"

#include <cstdint>

namespace helengine::dreamcast {

/// Generated render manager adapter. Asset construction converts cooked
/// models/materials into owned native resources; Draw gathers the single
/// authored camera and drawable, validates them, and presents one frame
/// through the installed native sink.
class DreamcastRenderManager3D final : public ::RenderManager3D {
public:
    DreamcastRenderManager3D();

    /// Installs (or clears with nullptr) the native presentation sink. Without
    /// a sink only an empty scene may be drawn; that is the RuntimeInitProbe
    /// case. Any nonempty draw request without a sink fails.
    void installFrameSink(IAuthoredFrameSink* sink) noexcept;

    /// Installs (or clears with nullptr) the frame assembler this manager
    /// gathers into. The assembler is owned by the runtime host, not by either
    /// render manager, because one frame carries both the 3D geometry gathered
    /// here and the sprites gathered by DreamcastRenderManager2D; the same
    /// pointer is installed in both. A nonempty draw request without an
    /// assembler fails rather than dereferencing.
    void installAssembler(AuthoredFrameAssembler* assembler) noexcept;

    void Draw() override;

    void Dispose() override;

    /// Counts and clear color from the most recent Draw attempt.
    const AuthoredFrameSummary& lastFrameSummary() const noexcept;

    /// Number of frames presented through the sink since construction.
    std::uint32_t presentedFrameCount() const noexcept;

    ::RuntimeMaterial* BuildMaterialFromCooked(::PlatformMaterialAsset* materialAsset) override;

    ::RuntimeMaterial* BuildMaterialFromCooked(
        ::HeCppString cookedAssetPath,
        ::IContentStreamSource* contentStreamSource) override;

    ::RuntimeModel* BuildModelFromCooked(
        ::HeCppString cookedAssetPath,
        ::IContentStreamSource* contentStreamSource) override;

    ::RuntimeModel* BuildModelFromRaw(::ModelAsset* data) override;

    void ReleaseMaterial(::RuntimeMaterial* material) override;

    void ReleaseModel(::RuntimeModel* model) override;

private:
    AuthoredFrameAssembler* assembler_ = nullptr;
    IAuthoredFrameSink* sink_ = nullptr;
    AuthoredFrameSummary lastSummary_{};
    std::uint32_t presentedFrames_ = 0;
};

} // namespace helengine::dreamcast

#endif
