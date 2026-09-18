# Dreamcast textures and sprites design

**Status:** approved 2026-09-17. Supersedes nothing; extends the authored scene
path proven by the v0.3 lighting milestone.

**Goal.** Give the Dreamcast a cooked PVR texture pipeline and a sprite drawing
path, proven by rendering the shared console instruction Blueprint that the
Nintendo 64 runtime already draws.

**Non-goals, deliberately.** Text and font atlases, rounded rectangles, the UI
root, textured 3D materials, runtime-built or runtime-mutated textures, sprite
rotation and per-sprite blend modes. Each is additive later and none is needed
for the target overlay.

## Context

`DreamcastRenderManager2D` is a stub whose five entry points all throw. The
shared `cube_test` scene drops three roots for Dreamcast: the desktop
instruction overlay, the UI root, and the console instruction Blueprint. Only
the third is in scope here; it is listed in the scene factory's
`SpritelessRuntimeExcludedPlatformIds`, which currently contains Dreamcast
alone because the N64 runtime already renders its authored sprite icons.

The lit 3D path was proven on screen on 2026-09-17 at ELF
`5c88499c7829b9d4854f755043a47a18a73620ae85b5023b1ae8ec9bcf04bc1c` and must not
regress.

## Decisions

| Decision | Choice |
|---|---|
| Scope | Textures and sprites only |
| Texture format | ARGB4444, twiddled, single format |
| Compositing | PVR translucent list, depth writes off, submission order |
| Non power-of-two art | Pad up, record and sample the used sub-rectangle |
| Frame model | Extend the existing assembler rather than add a second one |

Rationale for the single format: ARGB4444 gives sixteen alpha levels, which is
what anti-aliased icon edges need, at two bytes per pixel. One format keeps the
cooker, the runtime loader and the validator to one path each, matching how the
material schema stayed deliberately narrow. Further formats are additive and do
not invalidate content cooked under this one.

Rationale for padding rather than rejecting: the source art is shared with every
other platform, so a Dreamcast-shaped authoring constraint should not be pushed
back onto it. This is the same principle that made the camera field of view
authored state rather than a per-platform constant.

## Cooked texture asset

A new `dc-texture-v1` asset sits beside the existing cooked material and model
assets. All work happens in the builder so the runtime only copies bytes:

1. Decode the source image and convert to ARGB4444.
2. Pad up to the next power of two on each axis. Fill the pad with transparent
   black so no bleed can appear at the sampled edge.
3. Twiddle into the PVR's Morton order, interleaving the x and y bits.
4. Record the used sub-rectangle.

Twiddling is a pure function of the pixel array, so cooked bytes are
reproducible and hash-stable like every other cooked artifact on this platform.

Two rejections, each failing the cook with the asset named rather than degrading
quietly, following `DreamcastMaterialCooker`:

- a padded axis above 1024, the PVR limit
- a zero-sized source image

The 3D material schema is unchanged. `dc-standard-v1` keeps rejecting texture
bindings, so the lit 3D path proven yesterday is untouched. Textures are
reachable only through sprites in this milestone.

## Video memory

There is no reserved region. Video memory is partitioned once when the tile
accelerator is initialised: the vertex buffer size is fixed there and the
remainder is the texture pool. Textures allocate from that pool and are released
when the scene unloads. Scene-scoped lifetime means the whole set is freed
together, so there is no fragmentation to manage and no free list.

Two different concerns, kept separate:

- **At runtime** there is no limit of ours. A texture either allocates or it
  does not. Failure fails the load, naming the asset and the bytes requested.
- **At build time** the cooker sums cooked texture bytes and fails the build if
  the total cannot fit, the same kind of check as the existing embedded content
  and resident span budgets. It catches the problem on the developer's machine
  rather than on the console.

The build-time number is measured, not asserted. The runtime already reports
free video memory in its diagnostic snapshot, currently 6,937,344 bytes. The
real figure is to be read after tile accelerator initialisation on a build
carrying the sprite path, and the limit set from that measurement with margin.
Both the measurement and the derived limit are recorded in `docs/Validation.md`.

`BuildTextureFromRaw` and `UpdateTextureRegionCore` remain unimplemented and keep
throwing. Both are cooked-content bypasses, and refusing them preserves the rule
that everything on this platform is prepared offline.

## Sprite submission

`AuthoredSpriteState` carries a destination rectangle in screen pixels, the
source sub-rectangle within the padded texture, a texture handle, and a colour
tint with alpha. Nothing else.

`AuthoredFrameAssembler::addSprite` validates as `setCamera` and `addDrawable`
already do, failing the frame before any packet is emitted on:

- non-finite destination or source coordinates
- a null texture handle
- a source rectangle outside the texture's recorded used area
- a non-finite tint

At most 64 sprites per frame. Overflow signals a count one past the maximum and
fails, exactly as the fifth directional light does, with the bounds check before
the write.

The sink keeps one call, now carrying both lists rather than gaining a second
method. One call makes it impossible to present the 3D list without its overlay
and preserves the exactly-once contract the existing tests enforce. `present`
emits the opaque list first, then the translucent list with depth writes
disabled, in submission order.

The sprite frame type carries no generated headers, so it stays host-testable in
the harness that proved the lighting pass.

## Reaching the generated scene

`DreamcastRenderManager2D::DrawSprite` converts the generated drawable into an
`AuthoredSpriteState`. Texture resolution needs no new mechanism: the generated
runtime is already configured with platform-owned cooked texture resolution, the
route models and materials take.

**Assembler ownership moves.** The frame assembler currently belongs to the 3D
render manager, but sprites arrive through the 2D manager and both must land in
the same frame. Ownership moves to `DreamcastRuntimeHost`, which injects a
reference into both managers. The assembler represents the frame rather than
either manager, so this is the honest place for it.

Component support rules gain sprite components as `Transform`, and mark text and
rounded-rectangle components `Unsupported`. A scene using those then fails during
cooking with the component named, instead of reaching the runtime and throwing.
This mirrors the handling of point and spot lights.

## Snapshot

The authored snapshot is exactly 64 bytes across sixteen words with none spare.
Rather than grow it, the directional light count and sprite count pack into one
word: one is capped at 4, the other at 64, so both fit in 16 bits each. The
64-byte static assertion stands.

`kDreamcastAuthoredSnapshotVersion` goes to 3, and the verifier reads the packed
field. The bump is safe to make because a stale build now reports
`version-mismatch` immediately rather than stalling to a timeout, a gap fixed on
2026-09-17.

## Failure handling

New statuses keep failures specific: too many sprites, invalid sprite, and
texture allocation failed. Each fails the frame or the load before anything
reaches the screen, the rule the camera and lighting paths already follow.

## Testing

Host tests: sprite validation for each rejection above, overflow at sixty-five,
list ordering, sub-rectangle clamping, and both lists presenting exactly once.

Cooker tests: ARGB4444 conversion, padding with the recorded sub-rectangle, the
oversize rejection, the zero-size rejection, and the build-time total check.

Twiddling gets specific care. A round-trip test would pass against a function
that is wrong in a self-consistent way, so the test asserts a small image
against hand-computed Morton order, and the implementation is then mutated to
confirm the test actually fails. This is the discipline that caught the
field-of-view bounds following their own constants rather than pinning them.

The verifier gains a minimum sprite count, matching the minimum light count
added on 2026-09-17, so the feature ships with an automated gate rather than an
assumption that it worked.

## Evidence

Dreamcast is removed from `SpritelessRuntimeExcludedPlatformIds` in the shared
scene factory, so the console instruction Blueprint root is no longer dropped.

Proof is captured pixels showing the icons, not a non-zero sprite count in the
snapshot. A count proves gathering, not drawing. The lighting milestone's
evidence row had to be corrected for exactly this class of overclaim, where a
stated mechanism went beyond what the capture demonstrated.

Recorded in `docs/Validation.md`: the build identity and ELF hash, the verifier
result with the sprite gate armed, the captured frame with the icon pixels
identified, the measured free video memory, and confirmation that the lit 3D
path still renders as it did at ELF `5c88499c...`.
