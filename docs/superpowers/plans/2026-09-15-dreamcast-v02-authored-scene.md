# Dreamcast v0.2 Authored Scene Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax. Use one sequential gpt-5.6-luna session at xhigh; no automatic additional agents or independent review.

**Goal:** Build a normal Helengine project into a Dreamcast ELF which loads and renders its first authored scene through generated Core.
**Architecture:** Reuse the generic platform/codegen/content graph. Embed declared cooked assets as KOS ROMFS, load through SceneManager, and adapt the camera's normal ordered drawable queue to the existing PVR renderer. Preserve the standalone cube as Diagnostic mode.
**Tech stack:** .NET 9; Helengine C#-to-C++ generator; C++17; pinned KOS/GCC 15.2; Docker; PowerShell 7; Flycast 2.7.
**Spec:** ../specs/2026-09-15-dreamcast-v02-authored-scene-design.md
**Status:** Draft for review; implementation has not started.

## Global constraints

The complete Scope and exact constraints section of the spec is binding. In particular:

- Platform ID dc; display name Sega Dreamcast; builder ID helengine.dreamcast.builder; version 0.2.0; graphics profile dc-unlit; material schema dc-unlit-v1.
- Preserve explicit Diagnostic and Authored build modes. Existing diagnostic selectors remain available. Authored mode requires generated Core and declared cooked content; missing inputs fail, never fall back to the cube.
- One startup scene, one enabled perspective camera, one enabled static MeshComponent, one submesh covering one complete triangle list and one opaque, single-sided, unlit material. Transform-only parent entities are supported.
- Mesh bounds: 1–64 positions, 1–128 triangles, exactly one nonempty index buffer (16- or 32-bit); validate all indices before converting to 16-bit. No skinning or animation. Normals/UVs may be present but are unused; textures and vertex-color material modes are rejected.
- Material uses authored BaseColor RGB, alpha 255, Lit=false, UseVertexColor=false, DoubleSided=false, empty texture path, correct renderer family/schema. No default white material on missing or invalid data.
- Camera uses authored world transform, near/far planes and clear RGB; full viewport (0,0,1,1), back buffer only, color/depth clear enabled, no stencil. Camera transform must be rigid; reject scaled/sheared cameras. Fixed 45-degree vertical FOV, aspect 640/480.
- Mesh world transforms must be finite and nonsingular with positive determinant; reject mirrored/singular transforms for this milestone. Nonuniform scale and transform-only hierarchy are supported.
- Retain 640×480 RGB565, opaque PVR list, reciprocal positive camera-space depth, greater depth test/write and the established exterior-face winding convention. One scene begin/end per presented frame.
- Native geometry capacity: at most 128 input triangles and 512 output triangles after sequential near/far clipping. Persistent output storage; no large frame-local stack arrays or silent truncation.
- One MiB maximum embedded cooked payload; eight MiB maximum resident ELF loadable span including BSS and ROMFS; at least four MiB measured free heap after startup. These are conservative v0.2 gates, not claimed hardware limits. Measure total target RAM use, do not count the ROMFS twice.
- Native renderer/content allocations occur during startup or explicit release, never per draw. Generated runtime allocation behavior must be measured and reported separately; do not claim it is allocation-free without evidence.
- No textures, lighting, physics, scripts, audio, UI, animation, scene switching, multiple cameras or disc packaging. Native Start exit remains supported; this is not a general engine input implementation.
- Reuse the locked SDK image and KOS commit. Keep generated sources, logs, staged content, .NET outputs and binaries under C:/dev/helworks/builds/helengine-dc/v02. Never repair generated C++ with postprocessing.
- Read applicable AGENTS.md; follow C# documentation/layout rules, one class per file, no tuples. Shared-engine/generator fixes need a focused source regression test.
- Do not merge, push, globally register a platform, or modify an existing user project. Acceptance uses an isolated settings root and fixture.

## Baseline, paths and references

Start from Dreamcast main f0c66b2 or its descendant; recheck revisions/dirty files. Create an isolated checkout at execution time. Do not resume the old v0.1 feature worktree accidentally.

Use $dcRoot for that checkout, $heRoot=C:/dev/helworks/helengine and $v02Root=C:/dev/helworks/builds/helengine-dc/v02. All paths below are relative to $dcRoot unless prefixed. Pass .NET --artifacts-path "$v02Root/dotnet" to keep obj/bin external.

The locked SDK is helengine-dc-sdk:v01, ID sha256:a33ea383336b428d8ac9bc507e00fd066092d4b0c8fcea29adccd61b2025f067; KOS fcfa7d869471591ca1c777543261a7bfea7cb726. Verify against toolchain-lock.txt. Do not rebuild it speculatively.

Read once:
- Native: DreamcastBootHost.*, DreamcastRasterRenderer.*, DreamcastGeometryPipeline.*; Makefile and scripts/build.ps1/launch_in_emulator.ps1.
- Helengine: engine/helengine.baseplatform/Builders/IPlatformAssetBuilder.cs, Requests/PlatformBuildRequest.cs, Manifest/PlatformBuildManifest.cs.
- Discovery: engine/helengine.platforms/DevelopmentPlatformProvider.cs, PlatformInstallationStore.cs, PlatformDescriptorStore.cs.
- Real command/settings fixture: scripts/tests/build-platform-real-editor-smoke.tests.ps1.
- Build graph: engine/helengine.editor/managers/project/EditorPlatformBuildGraphRunner.cs; locate EditorGeneratedCoreRegenerationService.cs with rg --files.
- Runtime: engine/helengine.core/CoreInitializationOptions.cs, managers/rendering/RenderManager3D.cs, model/interfaces/ICamera.cs, Entity.cs, assets/raw/ModelAsset.cs, assets/raw/platform/PlatformMaterialAsset.cs.
- Comparable source: C:/dev/helworks/helengine-n64-impl/builder and src/platform/n64; helengine-gc/builder/GameCubeMaterialCooker.cs; helengine-psp/src/platform/psp/rendering/PspRenderManager3D.cpp. N64 v0.2 is developing code, not a validated template.
- Pinned KOS: include/kos/fs_romdisk.h, Makefile.rules romdisk targets, kernel/romdisk/romdiskbase.c.

## Task 1 — Platform contract, cooker and real fixture

**Create:**
- builder/helengine.dreamcast.builder.csproj
- builder/DreamcastPlatformAssetBuilder.cs, DreamcastPlatformDefinitionFactory.cs
- builder/DreamcastMaterialSchemaIds.cs, DreamcastMaterialCooker.cs
- builder/IDreamcastNativeBuildExecutor.cs, DreamcastNativeBuildExecutor.cs
- builder.tests/helengine.dreamcast.builder.tests.csproj
- builder.tests/DreamcastPlatformDefinitionTests.cs, DreamcastMaterialCookerTests.cs
- tools/helengine.dreamcast.fixture/helengine.dreamcast.fixture.csproj, Program.cs, DreamcastAuthoredSceneFixtureWriter.cs
- scripts/prepare-authored-fixture.ps1

**Interfaces:** Implement the current IPlatformAssetBuilder exactly: Descriptor, Definition, CookMaterial and BuildAsync. Introduce IDreamcastNativeBuildExecutor.Build(PlatformBuildRequest request, string contentRoot, string catalogRoot, CancellationToken cancellationToken). Production BuildAsync must report a native-not-ready failure until connected; tests may inject an executor. Provide both a default constructor and an executor-injection constructor.

Fixture CLI: --output <directory> --variant baseline|transform|color|camera|geometry. Save project.heproj and normal scene/model/platform-material assets through current serializers. Baseline cube bounds [-1,1], mesh Y rotation 25 degrees, scale (1,1.25,1), opaque red; camera sees origin, near .1, far 100. No generated C++ geometry.

- [ ] Add .NET 9 projects with HelengineRoot override; builder references helengine.baseplatform/core/files. Fixture references actual current project/scene serialization services. Use substantive XML documentation.
- [ ] Write failing descriptor/cooker tests:
~~~csharp
[Fact]
public void DescriptorTargetsDreamcast() {
    var builder = new DreamcastPlatformAssetBuilder();
    Assert.Equal("dc", builder.Descriptor.TargetPlatformId);
    Assert.Equal("Sega Dreamcast", builder.Definition.DisplayName);
}
~~~
Construct material requests with the actual current constructor. Deserialize cooked bytes with AssetSerializer, assert authored RGBA and restricted flags. Cases: red/green succeed; alpha 254, lit, texture path, vertex-color mode and foreign schema fail with identified cause.
- [ ] Define debug/release, dc-unlit, default C++ codegen, little-endian serialization, pointer size 4 and native-column-vector math. Resolve setting/feature IDs from current APIs; disable unsupported features without removing scene/model/material support. Do not equate component type names with runtime feature IDs.
- [ ] Cook normal PlatformMaterialAsset; validate required fields instead of silently filling white/default material. Declare restricted component/schema capabilities.
- [ ] Generate the fixture through normal project/scene save and asset-reference services. Support independent variants by editing authored data.
- [ ] Write an isolated platforms.json following the real-editor smoke test: dc entry with actual matching engineVersion, external builderAssemblyPath, selected playerSourceRootPath and existing codegenToolPath. Set/restore HELENGINE_ENGINE_USER_SETTINGS_ROOT around the build.
- [ ] Run:
~~~powershell
dotnet test builder.tests/helengine.dreamcast.builder.tests.csproj --artifacts-path "$v02Root/dotnet" -p:HelengineRoot="$heRoot" --filter "FullyQualifiedName~DreamcastPlatformDefinitionTests|FullyQualifiedName~DreamcastMaterialCookerTests"
~~~
Then invoke the real generic graph on the fixture. It must discover dc and generate real Core. At this gate the unfinished native executor may fail explicitly; retain the actual generated root for Task 2. Record the full build command/settings and generated path; the executor failure is not a passed build.
- [ ] Commit the tested platform/cooker/fixture checkpoint locally.

## Task 2 — Minimum generated Core compatibility gate

**Create:** src/platform/dreamcast/DreamcastRuntimeHost.*, DreamcastContentStreamSource.*, DreamcastContentPath.*, DreamcastRenderManager3D.*, DreamcastRenderManager2D.*, DreamcastInputBackend.*; docs/GeneratedRuntimeContract.md; tests/native/DreamcastContentPathTests.cpp; scripts/tests/authored-build-input.tests.ps1.
**Modify:** src/main.cpp, Makefile, scripts/build.ps1.

**Interfaces:** Build script adds -Mode Diagnostic|Authored (default Diagnostic), -GeneratedCoreRoot, -ContentRoot, -CatalogRoot. All roots mandatory in Authored; absent Core.hpp/catalog is an error. An explicit -RuntimeInitProbe gate may initialize Core without loading a scene and use a minimal declared probe catalog/content; mark the manifest as a probe, never an accepted authored build. For this probe only, provide an empty RuntimeSceneCatalog and no scene load; its dedicated catalog source exports the Task 4 ABI with zero entries. Full Authored mode rejects that catalog. Keep this bypass behind RuntimeInitProbe and test the rejection.

DreamcastContentStreamSource implements the actual generated IContentStreamSource.OpenRead signature and only the file hooks required by generation. Record exact generated signatures/ownership before writing overrides.

- [ ] Add failing tests for absent roots/Core.hpp/catalog, compiler failure, diagnostic independence and no success manifest after failure.
- [ ] Inspect generated Core/CoreInitializationOptions/RenderManager3D/RenderManager2D/input/content/FileStream headers. Record ABI, ownership, exceptions, required source units, math layout and frame boundaries in GeneratedRuntimeContract.md. Generated signatures must come from this graph, not a sibling's output.
- [ ] Add separate source lists and deterministic generated-source response file; hash every compiled input. Read-only source mounts, external objects only.
- [ ] Implement minimum truthful adapters. The probe may perform zero work; unsupported nonempty render/input requests must fail. Construct/initialize/dispose real Core.
- [ ] Prefer generated FileStream stdio through KOS VFS if compatible. If its native-file hook is required, implement at the source seam and verify copy/ownership, short reads, seek and close behavior. Never return a stream backed by freed temporary memory.
- [ ] Define normalizeContentPath(const std::string&) -> std::string in DreamcastContentPath.*. Normalize backslashes, reject absolute/traversal/empty segments, embedded NULs and colon-bearing paths, then prefix /rd/. Tests:
~~~cpp
assert(normalizeContentPath("assets/models/cube.model") ==
       "/rd/assets/models/cube.model");
for (const char* path : {"", "../x", "a/../x", "/x", "C:/x", "a//b", "a/./b"}) {
    assert(contentPathRejected(path));
}
~~~
contentPathRejected is a test helper returning true only for the expected path exception.
- [ ] Cross-build the real generated probe with the locked SDK. Inspect ELF load segments including BSS/unwind and record generated-input hashes.
- [ ] Boot to Core-ready and clean shutdown; record resident span and free heap. Use a bounded internal probe or a visible desktop-account launch. If a narrow generated-source defect occurs, fix generator/runtime source plus a focused regression, never emitted C++.
- [ ] Commit after Gate A passes.

**Gate A:** Generated Core compiles, links, initializes and shuts down within the spec memory budget. Broad runtime/compiler changes or memory-gate failures require an evidenced scope decision before continuing.

## Task 3 — Bounded authored geometry and matrix bridge

**Create:** src/platform/dreamcast/DreamcastAuthoredTransform.*, DreamcastRuntimeModel.*, DreamcastRuntimeMaterial.*; tests/native/DreamcastAuthoredTransformTests.cpp, DreamcastAuthoredMeshTests.cpp.
**Modify:** DreamcastRenderManager3D.*, DreamcastGeometryPipeline.*, DreamcastCubeMesh.*, DreamcastBootHost.*, existing native tests, Makefile.

**Interfaces:** Rename CubeMeshView to MeshView consistently, retaining pointer/count fields and cubeMesh() diagnostic provider. Add farPlane to GeometryConfig; explicitly initialize at every call site. Set GeometryFrame.kMaxTriangles=512 and move frame storage into persistent host/renderer state.

~~~cpp
// Existing row-major storage; column-vector multiplication.
bool makeCameraFromModel(const Matrix4x4& worldFromModel,
                         const Matrix4x4& worldFromCamera,
                         Matrix4x4& cameraFromModel) noexcept;
bool validateAuthoredMesh(const MeshView& mesh) noexcept;
~~~

The bridge validates finite/nonsingular positive-determinant model matrices and rigid camera matrices; computes inverse(cameraWorld)*modelWorld plus one verified engine-view to positive-Z conversion. Convert generated matrix fields in one named adapter; document/test basis vectors. Do not reconstruct exact hierarchy matrices from decomposed world S/R/T.

- [ ] Write failing transform fixtures. Camera sees a model one unit right and five units forward:
~~~cpp
Matrix4x4 result{};
assert(makeCameraFromModel(modelWorld, cameraWorld, result));
assertPointNear(transformPoint(result, {0,0,0}), {1,0,5});
~~~
Construct modelWorld/cameraWorld explicitly using the verified engine forward basis. Test-only transformPoint/assertPointNear implement plain matrix multiplication/epsilon comparison. Also compare a generated engine transform and bridge result for a rotated, translated, nonuniformly scaled parent plus child.
- [ ] Test 65 positions, 129 triangles, nonfinite positions, invalid indices, dual index buffers, incomplete triangle list, mirrored/singular transforms. Reject before submission. ModelAsset validation tests cover index widths/submesh fields; MeshView tests cover converted buffers.
- [ ] Clip against near AND far in camera space with checked polygon append. Current appendUnique silently caps four vertices; replace it with explicit bounded storage/error handling. Sequential triangle clipping may conservatively emit four triangles; 128*4=512 output bound. Test all-beyond-far, near crossing, far crossing, both planes, exact-on-plane, and maximal legal input without truncation.
- [ ] Assert camera movement/rotation, near/far, projection, hierarchy, winding and reciprocal-depth behavior with numeric expectations. Never compensate for a bad matrix using arbitrary reversed indices.
- [ ] Convert cooked model once into native owned arrays. Support exactly one submesh spanning all indices and its exact material slot; convert validated 32-bit indices to 16-bit. Dispose the deserialized model after copying, or retain it with clear ownership if required—never use freed borrowed buffers.
- [ ] RuntimeMaterial stores validated authored RGB/opaque state; no silent default. Define one disposal owner on success and every failure path.
- [ ] Run native math/mesh/raster tests plus ASan/UBSan on host-compatible code. Rebuild/run Diagnostic once after the shared geometry changes and check exterior faces.
- [ ] Wire the generated cooked model/material construction and release overrides into DreamcastRenderManager3D now, before Task 4 scene loading. Deserialize via content source, validate types/submesh/material, create owned resources and close/dispose on every path. Drawing may still report not-ready; loading must work. Test these hooks against real cooked bytes on the generated runtime.
- [ ] Commit the tested bridge.

## Task 4 — Manifest-owned package and startup scene

**Create:** builder/DreamcastSceneCompatibilityValidator.cs, DreamcastContentStager.cs, DreamcastStartupCatalogWriter.cs, DreamcastStartupCatalogEntry.cs; src/platform/dreamcast/DreamcastSceneBootstrap.*; builder.tests/DreamcastContentStagerTests.cs, DreamcastStartupCatalogWriterTests.cs, DreamcastBuildFailureTests.cs.
**Modify:** DreamcastPlatformAssetBuilder.cs, DreamcastNativeBuildExecutor.cs, Makefile, scripts/build.ps1, DreamcastRuntimeHost.*.

**Interfaces:**
- DreamcastContentStager.Stage(PlatformBuildManifest manifest, string cookedRoot, string freshStagingRoot, CancellationToken cancellationToken) returns normalized staged relative paths.
- DreamcastStartupCatalogWriter.Write(string catalogRoot, string startupSceneId, IReadOnlyList<DreamcastStartupCatalogEntry> entries) writes dc_runtime_scene_catalog.hpp/.cpp outside generator-owned source.
- Each entry has SceneId and CookedRelativePath strings. Validate all entries before writing; sort deterministically.
~~~cpp
struct HEDcSceneCatalogEntry {
    const char* sceneId;
    const char* cookedRelativePath;
};
extern "C" const char* he_dc_startup_scene_id();
extern "C" const HEDcSceneCatalogEntry*
he_dc_scene_catalog_entries(std::size_t* count);
~~~

- [ ] Add tests: exact startup mapping; duplicate/missing IDs; declared missing file; ambiguous association; traversal; normalized/case collision; unknown required feature; payload over 1 MiB. A misleading lone scene file must not satisfy an unrelated ID.
- [ ] Resolve associations through actual manifest IDs/payload references and test the generic graph's mapping. No basename matching, arbitrary lone-artifact fallback or directory globbing.
- [ ] Validate supported scene/model/material data using normal deserializers where available. Revalidate cardinality/types at runtime: feature metadata alone does not prove scene shape.
- [ ] Stage declared cooked artifacts in stable ordinal path order into a fresh directory. Normalize staged file timestamps to one declared build epoch and verify identical content yields the same ROMFS bytes. Reject root overlap and escaping symlinks/reparse points. Never recursively delete an existing caller-supplied root.
- [ ] Create ROMFS with pinned KOS genromfs, bin2c and romdiskbase tooling. Stock KOS rules write intermediates in the working directory; add equivalent targets explicitly under BUILD_ROOT. Link the image once and record package/file hashes and size. If container lacks genromfs, add only that pinned utility through a reproducible SDK-layer change, not a full compiler rebuild.
- [ ] Use one mount owner: KOS_INIT_ROMDISK(romdisk) with normal KOS flags for /rd automount. Verify exact pinned symbols/rules; do not also mount /rd manually. Linked ROMFS memory remains resident.
- [ ] Construct the generated RuntimeSceneCatalog from the ABI; install content source/catalog before Core initialization. Load startup via SceneManager.LoadScene and commit at its required boundary; no custom scene interpreter.
- [ ] Log startup ID, loaded camera/drawable counts, model/material identities, clear settings and clip planes. Require exactly one enabled camera/drawable. Distinguish this load-only gate from rendering.
- [ ] Propagate cancellation/compiler errors from BuildAsync. Build in a fresh native subdirectory; expose ELF/map/success manifest only for a verified current invocation. Hash native/generated/content inputs before and after compilation and reject a race. Keep artifact paths resolvable from the returned build output.
- [ ] Run builder/content tests and one actual generic-graph load probe, then commit.

**Gate B:** Core loads the graph's scene from /rd with expected identities and cardinality. It does not yet establish raster success.

## Task 5 — Core submissions and application lifecycle

**Modify:** DreamcastRenderManager3D.*, DreamcastRuntimeHost.*, DreamcastRasterRenderer.*, src/main.cpp.
**Create:** tests/native/DreamcastAuthoredSubmissionTests.cpp.

**Interfaces:** Use actual generated overrides recorded in Task 2 for cooked model/material overloads, release hooks and ordered queue visitor. Extend renderFrame to accept validated clear RGB with GeometryFrame; diagnostic passes its existing dark color.

- [ ] Introduce a generated-header-free submission seam with a fake sink. One camera/model/material must deliver the authored matrix/color/clear/depth state and exactly one frame submission. Invalid material/camera must fail before packets. Zero visible triangles after legitimate clipping is a valid clear-only frame.
- [ ] Use Entity.WorldTransformMatrix through the tested adapter. Read camera world transform/clip planes/clear/queue and enforce restrictions. Use pi/4 vertical FOV deliberately; do not reuse diagnostic 1.10 radians.
- [ ] Deserialize expected cooked asset types through the normal content source, validate, construct native runtime resources and close/release streams/assets on all paths. Unsupported raw-asset methods fail; do not unexpectedly bypass cooked input.
- [ ] Visit the camera's ordered render queue, preserving enabled/layer filtering and material-submesh binding. Do not fetch a hidden global cube or add diagnostic rotation.
- [ ] Implement one lifecycle:
~~~text
monotonic delta -> bounded Core.Update
pending scene commit at configured update boundary
Core.Draw -> validate/gather authored submission -> CPU geometry
PVR wait-ready -> set authored clear for this frame
scene-begin -> opaque-list-begin -> packets -> list-finish -> scene-finish
native Start / optional explicit timed-probe stop
~~~
Map this sequence to actual Core callbacks; no second begin/end or presentation outside the adapter. Keep persistent scratch buffers and no per-frame native allocations.
- [ ] Add a versioned authored diagnostic snapshot (or separate symbol). Do not silently change the existing 80-byte v0.1 ABI. Expose stage/error, scene digest, counts, memory and first completed frame; resolve its address with nm per ELF.
- [ ] On shutdown, wait for rendering before releasing resources. Use the generated ownership record to avoid double Dispose/delete of Core/options/catalog/content/backends; then stop PVR/video. Catch/report exceptions at host entry and identify failing stage. Start exit emits an explicit shutdown marker.
- [ ] Run submission tests and the real scene in Flycast. Inspect internal RGB565 framebuffer or the visible user desktop window. No desktop/window screenshots without permission; nonzero pixel hashes alone are insufficient visual evidence.
- [ ] Commit after Gate C.

**Gate C:** The screen shows actual authored camera/model/material data and Diagnostic remains functional.

## Task 6 — End-to-end changes, evidence and handoff

**Create:** scripts/verify-authored-scene.ps1, docs/AuthoredSceneBuild.md.
**Modify:** docs/Build.md, docs/Validation.md, fixture variants and negative tests.

- [ ] Complete prepare-authored-fixture.ps1 to create the fixture and scoped settings at known external paths. Emit the actual generic command; use current matching engine/generator identities:
~~~powershell
$previousSettings = $env:HELENGINE_ENGINE_USER_SETTINGS_ROOT
try {
    $env:HELENGINE_ENGINE_USER_SETTINGS_ROOT = "$v02Root/engine-user-settings"
    & "$heRoot/scripts/build-platform.ps1" -Project "$v02Root/fixtures/baseline/project.heproj" -Platform dc -BuildProfile debug -Output "$v02Root/acceptance-baseline" -CacheRoot "$v02Root/cache" -WorkspaceRoot "$v02Root/workspaces"
    if ($LASTEXITCODE -ne 0) { throw 'Authored Dreamcast build failed.' }
} finally {
    $env:HELENGINE_ENGINE_USER_SETTINGS_ROOT = $previousSettings
}
~~~
Check current wrapper semantics before invoking. Codegen cache reuse is allowed; output identities must remain verifiable and failed builds cannot reuse success.
- [ ] Independently generate/build baseline, transform, color, camera and geometry variants:
  - baseline: red authored transformed cube;
  - transform: move mesh one unit camera-right;
  - color: green, unchanged mesh/camera;
  - camera: move camera one unit camera-right, unchanged mesh (opposite projected movement);
  - geometry: halve the model's local X coordinates, unchanged entity transform.
Compare authored/cooked data, content/ELF hashes and predicted visible output. No native source changes between variants. A hash difference alone does not prove the authored content was rendered.
- [ ] Run negative fixtures: missing model, wrong startup association, lit/textured/transparent material, two cameras and over-limit mesh. Require identified error and no success manifest; verify launcher rejects absent/invalid current output.
- [ ] Run accepted baseline for at least 60 wall seconds, record frames/errors/native allocations or heap samples, then verify Start shutdown and missing-controller operation. Report generated-runtime allocations separately. Keep physical hardware pending.
- [ ] Rebuild Diagnostic and run affected native tests. Run focused shared-engine/generator tests only if their source changed. Do not repeat all v0.1 probes without a new reason.
- [ ] Load the actual authored build-manifest.txt into $authoredManifest (key/value parser), resolve its directory into $authoredOutputRoot and launch its declared BuildId:
~~~powershell
pwsh -NoProfile -File "$dcRoot/scripts/launch_in_emulator.ps1" -BuildId $authoredManifest.BuildId -OutputRoot $authoredOutputRoot -FlycastPath C:/dev/helworks/emus/dreamcast/flycast.exe -Visible
~~~
OutputRoot is the parent directory containing the BuildId directory, not the directory containing the ELF directly. The verifier must derive both from the published manifest/output contract and assert the ELF hash first. From Codex use the desktop account outside the sandbox. Confirm owner Helena, a nonzero window handle and title; process existence alone does not prove visibility.
- [ ] Record platform/engine/generator revisions and dirty state, source/generated/content hashes, SDK, ELF/map, loadable/BSS/ROMFS sizes, heap evidence, test output, emulator version/hash and visual evidence. Link actual artifacts. Document exact user launch commands and the fixed FOV/subset limits.
- [ ] Review all touched repository diffs, make local checkpoint commits, report the result. No automatic merge/push.

**Gate D:** Independent authored changes reach the screen through the real build graph; failures are explicit; Diagnostic survives.

## Self-review and execution handoff

Coverage: platform/cooker/fixture (1), real Core compatibility (2), native geometry/resource conversion (3), manifest/package/scene load (4), rendering/lifetime (5), real-project proof and documentation (6). Generated ABI is inspected before adapters are implemented. Hardware, general engine input, authored FOV and broader graphics remain deferred.

When blocked, report exact compiler/link/runtime evidence, relevant paths, tested hypothesis and smallest next change. Continue routine integration fixes. Request a scope decision only for demonstrated architectural expansion; do not switch models, rewrite emitted C++, silently raise limits or repeatedly rebuild the SDK.

Suggested execution prompt:

> Implement docs/superpowers/plans/2026-09-15-dreamcast-v02-authored-scene.md with gpt-5.6-luna at xhigh, sequentially and without extra agents. Read the linked design and repository instructions. Start from current Dreamcast main in an isolated checkout, preserve Diagnostic mode, reuse the pinned SDK and pass the minimum generated-Core gate first. Use the real Helengine graph/cooked assets and embedded ROMFS; render authored matrices/camera/material and prove independent authored edits on screen. Keep generated/build artifacts external, commit checkpoints locally, and do not merge or push.
