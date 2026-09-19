#include "DreamcastRenderManager2D.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "DreamcastRuntimeTexture.hpp"

#include "IContentStreamSource.hpp"
#include "runtime/native_string.hpp"
#include "system/io/stream.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace helengine::dreamcast {
namespace {

/// Largest cooked texture file this platform will read into memory before
/// parsing it, so a corrupt or mis-staged file cannot claim main RAM on a
/// 16 MiB console before being rejected.
///
/// The binding limit is DreamcastContentStreamSource's one MiB cooked payload
/// cap, which refuses a larger file at open; the dc-texture-v1 format ceiling
/// (a 1024 square at two bytes per pixel, just over two MiB) is higher but
/// unreachable, so advertising it here would promise support that does not
/// exist. One MiB admits every padded side up to and including 512.
constexpr std::size_t kMaxCookedTextureBytes = 1024u * 1024u;

struct StreamLease {
    ::Stream* value;

    ~StreamLease() {
        if (value != nullptr) {
            value->Dispose();
            delete value;
        }
    }
};

/// Reads a cooked asset in full through the generated content stream source.
std::vector<std::uint8_t> readCookedAsset(const std::string& cookedAssetPath,
                                          ::IContentStreamSource* contentStreamSource) {
    ::Stream* stream = contentStreamSource->OpenRead(cookedAssetPath);
    if (stream == nullptr) {
        throw std::runtime_error("Dreamcast content source returned a null stream: " +
                                 cookedAssetPath);
    }
    StreamLease streamLease{stream};
    const std::size_t length = stream->Length();
    if (length == 0) {
        throw std::runtime_error("Dreamcast cooked texture is empty: " + cookedAssetPath);
    }
    if (length > kMaxCookedTextureBytes) {
        throw std::runtime_error("Dreamcast cooked texture of " + std::to_string(length) +
                                 " bytes exceeds the " +
                                 std::to_string(kMaxCookedTextureBytes) + " byte limit: " +
                                 cookedAssetPath);
    }
    std::vector<std::uint8_t> bytes(length);
    std::size_t filled = 0;
    while (filled < length) {
        const std::size_t read = stream->Read(bytes.data(), filled, length - filled);
        if (read == 0) {
            break;
        }
        filled += read;
    }
    if (filled != length) {
        throw std::runtime_error("Dreamcast cooked texture read returned " +
                                 std::to_string(filled) + " of " + std::to_string(length) +
                                 " bytes: " + cookedAssetPath);
    }
    return bytes;
}

} // namespace

::RuntimeTexture* DreamcastRenderManager2D::BuildTextureFromCooked(
    std::string cookedAssetPath,
    ::IContentStreamSource* contentStreamSource) {
    // Matches the generated base, which rejects a whitespace-only path as well
    // as an empty one; without this a blank path reaches the stream source.
    if (::String::IsNullOrWhiteSpace(cookedAssetPath)) {
        throw std::invalid_argument("Dreamcast cooked texture path must be provided.");
    }
    if (contentStreamSource == nullptr) {
        throw std::invalid_argument("Dreamcast content stream source cannot be null.");
    }
    const std::vector<std::uint8_t> bytes = readCookedAsset(cookedAssetPath, contentStreamSource);
    return DreamcastRuntimeTexture::CreateFromCooked(cookedAssetPath, bytes.data(), bytes.size());
}

::RuntimeTexture* DreamcastRenderManager2D::BuildTextureFromRaw(::TextureAsset*) {
    throw std::runtime_error(
        "Dreamcast raw texture creation is unsupported; textures must be cooked offline.");
}

void DreamcastRenderManager2D::ReleaseTexture(::RuntimeTexture* texture) {
    if (texture == nullptr) {
        return;
    }
    // Dispose is virtual, so a DreamcastRuntimeTexture frees its video memory
    // here and any other RuntimeTexture is still released correctly; the base
    // destructor is virtual, so the delete is well-formed either way.
    texture->Dispose();
    delete texture;
}

void DreamcastRenderManager2D::DrawRoundedRect(::IRoundedRectDrawable2D*) {
    throw std::runtime_error("Dreamcast 2D rounded-rectangle support is not implemented.");
}

void DreamcastRenderManager2D::DrawSprite(::ISpriteDrawable2D*) {
    throw std::runtime_error("Dreamcast 2D sprite support is not implemented.");
}

void DreamcastRenderManager2D::DrawText(::ITextDrawable2D*) {
    throw std::runtime_error("Dreamcast 2D text support is not implemented.");
}

void DreamcastRenderManager2D::UpdateTextureRegionCore(::RuntimeTexture*,
                                                       int32_t,
                                                       int32_t,
                                                       int32_t,
                                                       int32_t,
                                                       ::Array<uint8_t>*,
                                                       int32_t) {
    throw std::runtime_error(
        "Dreamcast 2D texture updates are unsupported; textures must be cooked offline.");
}

} // namespace helengine::dreamcast

#endif
