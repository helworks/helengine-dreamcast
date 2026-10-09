#include "DreamcastRuntimeTexture.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "DreamcastTextureFormat.hpp"
#include "DreamcastPaletteAllocator.hpp"

#include <dc/pvr.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace helengine::dreamcast {
namespace {

/// All runtime textures share the same hardware palette and ARGB8888 interpretation.
DreamcastPaletteAllocator paletteAllocator;

/// Names a parse outcome so a rejected file reports which check failed.
const char* cookedTextureStatusName(CookedTextureStatus status) noexcept {
    switch (status) {
        case CookedTextureStatus::Ok:
            return "Ok";
        case CookedTextureStatus::TooSmall:
            return "TooSmall";
        case CookedTextureStatus::BadMagic:
            return "BadMagic";
        case CookedTextureStatus::BadVersion:
            return "BadVersion";
        case CookedTextureStatus::BadFlags:
            return "BadFlags";
        case CookedTextureStatus::BadSide:
            return "BadSide";
        case CookedTextureStatus::BadExtent:
            return "BadExtent";
        case CookedTextureStatus::SizeMismatch:
            return "SizeMismatch";
    }
    return "Unknown";
}

} // namespace

DreamcastRuntimeTexture::DreamcastRuntimeTexture() : ::RuntimeTexture() {
}

DreamcastRuntimeTexture::~DreamcastRuntimeTexture() {
    releaseVideoMemory();
}

void DreamcastRuntimeTexture::Dispose() {
    releaseVideoMemory();
    ::RuntimeTexture::Dispose();
}

const SpriteTexture& DreamcastRuntimeTexture::spriteTexture() const noexcept {
    return texture_;
}

void DreamcastRuntimeTexture::releaseVideoMemory() noexcept {
    if (paletteFirst_ >= 0) paletteAllocator.release(static_cast<unsigned>(paletteFirst_), paletteEntries_);
    paletteFirst_ = -1;
    paletteEntries_ = 0;
    if (texture_.vram != nullptr) {
        pvr_mem_free(static_cast<pvr_ptr_t>(texture_.vram));
    }
    texture_ = SpriteTexture{};
}

DreamcastRuntimeTexture* DreamcastRuntimeTexture::CreateFromCooked(
    const std::string& cookedAssetPath,
    const std::uint8_t* bytes,
    std::size_t length) {
    CookedTextureHeader header{};
    const CookedTextureStatus status = parseCookedTexture(bytes, length, header);
    if (status != CookedTextureStatus::Ok) {
        throw std::runtime_error("Dreamcast cooked texture rejected (" +
                                 std::string(cookedTextureStatusName(status)) + "): " +
                                 cookedAssetPath);
    }

    // parseCookedTexture accepts any power-of-two side up to 1024, but the PVR
    // cannot sample below 8: its texture-size code is computed as ctz(side) - 3,
    // which underflows, and pvr_txr_load copies in whole 32-byte store-queue
    // bursts, so a 2x2 payload (8 bytes) would upload nothing at all and leave
    // a blank texture behind. Reject it here, where the file can name itself,
    // rather than at submission time where the whole frame is dropped.
    if (header.paddedSide < renderer_detail::kMinSpriteTextureSide ||
        header.paddedSide > renderer_detail::kMaxSpriteTextureSide) {
        throw std::runtime_error("Dreamcast cooked texture padded side " +
                                 std::to_string(header.paddedSide) +
                                 " is outside the samplable range " +
                                 std::to_string(renderer_detail::kMinSpriteTextureSide) + ".." +
                                 std::to_string(renderer_detail::kMaxSpriteTextureSide) + ": " +
                                 cookedAssetPath);
    }

    const std::size_t pixelBytes = header.pixelBytes;

    auto texture = std::unique_ptr<DreamcastRuntimeTexture>(new DreamcastRuntimeTexture());
    pvr_ptr_t vram = pvr_mem_malloc(pixelBytes);
    if (vram == nullptr) {
        throw std::runtime_error("Dreamcast video memory allocation of " +
                                 std::to_string(pixelBytes) + " bytes failed (" +
                                 std::to_string(pvr_mem_available()) + " bytes available): " +
                                 cookedAssetPath);
    }
    texture->texture_.vram = vram;
    texture->texture_.paddedSide = header.paddedSide;
    texture->texture_.usedWidth = header.usedWidth;
    texture->texture_.usedHeight = header.usedHeight;
    texture->texture_.textureFormat = header.textureFormat;
    texture->texture_.mipmaps = header.mipmaps;

    if (header.paletteEntries != 0) {
        const int first = paletteAllocator.allocate(header.paletteEntries);
        if (first < 0) throw std::runtime_error("Dreamcast palette RAM exhausted: " + cookedAssetPath);
        texture->paletteFirst_ = first;
        texture->paletteEntries_ = header.paletteEntries;
        // ARGB8888 is a global register: all banks must use the same interpretation.
        pvr_set_pal_format(PVR_PAL_ARGB8888);
        const std::uint8_t* palette = bytes + kCookedTextureHeaderBytes + pixelBytes;
        for (unsigned entry = 0; entry < header.paletteEntries; ++entry) {
            const std::uint8_t* color = palette + entry * 4;
            const std::uint32_t argb = (std::uint32_t{color[3]} << 24) | (std::uint32_t{color[0]} << 16) | (std::uint32_t{color[1]} << 8) | color[2];
            pvr_set_pal_entry(static_cast<unsigned>(first) + entry, argb);
        }
        texture->texture_.textureFormat |= header.paletteEntries == 16 ? PVR_TXRFMT_4BPP_PAL(first / 16) : PVR_TXRFMT_8BPP_PAL(first / 256);
    }

    // The payload is already twiddled; this is a plain byte copy.
    pvr_txr_load(bytes + kCookedTextureHeaderBytes, vram, pixelBytes);

    texture->set_Width(static_cast<int32_t>(header.usedWidth));
    texture->set_Height(static_cast<int32_t>(header.usedHeight));
    return texture.release();
}

} // namespace helengine::dreamcast

#endif
