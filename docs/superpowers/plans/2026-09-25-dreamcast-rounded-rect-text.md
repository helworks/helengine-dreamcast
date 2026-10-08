# Dreamcast rounded rectangles and text Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render authored rounded rectangles and FontAsset-backed text in the Dreamcast 2D overlay alongside the existing sprites.

**Architecture:** Keep the opaque mesh pass unchanged. Extend the authored frame with bounded shape/glyph records and a single mixed-kind order stream, have the existing 2D manager gather camera queue drawables into it, and submit the stream through the PVR translucent list. Add a distinct 256-pixel font-atlas cook capability while retaining the existing 64-pixel sprite texture cap.

**Tech Stack:** C++17/KallistiOS PVR renderer, C#/.NET 9 builder and xUnit tests, PowerShell build/verifier scripts, Flycast.

**Spec:** `docs/superpowers/specs/2026-09-25-dreamcast-rounded-rect-text-design.md`

## Global Constraints

- Do not create or use git worktrees.
- Keep build, test, emulator-profile, and diagnostic outputs under ignored `.dreamcast` in this repository.
- Do not capture, inspect, or parse screenshots or other images; open Flycast visibly for the user to inspect.
- Do not modify the Helengine engine checkout or the demo checkout.
- Preserve the ordinary Dreamcast `texture` cook cap at 64 pixels per axis; the new font-atlas capability uses a 256-pixel-per-axis cap.
- Keep the current 64-sprite frame limit and enforce explicit limits for rounded rectangles, text glyphs, and total ordered overlay submissions.
- No commit is included unless the user asks for one.

## Review Focus

- Non-square font-atlas rescaling can desynchronize glyph UVs from metrics — pin width/height and metric rescaling in the cook/packaging test.
- Missing glyphs, spaces, newlines, wrapping, and alignment can diverge from Helengine layout — pin each behavior in `DreamcastTextLayoutTests`.
- Mixed sprites, rectangles, and glyphs can be reordered by separate arrays or PVR autosort — pin submission order across all three kinds.
- Non-finite/negative shape geometry or malformed UVs can bypass native bounds checks — pin rejection and first-failure latching in native assembler tests.
- Foreign/disposed font textures can outlive or mismatch their Dreamcast runtime handles — pin the text-gather validation and ensure no glyph is submitted for a rejected font.

---

### Task 1: Declare text and rounded-rectangle component support

**Files:**
- Modify: `builder/DreamcastPlatformDefinitionFactory.cs`
- Modify: `builder/DreamcastCookedContentValidator.cs`
- Test: `builder.tests/DreamcastPlatformDefinitionTests.cs`
- Test: `builder.tests/DreamcastCookedContentValidatorTests.cs`

**Interfaces:**
- Consumes: component ids `helengine.TextComponent` and `helengine.RoundedRectComponent`.
- Produces: Dreamcast support rules and validator acceptance for both component ids; rendering/cooking follow in later tasks.

- [x] **Step 1: Add failing definition tests**

Replace the current “rejects text and rounded rects” assertions with assertions that both rules have `PlatformComponentSupportKind.Transform` and a description that matches the implemented limitations. Keep the existing sprite and viewport assertions.

- [x] **Step 2: Add failing cooked-scene acceptance tests**

Add one test staging a `TextComponent` and one staging a `RoundedRectComponent` beside the required camera and mesh. Assert validation succeeds. Change the existing unsupported-component rejection fixture from `TextComponent` to the real engine component `helengine.AudioSourceComponent` so it continues testing an unsupported engine component.

- [x] **Step 3: Run the focused tests and confirm they fail**

Run: `dotnet test builder.tests/helengine.dreamcast.builder.tests.csproj --filter "FullyQualifiedName~DreamcastPlatformDefinitionTests|FullyQualifiedName~DreamcastCookedContentValidatorTests" --artifacts-path .dreamcast/dotnet/artifacts -p:HelengineRoot=C:/dev/helworks/helengine`

Expected: the new support-rule and acceptance assertions fail while the existing tests still compile.

- [x] **Step 4: Enable only these two support rules**

Change the two rule kinds from `Unsupported` to `Transform` and update their descriptions/remediation to state that rounded shapes and atlas-backed text are rendered. Do not relax validation for unrelated component ids or asset types.

- [x] **Step 5: Run the focused tests**

Run the command from Step 3. Expected: PASS.

### Task 2: Add the separate Dreamcast font-atlas cook capability

**Files:**
- Modify: `builder/DreamcastPlatformDefinitionFactory.cs`
- Modify: `builder/DreamcastPlatformCookWorkItemExecutor.cs`
- Test: `builder.tests/DreamcastPlatformDefinitionTests.cs`
- Test: `builder.tests/DreamcastPlatformCookWorkItemExecutorTests.cs`

**Interfaces:**
- Consumes: editor work items whose source kind is `font-atlas-texture`, target kind is `runtime-texture`, and serialized settings use the existing `DreamcastTextureCookSettingsSerializer`.
- Produces: a builder-owned `font-atlas-texture` capability with `MaxResolution = 256`, `ColorFormat = Rgba32`, `AlphaPrecision = A8`; the normal `texture` capability remains capped at 64.

- [x] **Step 1: Add failing capability assertions**

Change the single-capability test into separate assertions for `texture` and `font-atlas-texture`. Pin the new capability's source kind, target kind, ownership, 256 setting and format settings, and pin the ordinary capability's existing 64 setting.

- [x] **Step 2: Add a failing executor test for generated font atlases**

Create a serialized `TextureAsset` work item with source kind `font-atlas-texture`, 256 settings and output `fonts/default.dctex`. Assert the executor writes a valid Dreamcast cooked texture with correct used dimensions and magic. Add a 512-by-256 input case and assert the resulting used dimensions do not exceed 256 and preserve aspect ratio.

- [x] **Step 3: Run focused builder tests and confirm the new case fails**

Run: `dotnet test builder.tests/helengine.dreamcast.builder.tests.csproj --filter "FullyQualifiedName~DreamcastPlatformDefinitionTests|FullyQualifiedName~DreamcastPlatformCookWorkItemExecutorTests" --artifacts-path .dreamcast/dotnet/artifacts -p:HelengineRoot=C:/dev/helworks/helengine`

Expected: existing `font-atlas-texture` rejection is the only failure relevant to the new tests.

- [x] **Step 4: Implement the capability and accept its source kind**

Add the second capability in `DreamcastPlatformDefinitionFactory.Create`, keeping its target/ownership/format behavior aligned with the texture capability and using 256 as its default max resolution. Add `font-atlas-texture` to `SupportedSourceAssetKinds`; retain the same decode, resize, format, twiddle, and ROMFS output path.

- [x] **Step 5: Run focused builder tests**

Run the command from Step 3. Expected: PASS, including the ordinary texture cap and the two atlas dimensions.

### Task 3: Define bounded overlay records and preserve mixed draw order

**Files:**
- Modify: `src/platform/dreamcast/DreamcastSpriteFrame.hpp`
- Modify: `src/platform/dreamcast/DreamcastAuthoredFrame.hpp`
- Modify: `src/platform/dreamcast/DreamcastAuthoredFrame.cpp`
- Test: `tests/native/DreamcastSpriteSubmissionTests.cpp` (extend or rename to `DreamcastOverlaySubmissionTests.cpp` if the added cases make the existing file unclear)
- Modify: `Makefile`

**Interfaces:**
- Consumes: existing `AuthoredSpriteState`/`SpriteFrame` and the Dreamcast overlay scale/viewport dimensions.
- Produces: `AuthoredRoundedRectState`, `AuthoredGlyphState`, `AuthoredOverlayKind`, `AuthoredOverlayOrder`, a bounded `AuthoredOverlayFrame`, assembler methods `addRoundedRect(...)` and `addGlyph(...)`, and an extended sink call receiving the overlay frame.
- Limits: 64 rounded rectangles, 255 glyphs, and 383 total ordered submissions per frame. Existing sprite limit remains 64. The maximum ordered count equals the sum of per-kind capacities.

- [x] **Step 1: Add failing assembler tests for rect/glyph acceptance and ordering**

Pin: one valid rect and one valid glyph are recorded; a sprite→rect→glyph→sprite sequence retains that exact kind/index sequence; the 65th rectangle, 256th glyph, and 384th total submission fail before writing; invalid dimensions, non-finite values, null texture, and invalid UVs latch the first failure; `begin()` clears all records and counts.

- [x] **Step 2: Run the focused native test and confirm failure**

Run: `make host-test BUILD_ROOT=.dreamcast/host-tests/rounded-rect-text`

Expected: compilation fails because the overlay records and assembler calls do not exist yet.

- [x] **Step 3: Add host-testable overlay records and assembler validation**

Define the rectangle state with destination bounds, radius, border thickness, corner mask, fill and border colors; define a glyph as a textured quad with destination bounds, UV span and tint. Store per-kind records in fixed arrays and append `{kind, index}` to the single order array after validating capacity. Extend `IAuthoredFrameSink::present` to receive the overlay frame; do not change opaque geometry processing.

- [x] **Step 4: Run native tests**

Run the command from Step 2. Expected: PASS, including all existing geometry, sprite, and submission tests.

### Task 4: Add host-testable rounded-rectangle tessellation

**Files:**
- Create: `src/platform/dreamcast/DreamcastRoundedRectGeometry.hpp`
- Create: `src/platform/dreamcast/DreamcastRoundedRectGeometry.cpp`
- Create: `tests/native/DreamcastRoundedRectGeometryTests.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: validated `AuthoredRoundedRectState` and screen dimensions.
- Produces: a deterministic bounded triangle fan/strip representation for body and border, including the selected per-corner rounding; no KOS headers in the geometry API.

- [x] **Step 1: Write failing tessellation tests**

Pin a square with zero radius to four corners and two triangles; pin a rounded shape's selected corners and border ring to bounded non-degenerate triangles; pin all-corners-square when radius is zero and clamping when radius exceeds half the smaller extent.

- [x] **Step 2: Add invalid-input tests**

Pin rejection for NaN/infinite coordinates or radius, non-positive size, negative border thickness, and unknown corner bits. Assert no output is written on failure.

- [x] **Step 3: Run the geometry test and confirm failure**

Run: `make host-test BUILD_ROOT=.dreamcast/host-tests/rounded-rect-text`

Expected: the new test target fails to compile until the tessellator is added.

- [x] **Step 4: Implement deterministic rounded body and border tessellation**

Use a fixed maximum number of segments per rounded corner; clamp valid radii to half the smaller dimension; emit only selected corners as arcs and leave other corners square. Generate a ring for the border and a filled interior, applying the authored colors to vertices. Keep allocation out of the per-frame tessellator.

- [x] **Step 5: Run native tests**

Run the command from Step 3. Expected: PASS.

### Task 5: Implement FontAsset glyph layout as a host-testable module

**Files:**
- Create: `src/platform/dreamcast/DreamcastTextLayout.hpp`
- Create: `src/platform/dreamcast/DreamcastTextLayout.cpp`
- Create: `tests/native/DreamcastTextLayoutTests.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: a plain native `AuthoredFontView` of atlas dimensions, line height, space width, glyph code point, glyph UV rectangle, glyph offset and advance; drawable text, box size, font scale, alignment and wrap flag.
- Produces: bounded positioned glyph quads and `TextLayoutStatus`; no generated engine or KOS headers in the layout module.

- [x] **Step 1: Write failing layout tests**

Pin glyph destination size and offsets, atlas UV propagation, space advance, newline line-height, wrap at the authored width, left/center/right horizontal alignment, and the engine-compatible missing-glyph behavior (skip without emitting or advancing).

- [x] **Step 2: Write failing validation tests**

Pin rejection of zero/negative atlas dimensions, out-of-range or degenerate UVs, non-finite metrics, non-positive glyph advances when used as fallback width, non-finite/non-positive scale, and output overflow beyond 255 glyphs. Assert status and zero partial output on failure.

- [x] **Step 3: Run the new layout tests and confirm failure**

Run: `make host-test BUILD_ROOT=.dreamcast/host-tests/rounded-rect-text`

Expected: the new test target fails to compile until the view and layout API are implemented.

- [x] **Step 4: Implement deterministic line layout**

Implement the current engine builder's plain-text semantics: newline resets X and advances by `max(lineHeight * scale, 1)`; space advances by `max(SpaceWidth * scale, 1)`; a found glyph uses normalized atlas source rect, pixel width/height derived from atlas dimensions, glyph Y offset and positive advance (otherwise glyph width); missing glyphs emit and advance nothing. When wrapping is enabled, use the authored width divided by scale, rounded to the nearest integer and clamped to at least one; wrap at word spaces and hard-wrap long words at character boundaries. Resolve each line's left/center/right offset from its visible width and the authored box width. Reject malformed views before writing output.

- [x] **Step 5: Run native tests**

Run the command from Step 3. Expected: PASS.

### Task 6: Render the ordered overlay in the PVR translucent list

**Files:**
- Modify: `src/platform/dreamcast/DreamcastRasterRenderer.hpp`
- Modify: `src/platform/dreamcast/DreamcastRasterRenderer.cpp`
- Modify: `tests/native/DreamcastRasterSeamTests.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `GeometryFrame` plus `AuthoredOverlayFrame` from Task 3; rounded tessellation from Task 4; textured glyph quads from Task 5.
- Produces: one render-frame overload that validates all overlay references before `pvr_scene_begin`, opens the translucent list once when needed, and walks the mixed order stream without regrouping kinds.

- [x] **Step 1: Add host-testable validation and order tests**

Pin valid/invalid rectangle geometry, glyph UV bounds and texture references, all order entries within their per-kind count, and preservation of sprite/rect/glyph interleaving. Confirm malformed frames fail before the sink/list callback begins.

- [x] **Step 2: Implement renderer validation before scene submission**

Validate overlay capacities, indices, finite positions/colors, positive extents, UVs, and non-null textures before `pvr_scene_begin`. Keep the old two-argument diagnostic overloads delegating to an empty overlay so the diagnostic cube path is unchanged.

- [x] **Step 3: Implement translucent submission by order entry**

Open `PVR_LIST_TR_POLY` once, keep autosort disabled, and submit each ordered sprite, rectangle mesh, or glyph quad at overlay depth with depth writes disabled and culling disabled. Build textured headers from the correct `SpriteTexture`; build packed-color headers for shapes. Finish the list and scene on every opened-list path, propagating any PVR submission failure.

- [x] **Step 4: Run host tests**

Run: `make host-test BUILD_ROOT=.dreamcast/host-tests/rounded-rect-text`

Expected: PASS; no screenshot or image output is produced by native tests.

### Task 7: Gather rounded rectangles and text from generated drawables

**Files:**
- Modify: `src/platform/dreamcast/DreamcastRenderManager2D.hpp`
- Modify: `src/platform/dreamcast/DreamcastRenderManager2D.cpp`
- Modify: `src/platform/dreamcast/DreamcastRuntimeHost.cpp`
- Modify: `src/platform/dreamcast/DreamcastRuntimeHost.hpp` only if snapshot counter fields require a versioned contract comment
- Modify: `docs/GeneratedRuntimeContract.md` if emitted font runtime fields differ from the current documented contract

**Interfaces:**
- Consumes: generated `IRoundedRectDrawable2D`, `ITextDrawable2D`, `FontAsset`/glyph metadata and Dreamcast runtime textures; the shared assembler and overlay helpers from prior tasks.
- Produces: `DrawRoundedRect` and `DrawText` gathering into the common frame; a successfully presented frame reports shape/glyph counts.

- [x] **Step 1: Validate drawable gathering through the generated authored-scene integration gate**

The engine-generated drawable implementations are exercised by the local
`cube_test` integration fixture rather than copied behind a host-only adapter;
the gate requires nonzero rounded-rectangle and glyph counts and completes
241 frames with clean shutdown.

Pin owner position and world scale mapping for rounded bounds, font atlas texture ownership checks, conversion of engine font metadata to `AuthoredFontView`, and rejection of missing/disposed/foreign font textures. Keep engine-generated types behind a small adapter in `DreamcastRenderManager2D.cpp` so layout tests remain host-only.

- [x] **Step 2: Implement rounded-rectangle gathering**

Read only fields the engine command-list contract emits: parent world position/scale, size, radius, border thickness, corners, fill color and border color. Convert byte colors to unit floats and submit to the assembler; do not add Dreamcast-only rotation, `Color`, or `SourceRect` effects.

- [x] **Step 3: Implement text gathering**

Resolve the Dreamcast-owned font atlas texture and copy glyph metrics/rectangles into a bounded `AuthoredFontView`; use engine `FontAsset` line height/space width and the shared text layout module. Submit glyph quads in source render-queue order. Reject invalid fonts/metrics through the assembler's first-failure path.

- [x] **Step 4: Update the authored snapshot count packing**

Pack light, sprite, rounded-rectangle and glyph counts as four unsigned 8-bit lanes in the existing last snapshot word; the limits are respectively 4, 64, 64 and 255, so all fit. Increment the snapshot version to 4, update `DreamcastRuntimeHost.cpp` publication/logging, and update the matching `docs/GeneratedRuntimeContract.md` layout description. Preserve the 64-byte snapshot size.

- [x] **Step 5: Run host tests and compile the authored source**

Run: `make host-test BUILD_ROOT=.dreamcast/host-tests/rounded-rect-text`.
Then run the repository's authored build script with every build/generated/content/catalog root under `.dreamcast/rounded-rect-text/`; expected: successful compile with generated `FontAsset` members and no output written into another checkout.

### Task 8: Gate and report 2D counts without framebuffer capture

**Files:**
- Modify: `scripts/verify-authored-scene.ps1`
- Modify: `docs/Validation.md`
- Modify: `docs/AuthoredSceneBuild.md`
- Test: add `scripts.tests/VerifyAuthoredSceneScriptTests.ps1` only if a script-test harness already exists; otherwise exercise the count unpack/gates through the authored integration run.

**Interfaces:**
- Consumes: authored snapshot v4, packed counts from Task 7, and new optional `-MinimumRoundedRects` / `-MinimumGlyphs` parameters (both default zero).
- Produces: terminal outcomes `rounded-rects-below-gate` and `glyphs-below-gate`; passing report includes all four counts. Empty `-CaptureFrames ''` must produce no framebuffer files and no pixel parsing.

- [x] **Step 1: Add gates and version-4 count unpacking**

Update the Lua probe to require snapshot version 4 and decode lanes as `lights = packed % 256`, `sprites = floor(packed/256)%256`, `roundedRects = floor(packed/65536)%256`, and `glyphs = floor(packed/16777216)%256`. Add both minimum parameters, terminal result strings, and text output for the counts.

- [x] **Step 2: Keep verification capture-free for this feature**

Run the verifier with `-CaptureFrames '' -MinimumSprites 3 -MinimumRoundedRects 1 -MinimumGlyphs 1`. Confirm the evidence root contains only text/log metadata and no `.raw` or image files. Do not inspect image data.

- [x] **Step 3: Update documentation**

Describe rounded shapes, authored FontAsset atlases (256 cap), basic text layout, and explicit exclusions in `docs/AuthoredSceneBuild.md`. Add a text-only verification record in `docs/Validation.md` with build identity, verifier result/counts, and the explicit note that no image was captured or analyzed.

### Task 9: Full regression run and visible Flycast handoff

**Files:**
- No additional source files; this task validates the completed tasks.

- [x] **Step 1: Run complete test suites with local outputs**

Run: `make host-test BUILD_ROOT=.dreamcast/host-tests/rounded-rect-text` and `dotnet test builder.tests/helengine.dreamcast.builder.tests.csproj --artifacts-path .dreamcast/dotnet/artifacts -p:HelengineRoot=C:/dev/helworks/helengine`. Expected: all native and builder tests pass.

- [x] **Step 2: Build and verify the authored `cube_test` scene**

Use the current project/build inputs without modifying their checkout; route every generated source, content package, catalog, ELF, Flycast profile, and text evidence report under `.dreamcast/rounded-rect-text/`. Pass the verifier with capture disabled and minimum counts for the three existing sprites, at least one rounded rectangle and at least one glyph. Expected: successful ELF and verifier PASS.

- [x] **Step 3: Open Flycast visibly for user inspection**

Launch the exact verified ELF in a visible Flycast process using the repo-local profile. Leave the window open for the user. Do not capture or parse any image. User confirms whether rectangles and text appear correctly.

## Self-review checklist

- Spec coverage: support rules (Task 1), atlas capability/256 cap (Task 2), bounded records and mixed painter order (Tasks 3 and 6), rounded geometry (Task 4), text layout/metrics (Task 5), scene gathering and snapshot counts (Task 7), capture-free gates/docs (Task 8), local build and visible emulator handoff (Task 9).
- Placeholder scan: no `TODO`, `TBD`, or deferred implementation steps; exclusions are explicit.
- Type consistency: `AuthoredFontView` is the platform-neutral input to `DreamcastTextLayout`; its output is `AuthoredGlyphState`; assembler stores those glyphs and an order entry; renderer consumes the same `AuthoredOverlayFrame`.
- Review focus: atlas dimensions/metrics (Task 2); layout edge cases (Task 5); mixed-kind ordering (Tasks 3 and 6); invalid native geometry (Tasks 3 and 4); foreign font texture lifetime (Task 7).
