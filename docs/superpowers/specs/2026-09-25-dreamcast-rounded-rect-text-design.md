# Dreamcast rounded rectangles and text design

## Goal

Extend the existing Dreamcast authored 2D overlay so a scene can render
`RoundedRectComponent` and `TextComponent` alongside the sprites already
working in `cube_test`. Preserve the engine-authored font and layout data rather
than substituting a Dreamcast-only hard-coded glyph set.

## Constraints and current state

- `DreamcastRenderManager2D::DrawRoundedRect` and `DrawText` currently throw
  `not implemented`; the platform definition rejects both component types.
- The engine `TextComponent` exposes its `FontAsset`, text, layout size, tint,
  font scale, alignment, and wrapping state through `ITextDrawable2D`.
- The engine's command-list builder lays text out from `FontAsset` line height,
  space width, glyph source rectangles, glyph offsets and advances; its
  rounded-rectangle command carries bounds, radius, border thickness, corner
  selection, fill and border colors. The Dreamcast implementation must match
  those shared semantics rather than interpreting unused component fields
  differently.
- The editor can externalize authored/generated font atlases as cooked texture
  work. Dreamcast currently declares only the ordinary `texture` cook capability
  capped at 64 pixels per axis, which is suitable for the current sprite path
  but not a font atlas.
- The current 2D manager gathers ordered camera queues and the 3D manager
  assembles and presents the shared frame. All 2D content must remain in this
  path so viewport transforms, camera order, and painter order stay coherent.
- Do not create worktrees or build in a shared engine/demo checkout. Keep build,
  emulator-profile, and diagnostic outputs under the ignored `.dreamcast`
  directory in this repository. Do not capture or parse screenshots/images;
  Flycast inspection is for the user.

## Chosen approach

Use the engine's authored font-atlas pipeline and add both drawable kinds to the
Dreamcast's ordered translucent overlay. This respects `FontAsset` and avoids
an ASCII-only fallback that would make the same authored scene render
differently from the engine's intended font. Do not attempt complete advanced
text effects in this increment.

### Rounded rectangles

Gather each rounded-rectangle drawable into the shared authored frame. Derive
screen position and world scale from its owning entity as for sprites; preserve
the authored 2D render order and viewport mapping. Tessellate the rounded body
and border into bounded triangles and render it in the translucent PVR list
with depth writes disabled. Match the engine command-list contract: honor
world-space bounds, radius, corner selection, fill/border colors, and border
thickness. The engine command list does not currently carry the component's
rotation, `Color`, or `SourceRect`, so Dreamcast will not invent different
semantics for those fields. Reject invalid or unrepresentable geometry through
the frame's existing latched-error path instead of silently omitting or
simplifying the shape.

### Text and font atlas

Declare a dedicated Dreamcast `font-atlas-texture` cook capability, separate
from the ordinary 64-pixel sprite texture capability. Cook the engine-provided
atlas through the Dreamcast texture format path with a 256-pixel-per-axis cap
(the established font-atlas scale used by another Helengine console backend),
package it through the existing ROMFS flow, and resolve it as a Dreamcast
runtime texture. The platform cook/import path must rescale font metrics and
glyph rectangles consistently when the atlas is reduced. Use the engine's
layout semantics: atlas glyph rectangles, line height, space width, glyph
offsets/advances, font scale, wrapping, and horizontal alignment. Submit glyph
quads in the same ordered overlay as sprites and rounded rectangles.

The first supported text slice honors text content, font, authored layout size,
color, font scale, horizontal alignment, and wrapping. Match the engine's
existing behavior for a character absent from the selected atlas (skip it
without emitting a glyph or advancing). Outline and shadow effects,
selection/caret visuals, and runtime font mutation are out of scope. Missing
fonts/atlases, invalid glyph metadata, atlas overflow, invalid transforms, and
capacity overflow must be reported as build errors or latched frame errors,
not rendered as success.

### Ordering and frame data

Represent overlay submissions as one ordered sequence (or an equivalent
sequence-preserving structure) so sprites, shapes, and glyphs composite in the
order produced by the engine's camera render queues. Do not group all rectangles
or all text ahead of sprites if that changes authored painter order. Keep the
existing opaque 3D pass first and submit the 2D overlay after it in the
translucent PVR list.

## Validation

- Native tests cover rounded-rectangle tessellation, corners/border, invalid
  geometry, glyph layout/wrapping/alignment and metric rescaling, atlas UVs,
  mixed-kind ordering, and capacity/error latching.
- Builder tests cover the separate font-atlas capability, cooking settings,
  accepted work-item kind, cooked atlas packaging, and rejection of malformed
  or over-budget font data.
- Run the repository's normal tests and a fresh authored-scene build using
  output roots under `.dreamcast`; run the verifier with minimum shape/text
  counts enabled and check the existing sprite/light gates remain green.
- Launch the resulting ELF in Flycast using the repo-local profile. The user
  visually checks the window; no screenshot or image parsing is part of this
  task.

## Non-goals

- A fixed built-in ASCII bitmap font or a parallel Dreamcast-only text API.
- Full parity for outline, shadow, selection, and caret rendering.
- Runtime font atlas generation or updates.
- Changes to the engine checkout, demo checkout, or other platform backends.
- Committing generated build artifacts or emulator output.
