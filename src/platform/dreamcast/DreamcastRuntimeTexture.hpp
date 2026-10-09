#pragma once

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "DreamcastRasterRenderer.hpp"

#include "RuntimeTexture.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace helengine::dreamcast {

/// A cooked texture resident in PVR video memory.
///
/// The generated RuntimeTexture base carries only Width, Height, IsEngineOwned
/// and IsDisposed; it has no native handle, so the video-memory allocation has
/// to live in a subclass. The handle is kept as an embedded SpriteTexture -
/// exactly the member set (vram, paddedSide, usedWidth, usedHeight) this
/// resource is specified to hold - so the raster stage can be handed
/// `&texture->spriteTexture()` directly as an AuthoredSpriteState::texture
/// without a second, parallel struct having to be built and kept in sync.
///
/// Width and Height on the base are the *used* extent, not the padded side:
/// engine code reasons about the real image size, and the power-of-two square
/// the PVR requires is a storage detail that only the raster stage needs.
class DreamcastRuntimeTexture final : public ::RuntimeTexture {
public:
    /// Parses a legacy or native DCTX buffer, allocates video and palette memory,
    /// and uploads its already prepared pixel/layout payload.
    ///
    /// The payload is already twiddled by the builder, so it is uploaded with
    /// pvr_txr_load, a plain byte copy. pvr_txr_load_ex is deliberately not
    /// used: its documentation states that it always twiddles regardless of
    /// flags, which would twiddle already-twiddled data.
    ///
    /// @param cookedAssetPath path the bytes were read from, used in failure messages.
    /// @param bytes           whole cooked file, header followed by pixels.
    /// @param length          number of bytes available at `bytes`.
    /// @return an owned texture; the caller takes ownership.
    /// @throws std::runtime_error if the buffer does not parse, if the padded
    ///         side is outside the range the PVR can sample, or if the video
    ///         memory allocation fails.
    static DreamcastRuntimeTexture* CreateFromCooked(const std::string& cookedAssetPath,
                                                     const std::uint8_t* bytes,
                                                     std::size_t length);

    ~DreamcastRuntimeTexture() override;

    DreamcastRuntimeTexture(const DreamcastRuntimeTexture&) = delete;
    DreamcastRuntimeTexture& operator=(const DreamcastRuntimeTexture&) = delete;

    /// Frees the video memory allocation and marks the texture disposed.
    /// Idempotent: a second call is a no-op.
    void Dispose() override;

    /// The raster stage's view of this texture. Remains valid until Dispose.
    const SpriteTexture& spriteTexture() const noexcept;

private:
    DreamcastRuntimeTexture();

    /// Releases the video memory allocation, if one is still held.
    void releaseVideoMemory() noexcept;

    SpriteTexture texture_{};
    /// First entry reserved in the shared PVR palette, or -1 for a direct-color texture.
    int paletteFirst_ = -1;
    /// Number of entries owned until disposal.
    unsigned paletteEntries_ = 0;
};

} // namespace helengine::dreamcast

#endif
