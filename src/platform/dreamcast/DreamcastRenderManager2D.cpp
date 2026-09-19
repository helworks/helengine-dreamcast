#include "DreamcastRenderManager2D.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "DreamcastAuthoredTransform.hpp"
#include "DreamcastRuntimeTexture.hpp"

#include "Entity.hpp"
#include "IContentStreamSource.hpp"
#include "ISpriteDrawable2D.hpp"
#include "byte4.hpp"
#include "float4.hpp"
#include "float4x4.hpp"
#include "int2.hpp"
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

/// Converts one authored byte channel to the unit float the assembler wants.
float unitFromByte(std::uint8_t channel) noexcept {
    return static_cast<float>(channel) * (1.0f / 255.0f);
}

} // namespace

void DreamcastRenderManager2D::installAssembler(AuthoredFrameAssembler* assembler) noexcept {
    assembler_ = assembler;
}

void DreamcastRenderManager2D::Dispose() {
    // Borrowed, never owned: the runtime host outlives this manager.
    assembler_ = nullptr;
    ::RenderManager2D::Dispose();
}

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

void DreamcastRenderManager2D::DrawSprite(::ISpriteDrawable2D* sprite) {
    if (assembler_ == nullptr) {
        // Not a property of this sprite: the host never installed the frame.
        // Failing the sprite through the assembler is impossible, so this is
        // the one condition here that throws.
        throw std::runtime_error(
            "Dreamcast sprite draw requested before a frame assembler was installed.");
    }

    // Built empty and filled only when every input is sound. A state left
    // empty carries a null texture, which is exactly the assembler's
    // InvalidSprite condition, so a null drawable, a null owning entity, a
    // missing texture and a texture built by some other backend all fail the
    // whole frame rather than being skipped. A skipped sprite would render a
    // wrong frame and report success.
    AuthoredSpriteState state{};
    ::Entity* owner = sprite != nullptr ? sprite->get_Parent() : nullptr;
    auto* texture = sprite != nullptr
                        ? dynamic_cast<DreamcastRuntimeTexture*>(sprite->get_Texture())
                        : nullptr;
    if (owner != nullptr && texture != nullptr) {
        const SpriteTexture& handle = texture->spriteTexture();

        // ISpriteDrawable2D carries no screen position; it comes from the
        // owning entity. The world transform is used rather than the local
        // position so a sprite parented under another entity lands where the
        // hierarchy puts it, and transformPoint of the origin is that
        // matrix's translation.
        const Matrix4x4 worldFromEntity = matrixFromGenerated(owner->get_WorldTransformMatrix());
        const Vec3 origin = transformPoint(worldFromEntity, Vec3{0.0f, 0.0f, 0.0f});
        state.destinationX = origin.x;
        state.destinationY = origin.y;

        // Size is authored in pixels and defaults to (0, 0), which the engine
        // reads as "the whole texture"; RenderCommandListBuilder2D::EmitSprite
        // makes the same substitution. Width/Height on the runtime texture are
        // the real image extent, not the padded square.
        const ::int2 size = sprite->get_Size();
        state.destinationWidth = size.X > 0 ? static_cast<float>(size.X)
                                            : static_cast<float>(texture->Width);
        state.destinationHeight = size.Y > 0 ? static_cast<float>(size.Y)
                                             : static_cast<float>(texture->Height);

        // get_SourceRect is (x, y, width, height) normalized against the real
        // image; the raster stage samples the padded square.
        const ::float4 sourceRect = sprite->get_SourceRect();
        const SpriteSourceSpan span =
            spriteSourceSpan(SpriteSourceRect{sourceRect.X, sourceRect.Y, sourceRect.Z, sourceRect.W},
                             handle.usedWidth, handle.usedHeight, handle.paddedSide);
        state.sourceU0 = span.u0;
        state.sourceV0 = span.v0;
        state.sourceU1 = span.u1;
        state.sourceV1 = span.v1;

        const ::byte4 color = sprite->get_Color();
        state.tint = ColorRgba{unitFromByte(color.X), unitFromByte(color.Y),
                               unitFromByte(color.Z), unitFromByte(color.W)};

        // The runtime texture embeds the handle the raster stage expects, so
        // its address is passed through unchanged. It stays valid until
        // ReleaseTexture, which cannot run inside a frame.
        state.texture = &handle;
    }

    // The return value is deliberately unused: the generated caller
    // (SpriteComponent::Draw) discards it, and the assembler latches the first
    // failure and reports it from present(), which is where the frame is
    // failed.
    static_cast<void>(assembler_->addSprite(state));
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
