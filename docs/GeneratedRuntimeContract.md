# Dreamcast generated runtime contract

This document records the generated ABI and native compatibility evidence for
Tasks 2 and 3. The generated graph is an external, read-only input. No
generated source is edited in the Dreamcast worktree.

## Generated graph identity

The graph reached the expected native-not-ready boundary in
`v02/root-graph-generator-v4`. Its authoritative root was:

`C:/dev/helworks/builds/helengine-dc/v02/root-graph-generator-v4/cache/v2/c829f5140cb3c2dd14e34a15a209d15a/b/dc/debug/release/generated-core`

The graph log is `v02/execution/root-graph-generator-v4-console.log`. The
isolated Helengine source used for graph fixes ended at `5a0696ca`; the
isolated generator source includes `e35366b` for the generic file descriptor
capability fix and `c1d476f` for the pre-open capability guard, after the
earlier Math, Split and FileStream compatibility fixes. The pinned Release
payload hashes are recorded in
`v02/execution/codegen-release-payload-sha256.txt`.

Selected generated input hashes from the graph root are:

```text
Core.hpp                                  B8D5812286258B31958D2FE65B755F61BC28ABDA31FFCD50FC86ADFEBA8F8982
Core.cpp                                  ADF2A3B5C8EE7DFA00EFDF34767E84EA7586D1008461E2ED15FF8D0B4DA5DAF4
RenderManager3D.hpp                        C234B2CFB884162A0F103432BA8C58E2F8914A57E6ABA15C3DB8CEC8BC1212C9
RenderManager3D.cpp                        B71A583FE8A2D275AB218C62BF9F8857751965F27153CF3857B088658D1627C6
RuntimeContentManagerConfiguration.hpp     7E315F52285FD8903D4728B8CA5A4DBD34D28D8EFF03F4D76278B7766785E839
helcpp_config.hpp                           502234296362D93E7FE9C9B5A532EEC2C9C43101F5B6AD5E2A84CCCE017EC0E1
```

## Core and adapter ABI

The generated `Core.hpp` exposes these lifecycle methods:

```cpp
Core();
Core(CoreInitializationOptions* options);
void Initialize(RenderManager3D*, RenderManager2D*, IInputBackend*,
                PlatformInfo*, CoreInitializationOptions*);
void Update(double elapsedSeconds);
void Draw();
void CompleteFrameBoundary();
void Dispose();
```

The exact adapter signatures are:

```cpp
class IContentStreamSource {
public:
    virtual Stream* OpenRead(std::string assetPath) = 0;
};

class IInputBackend {
public:
    virtual InputFrameState CaptureFrame() = 0;
};

class RenderManager3D {
public:
    virtual RuntimeModel* BuildModelFromRaw(ModelAsset* data) = 0;
};

class RenderManager2D {
public:
    virtual RuntimeTexture* BuildTextureFromRaw(TextureAsset* data) = 0;
    virtual void DrawRoundedRect(IRoundedRectDrawable2D*) = 0;
    virtual void DrawSprite(ISpriteDrawable2D*) = 0;
    virtual void DrawText(ITextDrawable2D*) = 0;
protected:
    virtual void UpdateTextureRegionCore(RuntimeTexture*, int32_t, int32_t,
                                         int32_t, int32_t, Array<uint8_t>*,
                                         int32_t) = 0;
};
```

`Core::Initialize` stores the passed platform and adapter pointers. The native
host owns those objects and calls `Core::Dispose` before deleting them.
`Core::Dispose` disposes the render managers and generated registrations; it
does not delete objects passed to `Initialize`, and it is not documented as a
full generated heap reclamation operation.

The generated profile reports `HE_CPP_USE_EXCEPTIONS=0` and
`HE_CPP_USE_RTTI=0`. Generated failures nevertheless use owned
`::Exception*` pointers at this ABI boundary. The host catches and deletes
those pointers, then handles native `std::exception` and unknown failures.

The normal frame order is `Core::Update`, `Core::Draw`, and
`Core::CompleteFrameBoundary`. The Task 2 probe performs one zero-delta update,
draw, and explicit boundary call with `CommitPendingSceneOperationsDuringDraw`
disabled. It does not claim authored scene loading or rendering.

## Content and generated FileStream

`DreamcastContentStreamSource::OpenRead(std::string)` is the generated content
interface. Its `/rd/` hooks normalize separators, reject absolute, traversal,
empty-segment, colon-bearing and embedded-NUL paths, bound cooked files to one
MiB, use RAII for `FILE*`, reject short reads, and pass a copied byte buffer to
the generated `FileStream(const uint8_t*, size_t)` constructor. The copied
buffer remains valid after the file is closed.

The code generator now emits the generic capability define
`HE_CPP_RUNTIME_HAS_FILE_DESCRIPTOR_APIS`. Existing profiles default to `1`;
limited-libc profiles set the generic codegen option
`native-file-stream-descriptor-apis=false`. Dreamcast selects that option in
its platform codegen profile. With the capability disabled, file-backed
mutation/query paths fail before `fopen`, while memory-backed read, seek and
resize remain available. The runtime template contains no Dreamcast-specific
descriptor branch.

## Probe catalog ABI

The probe consumes a dedicated catalog source supplied through `CatalogRoot`.
The required external files are `dc_runtime_scene_catalog.hpp` and
`dc_runtime_scene_catalog.cpp`; the Makefile compiles that source as a native
object. The source exports the Task 4 ABI:

```cpp
struct HEDcSceneCatalogEntry {
    const char* sceneId;
    const char* cookedRelativePath;
};

extern "C" const char* he_dc_startup_scene_id();
extern "C" const HEDcSceneCatalogEntry*
he_dc_scene_catalog_entries(std::size_t* count);
```

The Task 2 probe's external implementation returns a null startup ID, a null
entry pointer and count zero. `createProbeSceneCatalog` verifies those values
before constructing the generated empty `RuntimeSceneCatalog`. Authored mode
rejects this probe-only bypass, and the build-input test covers missing ABI
files, non-probe catalogs, compiler failure and absence of a success manifest.

## Native compilation

The generated root's `generated_unity.cpp` is compiled once. The build then
compiles only the finalized units absent from that unity file:

```text
GeneratedRuntimeCameraComponentDeserializer.cpp
GeneratedRuntimeMeshComponentDeserializer.cpp
GeneratedRuntimeModuleRegistration.cpp
runtime/runtime_code_module_manifest.cpp
runtime/runtime_physics3d_scene_feature_manifest.cpp
runtime/runtime_scene_catalog_manifest.cpp
runtime/runtime_standard_platform_input_manifest.cpp
runtime/runtime_startup_manifest.cpp
```

The source-list check removes CRLF before parsing unity includes and rejects
duplicates. The generated root requires C++20 because the pinned runtime emits
`requires`, `bind_front` and `in_range`; native sources retain strict
`-Wall -Wextra -Werror`. Generated compatibility warnings remain visible and
are non-fatal only for generated units. The finalized v0.2 input contains no
`AddOwned` calls, so the visible generated non-virtual-dtor warning has no
owning path in this closed program.

## Heap and runtime evidence

The checked heap value is:

```text
heapLimit       = _arch_mem_top - THD_KERNEL_STACK_SIZE
untouchedHeap   = max(heapLimit - uintptr_t(mm_sbrk(0)), 0)
checkedFreeHeap = untouchedHeap + max(mallinfo().fordblks, 0)
```

This separates untouched heap from allocator arena, allocated and free
components and avoids counting ROMFS or the ELF twice. The final Gate A probe
output is `v02/execution/root-probe-v4/runtime-probe.csv` (verified by the root
task) and reports at vblank 41:

```text
magic=48455250 version=1 status=5 error=0 stage=6
startFree=14615184 coreReadyFree=14608776
coreDisposedFree=14608704 shutdownFree=14609488
coreReady=1 frameComplete=1 coreDisposed=1 cleanShutdown=1
```

The linked final Gate A ELF is
`v02/execution/root-gate-a-v4/helengine_dc.elf`, SHA-256
`339378133d3e2fe75f462239596c7d25d97305b87f16930316ea20611a6101d6`. Its
resolved probe symbol is `_helengine_dreamcast_runtime_probe` at `0x8c1e1954`.
The inspected ELF loadable resident span including BSS is 1,926,476 bytes
(1.837231 MiB), below the eight MiB gate. SDK image
`sha256:a33ea383336b428d8ac9bc507e00fd066092d4b0c8fcea29adccd61b2025f067`
and KOS commit `fcfa7d869471591ca1c777543261a7bfea7cb726` were unchanged.

The generic FileStream capability checks are recorded under
`v02/execution/root-generic-stream-check`; both capability paths passed the
100-iteration ASan/UBSan ownership and descriptor checks. Native geometry,
raster and content-path tests, the 10-case builder suite and authored-build
input tests passed in their external checkpoint directories.

## Authored matrix, mesh and resource bridge

The generated `float4x4` ABI was verified from the emitted headers and source:
translation is stored in `M41..M43`, and `Entity::get_WorldTransformMatrix`
multiplies `local * parent`. The bridge therefore transposes generated fields
into the native column-vector `Matrix4x4` layout. It validates finite affine
model matrices with a positive determinant, and finite rigid camera matrices
with orthonormal columns. It inverts the rigid camera and reflects only the
camera-space Z row because generated camera forward is `-Z` while the native
geometry stage consumes positive depth.

That Z reflection reverses screen winding. Diagnostic geometry retains its
positive screen-space counterclockwise cull contract; authored submission can
select clockwise front faces through `RendererConfig::frontFaceWinding`. The
bridge does not flip X or rewrite authored indices. The fixed-pose raster seam
test records the outward authored `+Z` face as negative projected area after
the reflection and the opposite face as positive.

`MeshView` accepts 1..64 positions and 1..128 input triangles. Near and far
planes are clipped sequentially with interpolated colors; output is bounded
to 512 triangles and errors clear the frame before returning. The frame is
persistent on the host heap, and the pinned SH4 stack audit records
`processGeometry` at 976 bytes plus a 148-byte clip lambda, `makeGeometry` at
136 bytes and `runDreamcastHost` at 308 bytes. No 43 KiB `GeometryFrame`
temporary remains on those paths.

Cooked model conversion copies positions, indices, bounds and one submesh into
owned native arrays, preserves the source model ID and retains the generated
asset only through `RuntimeModel::SetRawModelAsset`. Material conversion copies
the validated `dc-standard` opaque color and render state, including the
cooked `Lit` flag (`PlatformMaterialAsset::Lit`), set from the authored
`lighting-mode` field (`unlit`/`lit`, cooked schema `dc-standard-v1`).
Borrowed raw-model creation is rejected without consuming its argument. Every
adapter failure and release path is RAII guarded, and former-authoring-ID
cleanup recognizes the generated `Array<T>::Empty()` static singleton before
deleting metadata.

The focused host suite is recorded under
`v02/execution/task3-host-check-07` and covers geometry, matrix, mesh, raster
and content paths, including 128 legal nondegenerate inputs producing 384
clipped output triangles. The generated-runtime integration evidence is under
`v02/execution/root-generated-integration`: real `Core` entity hierarchy,
nonuniform rotated parent/child transforms, cooked model/material
build/release cycles, borrowed empty-history sentinels, wrong-type cleanup and
invalid model cases passed under ASan/UBSan. Its only leak report is the known
40-byte pre-main `StandardPlatformInputConfiguration` static allocation.

Task 2 proves generated Core initialization, one complete frame boundary,
ordered disposal, the supplied catalog ABI and the memory gate. Task 3 adds the
authored matrix/mesh bridge and owned model/material adapters. Task 4 replaces
the probe catalog with the manifest-owned startup catalog and boots the normal
game.

## Manifest-owned package and normal boot (Task 4)

The builder resolves every manifest scene to exactly one cooked artifact
through the scene's `cooked-relative-path` metadata, stages only the declared
`CookedArtifacts` from `WorkingRoot/package-source` into a fresh
`dreamcast-build-<guid>/content` tree (hash verified, one MiB budget, epoch
timestamps, no reparse points, no root overlap), and writes the external
startup catalog into `dreamcast-build-<guid>/catalog`. The native executor runs
`scripts/build.ps1 -Mode Authored` from `HELENGINE_DC_REPOSITORY_ROOT` with the
PowerShell host named by `HELENGINE_DC_POWERSHELL`; `prepare-authored-fixture.ps1`
sets both around the generic graph. The Makefile embeds the staged tree with the
pinned `genromfs`/`bin2c` utilities and KOS `romdiskbase.c`, which owns the `/rd`
automount; no `KOS_INIT_ROMDISK` macro is used.

`runDreamcastAuthoredScene` converts the C catalog ABI into the generated
`RuntimeSceneCatalog` (the generated catalog copies the entry array but not
the entries, so the host disposes entries after the catalog), initializes Core
with `PlatformInfo("dc", "1.0.0")` to match the generated startup manifest,
loads the startup scene through `SceneManager::LoadScene`, commits at the
explicit frame boundary and requires exactly one loaded scene, camera,
drawable, owned model and owned material before entering the frame loop.

Boot evidence for the normal game (no `RuntimeInitProbe`):

```text
Direct build   v02/execution/task4-authored-01   ELF SHA-256 6ae09f285c4a08489148210710b40b624b148bf7b67478b18fc7e53d05edbc93
Pipeline build v02/root-graph-task4-v2/build/authored-a11df066839b4e738fd37ea5105cb4eb (same SHA-256, GitCommit f225880, GitDirty=false)
Resident span  0x8c010000..0x8c1f5acc = 1,989,324 bytes (1.897 MiB) including BSS and the 2 KiB ROMFS
Symbols        _helengine_dreamcast_runtime_probe 0x8c1f0ed4, _helengine_dreamcast_authored_scene_ready 0x8c1f0f24,
               _helengine_dreamcast_authored_frame_count 0x8c1f0f28 (pinned sh-elf-nm)
```

The hidden bounded Lua probe `v02/execution/task4-boot-01/authored-boot-probe.lua`
(`authored-boot-probe.csv`) observed Core-ready and scene-ready at vblank 35,
the authored frame counter advancing 1, 60, 120, 180, checked free heap
14,545,264 bytes at Core-ready and 14,540,760 bytes while running, then
injected Start at frame 180 and observed the terminal snapshot at vblank 262:
status 5, error 0, stage 6, coreReady/frameComplete/coreDisposed/cleanShutdown
all 1, shutdown free heap 14,544,392 bytes. The scene unload check inside the
host (zero loaded scenes and zero owned model/material references) passed
because a failure there would have reported error status.

The full generic graph (`prepare-authored-fixture.ps1 -Build`, log
`v02/execution/task4-graph-v2-console.log`) completed generation, cooking,
staging, catalog writing and the native executor with exit code 0. The
pipeline ELF was launched visibly through `launch_in_emulator.ps1 -Visible`
as `DESKTOP-HELENA\Helena` with a nonzero main window handle titled
`Flycast - helengine_dc`. This gate covers boot, scene load, the running frame
loop and clean Start shutdown; the screen is clear-only until Task 5.

## Light gathering and the dc-standard material (Tasks 4-5)

The material's cooked schema is `dc-standard-v1` under graphics profile
`dc-standard`; `PlatformMaterialAsset::Lit` (set from the authored
`lighting-mode` field) selects Lambertian per-vertex shading instead of a flat
vertex color. `DrawableCollector::Visit` copies `material->lit()` into the
draw state alongside the material color.

`DreamcastRenderManager3D.cpp`'s `gatherLights(::ObjectManager&)` reads the
generated light lists once per frame through this ABI:

```cpp
List<::AmbientLightComponent*>* ObjectManager::get_AmbientLights();
List<::DirectionalLightComponent*>* ObjectManager::get_DirectionalLights();
::Entity* LightComponent::get_Parent();       // both light kinds
bool Entity::get_IsHierarchyEnabled();
::float4 Entity::get_Orientation();           // directional lights only
::float4 LightComponent::get_Color();
float LightComponent::get_Intensity();
```

Ambient contributions sum unconditionally (skipping disabled owners) with no
cap. Directional lights are walked in authored order; each light's forward
direction comes from the owning entity's world orientation quaternion through
the engine's `float4.RotateVector` formula (the same one the GameCube
renderer uses), normalized. Up to `kMaxDirectionalLights = 4` enabled
directional lights are accepted; gathering a fifth reports
`kMaxDirectionalLights + 1` and returns immediately, which
`AuthoredFrameAssembler::setLights` turns into
`AuthoredFrameStatus::TooManyDirectionalLights` (status string
`too-many-directional-lights`). A non-finite ambient color, or a non-finite or
near-zero-length (squared length <= 1.0e-8f) directional color/direction,
fails with `AuthoredFrameStatus::InvalidLight` (`invalid-light`) rather than
silently shading black. A lit material on a mesh without normals fails with
`AuthoredFrameStatus::LitMeshWithoutNormals` (`lit-mesh-without-normals`).

Per vertex, the authored normal is transformed to world space and combined as
`diffuse = ambient_rgb + sum(light.color * max(0, -dot(unitNormal,
light.direction)))` over the gathered directional lights (a light facing away
from the vertex contributes nothing), then `finalColor = clamp01(materialColor
* diffuse)` per channel with alpha fixed at 1 (`DreamcastAuthoredFrame.cpp`).
There is no specular term.

The authored snapshot (`helengine_dreamcast_authored_snapshot`, magic `HEAS`)
is version 2 as of this work: the 16th `uint32_t` field, previously reserved,
is now `directionalLightCount`, the number of directional lights `gatherLights`
reported for the frame; `kDreamcastAuthoredSnapshotVersion = 2u`. The struct's
overall size (64 bytes) and every other field's position are unchanged from
version 1.
