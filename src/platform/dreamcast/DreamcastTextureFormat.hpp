#pragma once

#include <cstddef>
#include <cstdint>

namespace helengine::dreamcast {

/// Size in bytes of the fixed cooked-texture header, before the pixel payload.
constexpr std::size_t kCookedTextureHeaderBytes = 16;

/// Fields decoded from a cooked texture header, once it has been validated.
///
/// The padded side is the power-of-two square dimension the pixel payload is
/// stored at; the used width and height are the source image's real extent
/// within that padded square.
struct CookedTextureHeader {
    /// Power-of-two, square side length the pixel payload is padded to.
    std::uint16_t paddedSide;
    /// Real image width, no greater than paddedSide.
    std::uint16_t usedWidth;
    /// Real image height, no greater than paddedSide.
    std::uint16_t usedHeight;
    /// PVR format bits, excluding a palette bank assigned at load time.
    std::uint32_t textureFormat;
    /// Whether the payload includes the full smallest-first mip chain.
    bool mipmaps;
    /// 32-byte-aligned upload length, excluding the palette tail.
    std::size_t pixelBytes;
    /// RGBA8888 entries appended after the upload payload: zero, 16 or 256.
    std::uint16_t paletteEntries;
};

/// Outcome of parsing a cooked texture header.
///
/// Values are ordered to match the sequence in which parseCookedTexture
/// performs its checks, so the first fault present in a buffer determines
/// the status it reports.
enum class CookedTextureStatus : std::uint8_t {
    /// The header parsed successfully and the buffer length matches the payload.
    Ok,
    /// The buffer is shorter than the fixed header, or the pointer is null.
    TooSmall,
    /// The magic bytes at offset 0 are not "DCTX".
    BadMagic,
    /// The version field is neither legacy version one nor native version two.
    BadVersion,
    /// Flags or palette count are invalid for this version or hardware layout.
    BadFlags,
    /// The padded side is zero, not a power of two, or exceeds 1024.
    BadSide,
    /// The used width or height is zero, or exceeds the padded side.
    BadExtent,
    /// The buffer length does not equal the header, format-dependent pixels and palette tail.
    SizeMismatch,
};

/// Parses and validates a cooked texture header from raw bytes.
///
/// Reads every multi-byte field with std::memcpy rather than a pointer cast,
/// because the target SuperH processor faults on unaligned multi-byte loads.
/// Checks run in the order CookedTextureStatus lists them, so a buffer with
/// several faults reports the first one encountered. On success, `header` is
/// filled in and CookedTextureStatus::Ok is returned; on failure, `header` is
/// left unmodified.
///
/// @param bytes  pointer to the start of the cooked texture buffer, may be null.
/// @param length number of bytes available at `bytes`.
/// @param header receives the decoded header fields when parsing succeeds.
/// @return the validation outcome.
CookedTextureStatus parseCookedTexture(const std::uint8_t* bytes, std::size_t length,
                                        CookedTextureHeader& header) noexcept;

} // namespace helengine::dreamcast
