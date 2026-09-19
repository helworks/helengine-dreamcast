#include "DreamcastRenderManager2D.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "DreamcastRuntimeTexture.hpp"

#include "Core.hpp"
#include "Entity.hpp"
#include "ICamera.hpp"
#include "IContentStreamSource.hpp"
#include "IDrawable2D.hpp"
#include "IRenderQueue2D.hpp"
#include "IRenderVisitor2D.hpp"
#include "ISpriteDrawable2D.hpp"
#include "ObjectManager.hpp"
#include "byte4.hpp"
#include "float3.hpp"
#include "float4.hpp"
#include "int2.hpp"
#include "runtime/native_list.hpp"
#include "runtime/native_string.hpp"
#include "system/io/stream.hpp"

#include <cmath>
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

/// Largest departure from the identity quaternion still treated as unrotated.
///
/// A sprite whose entity is never rotated carries an exact identity, but a
/// sprite parented under other entities reaches its world orientation through
/// float4::Concatenate, which accumulates a little floating-point error per
/// level. This admits that error and nothing more; it is not a tolerance for
/// "slightly rotated" art.
constexpr float kIdentityOrientationEpsilon = 1.0e-4f;

/// True when a world orientation represents no rotation at all.
///
/// A quaternion and its negation are the same rotation, so the scalar part is
/// compared by magnitude. Written as positive comparisons so a NaN component
/// falls through to false and the sprite is rejected rather than admitted.
bool isUnrotated(const ::float4& orientation) noexcept {
    return std::fabs(orientation.X) <= kIdentityOrientationEpsilon &&
           std::fabs(orientation.Y) <= kIdentityOrientationEpsilon &&
           std::fabs(orientation.Z) <= kIdentityOrientationEpsilon &&
           std::fabs(std::fabs(orientation.W) - 1.0f) <= kIdentityOrientationEpsilon;
}

/// Visits a camera's ordered 2D queue and asks every enabled drawable to draw
/// itself, which routes it back into this manager through the drawable's own
/// component - the engine's route, needing no cast here.
///
/// No layer mask is applied. The engine's own 2D visitor
/// (RenderCommandListBuilder2D::Visit) filters on the drawable and its parent
/// only, so masking here would make a sprite outside the camera mask vanish on
/// this platform and nowhere else.
class Drawable2DCollector final : public ::IRenderVisitor2D {
public:
    explicit Drawable2DCollector(DreamcastRenderManager2D& manager) noexcept
        : manager_(manager) {
    }

    void Visit(::IDrawable2D* drawable) override {
        if (drawable == nullptr) {
            // There is nothing to ask to draw itself, so submit the failure
            // through the one sprite path rather than opening a second
            // failure channel: DrawSprite(nullptr) fails the frame.
            manager_.DrawSprite(nullptr);
            return;
        }
        // A disabled owner is skipped, exactly as the engine's visitor does.
        // A *null* owner is deliberately not skipped: it is forwarded so
        // DrawSprite fails the frame for it, the same as a missing texture.
        ::Entity* owner = drawable->get_Parent();
        if (owner != nullptr && !owner->get_IsHierarchyEnabled()) {
            return;
        }
        drawable->Draw();
    }

private:
    DreamcastRenderManager2D& manager_;
};

} // namespace

void DreamcastRenderManager2D::installAssembler(AuthoredFrameAssembler* assembler) noexcept {
    assembler_ = assembler;
}

bool DreamcastRenderManager2D::sharesAssembler(
    const AuthoredFrameAssembler* assembler) const noexcept {
    return assembler != nullptr && assembler_ == assembler;
}

void DreamcastRenderManager2D::Draw() {
    if (assembler_ == nullptr) {
        throw std::runtime_error(
            "Dreamcast 2D draw requested before a frame assembler was installed.");
    }
    ::Core* core = get_OwnerCore();
    ::ObjectManager* objectManager = core != nullptr ? core->get_ObjectManager() : nullptr;
    ::List<::ICamera*>* cameras =
        objectManager != nullptr ? objectManager->get_Cameras() : nullptr;
    if (cameras == nullptr) {
        return;
    }
    Drawable2DCollector collector(*this);
    for (int32_t index = 0; index < cameras->get_Count(); ++index) {
        ::ICamera* camera = cameras->get_Item(index);
        if (camera == nullptr) {
            // The 3D manager already fails the frame for a null camera; there
            // is no 2D queue to reach through one.
            continue;
        }
        ::IRenderQueue2D* queue = camera->get_RenderQueue2D();
        if (queue == nullptr) {
            // Not a failure: a camera may legitimately carry no overlay.
            continue;
        }
        queue->VisitOrdered(&collector);
    }
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
    // missing texture, a texture built by some other backend and a rotated
    // entity all fail the whole frame rather than being skipped. A skipped or
    // wrongly drawn sprite would render a wrong frame and report success.
    AuthoredSpriteState state{};
    ::Entity* owner = sprite != nullptr ? sprite->get_Parent() : nullptr;
    auto* texture = sprite != nullptr
                        ? dynamic_cast<DreamcastRuntimeTexture*>(sprite->get_Texture())
                        : nullptr;
    // AuthoredSpriteState is an axis-aligned rectangle with no rotation field,
    // so a rotated entity cannot be represented. It is rejected rather than
    // drawn unrotated: silently dropping the rotation renders a wrong frame
    // and reports success. Lift this once the sprite contract carries a
    // rotation, which is what the engine's own emission passes alongside the
    // quad.
    const bool unrotated = owner != nullptr && isUnrotated(owner->get_Orientation());
    if (owner != nullptr && texture != nullptr && unrotated) {
        const SpriteTexture& handle = texture->spriteTexture();

        // ISpriteDrawable2D carries no screen position; it comes from the
        // owning entity. Entity::get_Position, get_Scale and get_Orientation
        // all return *world* values from one cached transform decomposition
        // (EnsureWorldTransformCache), so a sprite parented under another
        // entity lands where the hierarchy puts it. They are read instead of
        // get_WorldTransformMatrix because that accessor is uncached and
        // recomposes the whole parent chain on every call, and because scale
        // and orientation are needed here in decomposed form anyway.
        const ::float3 position = owner->get_Position();
        state.destinationX = position.X;
        state.destinationY = position.Y;

        // Size is authored in pixels and defaults to (0, 0), which the engine
        // reads as "the whole texture"; RenderCommandListBuilder2D::EmitSprite
        // makes the same substitution and multiplies the result by the world
        // scale, which an axis-aligned rectangle represents exactly. A
        // non-positive scale yields a non-positive extent, which the assembler
        // rejects. Width/Height on the runtime texture are the real image
        // extent, not the padded square.
        //
        // The fallback is a known per-platform hazard and is kept deliberately.
        // Cooked textures on this platform are capped at 64 pixels per axis, so
        // the runtime extent is the POST-downscale size: a sprite relying on the
        // fallback draws a quarter the size here that it does on a platform
        // cooking at native resolution. It is not a defect today because the
        // scene this milestone targets never relies on it - every sprite in the
        // console instruction Blueprint carries an explicit Size, produced by
        // ResolvedControlIcon::FitDisplaySizeWithin, which clamps each axis to a
        // minimum of one and is fed authored bounds of 48x48, 38x48 and 46x46.
        // The serialized Blueprint was read to confirm this: every SpriteComponent
        // Size field in it is nonzero. Diverging from the engine's own
        // substitution would be the larger risk, since a scene that does rely on
        // the fallback would then differ from every other platform in a second
        // way. If a scene ever does rely on it, the honest fix is to carry the
        // pre-downscale extent in the cooked header rather than to special-case
        // the size here.
        const ::int2 size = sprite->get_Size();
        const ::float3 scale = owner->get_Scale();
        state.destinationWidth =
            (size.X > 0 ? static_cast<float>(size.X) : static_cast<float>(texture->Width)) *
            scale.X;
        state.destinationHeight =
            (size.Y > 0 ? static_cast<float>(size.Y) : static_cast<float>(texture->Height)) *
            scale.Y;

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
