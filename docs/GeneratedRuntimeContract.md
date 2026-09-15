# Dreamcast generated runtime contract

This document records the generated ABI and native compatibility evidence for
Task 2. The generated graph is an external, read-only input. No generated
source is edited in the Dreamcast worktree.

## Generated graph identity

The graph reached the expected native-not-ready boundary in
`v02/root-graph-generator-v3`. Its authoritative root was:

`C:/dev/helworks/builds/helengine-dc/v02/root-graph-generator-v3/cache/v2/c829f5140cb3c2dd14e34a15a209d15a/b/dc/debug/release/generated-core`

The graph log is `v02/execution/root-graph-generator-v3-console.log`. The
isolated Helengine source used for graph fixes ended at `5a0696ca`; the
isolated generator source is at `e35366b` for the generic file descriptor
capability fix, after the earlier Math, Split and FileStream compatibility
fixes. The pinned Release payload hashes are recorded in
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
components and avoids counting ROMFS or the ELF twice. The bounded Gate A
probe output is `v02/execution/root-probe14/runtime-probe.csv` (verified by the
root task) and reports at vblank 41:

```text
magic=48455250 version=1 status=5 error=0 stage=6
startFree=14615408 coreReadyFree=14608976
coreDisposedFree=14608904 shutdownFree=14609688
coreReady=1 frameComplete=1 coreDisposed=1 cleanShutdown=1
```

The linked A14 ELF is
`v02/execution/task2-gate-a-14/helengine_dc.elf`, SHA-256
`957f7a2b2fdd0c2cf3b9d994ea3e315f1ec6a39fd7a976d752b397e4583b06f6`. Its
resolved probe symbol is `_helengine_dreamcast_runtime_probe` at `0x8c1e1874`.
The inspected ELF loadable resident span including BSS is 1,926,028 bytes
(1.836803 MiB), below the eight MiB gate. SDK image
`sha256:a33ea383336b428d8ac9bc507e00fd066092d4b0c8fcea29adccd61b2025f067`
and KOS commit `fcfa7d869471591ca1c777543261a7bfea7cb726` were unchanged.

The generic FileStream capability checks are recorded under
`v02/execution/root-generic-stream-check`; both capability paths passed the
100-iteration ASan/UBSan ownership and descriptor checks. Native geometry,
raster and content-path tests, the 10-case builder suite and authored-build
input tests passed in their external checkpoint directories.

Task 2 proves generated Core initialization, one complete frame boundary,
ordered disposal, the supplied catalog ABI and the memory gate. Task 3 owns
the authored matrix/mesh bridge; Task 4 owns replacing the probe catalog with
the manifest-owned startup catalog.
