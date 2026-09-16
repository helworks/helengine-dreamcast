# Dreamcast v0.3 per-vertex lighting design

**Status:** approved in discussion on 2026-09-16; implementation not started.
**Goal:** light authored meshes on Dreamcast with the engine's directional and ambient lights so that demodisc `cube_test` renders its sun the way GameCube does.
**Baseline:** `main` of `helengine-dc` at `eb73fb5` (cube_test boots, spins and presents unlit through generated Core).

## Scope

In scope:

- Directional and ambient lights, per-vertex Lambert diffuse computed on the SH4, fed to the existing Gouraud vertex-color path.
- Material `lighting-mode` with `lit` (default) and `unlit`; platform ids renamed to `dc-standard-v1` / `dc-standard`.
- Model normals converted into the native mesh.
- Validation, tests, evidence and docs for the above.

Out of scope: point and spot lights, specular, shadows, textures, per-pixel anything, changes to the geometry pipeline or the raster renderer, N64 behaviour, engine or generator changes.

## Data and material contract

- `SourceVertex` gains `Vec3 normal` after `position`. The diagnostic cube literal supplies zero normals; the diagnostic host never reads them, so Diagnostic output is unchanged (its static-pose capture hash `2c13dc96…` must still match).
- `MeshView` gains `bool hasNormals`. `DreamcastRuntimeModel::CreateOwned` copies `ModelAsset.Normals` when the array is nonempty; a normal count different from the position count fails conversion with an identified message. Nonfinite normals fail conversion. Normals are stored as authored (renormalized at lighting time).
- Platform definition: material schema `dc-standard-v1`, graphics profile `dc-standard`, renderer family `dc-standard`. `lighting-mode` choices are `lit` and `unlit`, default `lit`. The remaining fields keep their current meaning and defaults (texture empty, transparency opaque, double-sided false, vertex-color ignore, base color required).
- Cooker: `Lit` on the cooked `PlatformMaterialAsset` follows `lighting-mode`; `RendererFamilyId = "dc-standard"`. Everything else unchanged.
- `DreamcastRuntimeMaterial::Create` accepts renderer family `dc-standard`, both lit states, and keeps rejecting textures, double-sided, vertex-color and non-opaque materials. It exposes `bool lit()`.
- Contract rules (explicit failures, never silent fallbacks):
  - lit material bound to a model without normals: frame status `lit-mesh-without-normals`;
  - more than four enabled directional lights: frame status `too-many-directional-lights`.
- No implicit ambient term. With no ambient light entity, faces facing away from every directional light are black, matching GameCube.

## Light gathering

`DreamcastRenderManager3D::Draw` gathers lights once per frame before visiting the camera queue:

- `ObjectManager.DirectionalLights` and `AmbientLights`, keeping entries whose owning entity `get_IsHierarchyEnabled()` is true. Layer masks are not applied to lights (the engine does not layer-filter lights either).
- Ambient contribution: `Σ color.rgb × intensity` over ambient lights.
- Directional contribution: for each light, world direction is the engine forward axis `(0, 0, −1)` rotated by the rotation part of the light entity's world matrix (`get_WorldTransformMatrix`, converted through the existing `matrixFromGenerated`), then normalized; RGB is `color.rgb × intensity`.
- The result is a generated-header-free `AuthoredLightState { ColorRgba ambient; uint8_t directionalCount; AuthoredDirectionalLight directional[4]; }` passed to the assembler with `setLights`. `AuthoredDirectionalLight` is `{ Vec3 direction; ColorRgba color; }`.

## Lighting math (frame assembler)

For each drawable with a lit material and `hasNormals`:

- Normal matrix `Nm = transpose(inverse(upper3x3(worldFromModel)))`; the existing `validateModelTransform` already guarantees a finite positive-determinant matrix, so the inverse exists.
- Per vertex: `n = normalize(Nm × normal)`; `diffuse = ambient + Σ_i lightRgb_i × max(0, dot(n, −direction_i))`; `color = clamp01(base × diffuse)`, alpha 1. Written into the existing persistent scratch vertex buffer that the assembler already fills with the material color.
- Unlit materials keep today's behaviour (flat base color).
- The assembler's summary records `directionalLightCount` and whether the drawable was lit. Everything downstream (geometry clipping, projection, raster packets) is unchanged.

## Diagnostics

The authored snapshot moves to version 2: the `reserved` word becomes `directionalLightCount` (0 when the material is unlit or no lights are enabled). Layout size stays 64 bytes. The verifier's Lua reads it; existing readers of version 1 fields are unaffected.

## Builder and validation

- `DreamcastMaterialSchemaIds`: `StandardSchemaId = "dc-standard-v1"`, `StandardGraphicsProfileId = "dc-standard"`, lighting values `lit` / `unlit`.
- `DreamcastCookedContentValidator`: engine components allowed in the pruned startup scene become camera, mesh, `helengine.DirectionalLightComponent` and `helengine.AmbientLightComponent`; lit materials pass; renderer family check moves to `dc-standard`.
- `DreamcastSceneCompatibilityValidator`, stager, catalog writer: unchanged.
- The rename is applied to the platform definition factory, cooker, tests, fixture tool, docs, and the `dc` entry in `C:\dev\helprojs\demodisc\user_settings\build_config.json` (`selectedGraphicsProfileId`).

## Project and fixture changes

- `CubeTestSceneFactory` (demodisc, uncommitted like the earlier `dc` edits): two exclusion sets. Instruction overlay, console instructions, UI root and orbit component are excluded on `n64` and `dc`; the directional light is excluded on `n64` only. Rebuild the generated code projects, then regenerate the scene through `menu.generate-cube-test-scene`, and refresh the scene hash in the `dc` build entry.
- Fixture tool: baseline and the existing variants explicitly author `lighting-mode = unlit` so their acceptance meaning is unchanged; new positive variant `lit` authors a lit red material plus one white directional light (yaw −0.65, pitch −0.85 like cube_test) and no ambient light.

## Testing

- Host `DreamcastAuthoredSubmissionTests` (fake sink, no generated headers): facing-light vertex gets full light color; facing-away vertex gets ambient only; black without ambient; clamping; nonuniform scale keeps transformed normals unit and perpendicular (inverse-transpose); unlit material ignores lights; lit material without normals fails before presentation; five directional lights fail; ambient-only lighting; direction derived from a rotated world matrix equals rotated `(0,0,−1)`.
- Builder tests: schema default lit, explicit unlit cooks, cooker still rejects unsupported fields, validator accepts light components and lit materials, definition test covers the rename.
- Native: full `make host-test`, ASan/UBSan on the submission suite, Diagnostic static-pose rebuild with capture hash equal to `2c13dc96efabe106aae6beac98426f32eea4d8637e18360a8f7efccbde2ae804`.
- Emulator: cube_test rebuilt through the graph; hidden verifier PASS; captured frames show at least two distinct brightness levels of the base color on visible faces and differ between loop frames 120 and 180; clean Start shutdown; visible launch as Helena. The `lit` fixture variant verified the same way; the unlit baseline variant re-verified with its frame hash unchanged from `774e28d5…` apart from the renamed renderer family (documented if the hash moves).
- Docs: `AuthoredSceneBuild.md`, `Validation.md`, `GeneratedRuntimeContract.md` material section.

## Constraints carried over

Native allocations only at startup or explicit release; no per-frame allocation (scratch buffers are already persistent). One PVR scene per frame. Budgets unchanged (one MiB ROMFS, eight MiB resident span, four MiB free heap). No generated C++ is edited; engine and generator sources are untouched. No merge or push without a request.
