#include "DreamcastTextureFormat.hpp"

#include <cstring>

namespace helengine::dreamcast {
namespace {

constexpr std::uint32_t kMagic = 0x58544344u; // "DCTX", little-endian.
constexpr std::uint16_t kVersion = 1;
/// Largest padded square side the tile accelerator can sample.
///
/// This is hardware truth and is deliberately *not* the same number as the
/// cooker's DreamcastTextureSchemaIds.MaximumSide, which is a platform limit
/// derived from DreamcastContentStreamSource's one MiB cooked payload read cap
/// and is therefore smaller. Do not tidy the two into one constant: if that
/// read cap is raised the cooker's limit moves and this one does not.
constexpr std::uint16_t kMaxSide = 1024;

bool isPowerOfTwo(std::uint16_t value) {
    return value != 0 && (value & (value - 1)) == 0;
}

} // namespace

CookedTextureStatus parseCookedTexture(const std::uint8_t* bytes, std::size_t length,
                                        CookedTextureHeader& header) noexcept {
    if (bytes == nullptr || length < kCookedTextureHeaderBytes) {
        return CookedTextureStatus::TooSmall;
    }

    std::uint32_t magic = 0;
    std::memcpy(&magic, bytes + 0, sizeof(magic));
    if (magic != kMagic) {
        return CookedTextureStatus::BadMagic;
    }

    std::uint16_t version = 0;
    std::memcpy(&version, bytes + 4, sizeof(version));
    if (version != kVersion && version != 2) {
        return CookedTextureStatus::BadVersion;
    }

    std::uint16_t flags = 0;
    std::memcpy(&flags, bytes + 6, sizeof(flags));
    const unsigned format = version == 1 ? 2 : flags & 7u;
    const bool vq = (flags & 8u) != 0;
    const bool mipmaps = (flags & 16u) != 0;
    const bool linear = (flags & 32u) != 0;
    std::uint16_t paletteEntries = 0;
    if (version == 2) std::memcpy(&paletteEntries, bytes + 14, sizeof(paletteEntries));
    const unsigned expectedPalette = format == 5 ? 16 : format == 6 ? 256 : 0;
    if ((version == 1 && flags != 0) || (version == 2 &&
        ((flags & ~63u) != 0 || format > 6 || (vq && format >= 5) ||
         (linear && (vq || mipmaps || format >= 5)) || paletteEntries != expectedPalette))) {
        return CookedTextureStatus::BadFlags;
    }

    std::uint16_t paddedSide = 0;
    std::memcpy(&paddedSide, bytes + 8, sizeof(paddedSide));
    if (paddedSide == 0 || !isPowerOfTwo(paddedSide) || paddedSide > kMaxSide || (version == 2 && paddedSide < 8)) {
        return CookedTextureStatus::BadSide;
    }

    std::uint16_t usedWidth = 0;
    std::memcpy(&usedWidth, bytes + 10, sizeof(usedWidth));
    std::uint16_t usedHeight = 0;
    std::memcpy(&usedHeight, bytes + 12, sizeof(usedHeight));
    if (usedWidth == 0 || usedWidth > paddedSide || usedHeight == 0 || usedHeight > paddedSide) {
        return CookedTextureStatus::BadExtent;
    }

    std::size_t pixels = static_cast<std::size_t>(paddedSide) * paddedSide;
    if (mipmaps) pixels = pixels * 4 / 3 + 3;
    std::size_t pixelBytes = format == 5 ? pixels / 2 : format == 6 ? pixels : pixels * 2;
    if (vq) pixelBytes = (pixelBytes + 7) / 8 + 2048;
    if (version == 2) pixelBytes = (pixelBytes + 31) & ~std::size_t{31};
    if (length != kCookedTextureHeaderBytes + pixelBytes + paletteEntries * 4u) {
        return CookedTextureStatus::SizeMismatch;
    }

    header.paddedSide = paddedSide;
    header.usedWidth = usedWidth;
    header.usedHeight = usedHeight;
    header.textureFormat = (format << 27) | (vq ? 1u << 30 : 0u) | (linear ? 1u << 26 : 0u);
    header.mipmaps = mipmaps;
    header.pixelBytes = pixelBytes;
    header.paletteEntries = paletteEntries;
    return CookedTextureStatus::Ok;
}

} // namespace helengine::dreamcast
