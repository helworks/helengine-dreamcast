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
    if (version != kVersion) {
        return CookedTextureStatus::BadVersion;
    }

    std::uint16_t flags = 0;
    std::memcpy(&flags, bytes + 6, sizeof(flags));
    if (flags != 0) {
        return CookedTextureStatus::BadFlags;
    }

    std::uint16_t paddedSide = 0;
    std::memcpy(&paddedSide, bytes + 8, sizeof(paddedSide));
    if (paddedSide == 0 || !isPowerOfTwo(paddedSide) || paddedSide > kMaxSide) {
        return CookedTextureStatus::BadSide;
    }

    std::uint16_t usedWidth = 0;
    std::memcpy(&usedWidth, bytes + 10, sizeof(usedWidth));
    std::uint16_t usedHeight = 0;
    std::memcpy(&usedHeight, bytes + 12, sizeof(usedHeight));
    if (usedWidth == 0 || usedWidth > paddedSide || usedHeight == 0 || usedHeight > paddedSide) {
        return CookedTextureStatus::BadExtent;
    }

    const std::size_t pixelBytes = static_cast<std::size_t>(paddedSide) * paddedSide * 2;
    if (length != kCookedTextureHeaderBytes + pixelBytes) {
        return CookedTextureStatus::SizeMismatch;
    }

    header.paddedSide = paddedSide;
    header.usedWidth = usedWidth;
    header.usedHeight = usedHeight;
    return CookedTextureStatus::Ok;
}

} // namespace helengine::dreamcast
