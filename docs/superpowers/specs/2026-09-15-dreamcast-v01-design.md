# Dreamcast v0.1: native host and unlit cube — approved design and milestone plan

**Status:** Approved for v0.1 implementation. Native, geometry, and reproducible build implementation is present on `feature/dreamcast-v01`; emulator and hardware acceptance remain tracked in `docs/Validation.md`.

**Design history:** This document began as “Draft for discussion. No implementation started.” That was accurate while the scope and interfaces were under review. The review is now complete; the draft decisions below were approved for implementation through the companion plan. The approval-time note that no source implementation, toolchain installation, dependency checkout, or emulator execution had been performed describes that earlier review state, not the current worktree.

**Goal:** Build a standalone Helengine Dreamcast executable that initializes video and reliably renders a rotating, unlit cube.

**Confirmed scope:** Native host and cube first. Helengine editor, generated runtime, scene export, and platform-builder integration belong to a subsequent milestone.

## Reference intake

Read both project references in full:

- [Dreamcast Development Reference](../../Dreamcast_Development_Reference.md): toolchain and C++ setup (§3–5), CPU and memory (§6), native PVR rendering and ownership (§7–8), video and budgets (§10), deployment/debugging (§14–15), and staged validation (§16–19).
- [Dreamcast Rendering Optimization](../../dreamcast-rendering-optimization.md): geometry and SH4 processing (§5–6), projection/clipping (§7), list management (§8), submission and VRAM (§10–11), and measurement (§14).

The immediate lessons are to establish a reproducible ELF, give one component ownership of PVR submission, transform and clip geometry on the CPU, specify reciprocal depth correctly, and measure before optimizing. Asset conversion, animation, visibility systems, and advanced transfers are future guidance.

The documents identify KOS `fcfa7d869471591ca1c777543261a7bfea7cb726` as their inspected snapshot. Use it as a candidate pin; verify that it is available and that its toolchain, examples, and our executable build before accepting the environment. The research snippets are not proof of a working build. This planning session could not retrieve the pinned raw sources through the web tool; it did check the live [KOS scene submission API](https://kos-docs.dreamcast.wiki/group__pvr__scene__mgmt.html) and [polygon header API](https://kos-docs.dreamcast.wiki/group__pvr__primitives__headers.html). Live documentation does not establish compatibility with an unbuilt pin.

Sibling context: the GameCube host uses `src/main.cpp` and focused native classes under `src/platform/gamecube/`. The N64 v0.1 draft likewise starts with a standalone host. Follow that organization, without importing the mature GameCube runtime or treating its GPU behavior as a Dreamcast contract.

## Approach

**Recommended: C++ with KallistiOS and a native PVR renderer.** Use KOS startup, video, timers, and ordinary `pvr_prim()` submission. Keep transform/projection/clipping logic separate from PVR packet emission so its math can be tested on the host. Start with straightforward single-precision math; KOS accelerated matrix helpers can be adopted when correctness and profiling justify them.

Alternatives considered:

- **GLdc:** supplies a fixed-function GL interface and software geometry processing. It is useful for a compatible existing renderer; this standalone milestone has no GL implementation to preserve. It also introduces another state and lifecycle layer.
- **Native PVR plus SH4ZAM from day one:** attractive for a larger geometry workload. It adds a dependency and optimized math integration before a 12-triangle scene needs it. Keep the geometry boundary suitable for this later.

## Visible result

- Initial target: 640 × 480, RGB565, 4:3, KOS-managed buffering and presentation. Start with the standard KOS mode selection for the detected cable; record the actual mode and refresh. Do not force a VGA-only mode on a TV connection. Additional regional/display policies are later work.
- Dark clear color and a centered cube viewed with a perspective camera.
- Six opaque face colors, constant as the cube turns. No lighting, normals, texture sampling, fog, blending, or transparency.
- Original fixed geometry: 24 face vertices and 12 indexed triangles. Face-local vertices allow distinct colors at shared corners.
- Slow two-axis rotation driven by elapsed time, with a bounded delta after a debugger pause. Angular speed is independent of presentation rate.
- Depth testing, depth writes, and backface culling enabled explicitly after winding validation. A diagnostic mode can disable culling to isolate depth errors.
- No controller required. Optionally read Start from the first available controller to exit, handling missing/disconnected devices. This is a local host convenience, not the engine input API.
- Primary artifact: `helengine_dc.elf`, with symbols and a link map. Flycast direct ELF/HLE is the proposed first execution route; verify the installed version during setup. Disc packaging is outside v0.1.

Two packed 640 × 480 RGB565 framebuffers account for 1,228,800 bytes. That is only framebuffer arithmetic: measure actual KOS allocations, including parameter buffers and tile bins. Do not allocate a desktop-style full-screen depth surface; PVR provides its own depth/visibility mechanism.

## Native structure

Use namespace `helengine::dreamcast` and documented, focused types. These are proposed file boundaries, not shared engine interfaces.

| Proposed file | Responsibility |
|---|---|
| `src/main.cpp` | Declare KOS startup configuration and enter the native host. |
| `src/platform/dreamcast/DreamcastBootHost.hpp/.cpp` | Ordered startup, elapsed time, optional Start exit, diagnostics, and shutdown. |
| `src/platform/dreamcast/DreamcastVideo.hpp/.cpp` | Video mode selection and ordered PVR initialization/shutdown; record active settings. |
| `src/platform/dreamcast/DreamcastRasterRenderer.hpp/.cpp` | Own scene/list transitions, cached unlit context/header, aligned packets, and submission. |
| `src/platform/dreamcast/DreamcastGeometryPipeline.hpp/.cpp` | CPU transform, near clipping, perspective projection, winding, and bounded scratch data; no KOS calls. |
| `src/platform/dreamcast/DreamcastCubeMesh.hpp/.cpp` | Immutable original cube positions, face colors, and triangle indices. |
| `tests/native/DreamcastGeometryPipelineTests.cpp` | Small host executable covering projection, clipping, winding, and bounds. |
| `Makefile` | KOS wrapper integration, dependency tracking, ELF and map output. |
| `scripts/build.ps1`, `scripts/launch_in_emulator.ps1` | Windows entry points with explicit configuration, fresh artifact checks, and propagated failures. |
| `toolchain-lock.txt`, `docs/Build.md`, `docs/Validation.md` | Exact environment record, reproducible instructions, and observed acceptance evidence. |

Choose one supported host environment during setup. Prefer a Linux KOS build through the available Docker or WSL route, following sibling conventions where useful. Do not implement several host environments at once or install anything as part of drafting this plan. Keep SDK installations separate from project sources; keep generated output and logs under `C:\dev\helworks\builds\helengine-dc\<build-id>` and out of Git. Preserve required third-party notices when producing distributable artifacts.

## Geometry contract

For this standalone host, define camera space with +X right, +Y up, and positive depth `d` in front of the camera. This is local to the diagnostic renderer; later shared-runtime integration must explicitly map Helengine's conventions.

1. Transform the cube into camera space using float math. Document matrix storage and multiplication order next to the implementation.
2. Clip each triangle against `d >= near`, using a strictly positive near distance. Reject triangles entirely behind it. Interpolate position and color at intersections before projection, preserve winding, and triangulate a resulting quad.
3. Use signed edge distances for clipping: `t = a / (a - b)`. Handle on-plane vertices and degenerate output without dividing by zero or emitting zero-area triangles.
4. Project using `q = 1 / d`, `screenX = width/2 + fx*x*q`, and `screenY = height/2 - fy*y*q`, where `fy = height/(2*tan(verticalFov/2))` and `fx = fy` for this square-pixel viewport. Submit `q` as the PVR depth value, with an explicit compatible greater-depth comparison.
5. Account for the screen-Y inversion when choosing front-face winding and the PVR cull mode. Verify it using known poses, not by changing index order until one view happens to look correct.
6. Start with independent three-vertex strips, each terminated with `PVR_CMD_VERTEX_EOL`. This makes clipped output straightforward. The unclipped cube emits 36 vertex records; strip optimization is deferred.
7. Use fixed scratch storage. Near-plane clipping of one triangle produces at most four polygon vertices and two output triangles. Validate finite projected values and capacity before packet emission.

The first camera keeps the cube comfortably inside the view. Add diagnostic near-plane and screen-edge crossings. PVR viewport/tile handling is not our near clipper. A complete general-purpose frustum/scissor API is outside this small host.

## Frame ownership

Initialize video before PVR and check startup results. Configure only the opaque polygon list for the final v0.1 path; retain conservative SDK buffer settings and measure their actual cost. Compile the untextured, opaque polygon header once. Initialize every packet field, including unused UV/offset-color fields, and satisfy the selected API's alignment and size contracts.

Each frame:

1. Read optional exit input and update the cube pose from the elapsed-time clock.
2. Transform, clip, and project into bounded CPU scratch storage.
3. Call `pvr_wait_ready()`, begin the scene, and open `PVR_LIST_OP_POLY`.
4. Submit the cached header and valid vertex records through ordinary `pvr_prim()` calls.
5. Finish the list and scene. KOS owns rendering and page flips; do not add manual framebuffer swapping or an independent per-frame VBlank wait.

Use the non-DMA submission contract: CPU packets may be reused once the submission call has consumed them; GPU internal storage remains KOS-owned. Verify that contract in the selected source revision. No textures, external vertex-DMA buffers, or mutable GPU-referenced matrices are involved. Avoid adding a render-completion wait every frame merely for CPU-only model transforms. Drain pending graphics work before shutdown or video reconfiguration using the SDK's readiness/completion sequence.

On failure, log the specific stage and stop safely. Track initialized subsystems so partial startup does not tear down resources that were never acquired. Normal logging is limited to startup, first submission, periodic aggregate diagnostics, and shutdown; a submitted frame is not itself proof that an image was displayed.

## Milestone plan

### 1. Reproducible toolchain and video bootstrap

- [x] Inspect the available host build environment and Flycast installation; select one supported route.
- [x] Resolve the candidate KOS revision, record compiler and ABI settings, and build the upstream hello and simple PVR examples. Use one consistent SH-4 floating-point ABI across KOS and the executable.
- [x] Add the native entry point, host/video ownership, build files, startup logs, and ignored external build output.
- [x] Produce a fresh SuperH little-endian ELF and map. Confirm its header with the cross-toolchain and retain build/source identifiers.
- [ ] Boot a stable clear-color screen, then a screen-space triangle, through the normal PVR scene lifecycle. These isolate display/submission from 3D math.

**Checkpoint:** Clean build and repeated successful boots; missing tools or compilation failure never launch an older artifact.

### 2. Static unlit cube

- [x] Add immutable cube data and the testable geometry pipeline.
- [x] Test centered projection, positive reciprocal depth, nearer-versus-farther ordering, fully rejected geometry, all near-plane crossing cases, on-plane/degenerate cases, and scratch-capacity limits.
- [x] Render the fixed-pose cube with explicit colors, depth state, culling, and strip termination.
- [ ] Inspect several fixed poses. Reverse triangle submission order and repeat with culling disabled to exercise occlusion independently of accidental draw order.

**Checkpoint:** Correct visible faces, constant unlit colors, and stable occlusion; CPU geometry tests pass.

### 3. Rotation, clipping, and frame lifetime

- [x] Add elapsed-time two-axis rotation and a bounded pause delta.
- [x] Exercise near-plane crossings and screen edges with deterministic diagnostic poses; reject nonfinite output and preserve winding after clipping.
- [ ] Verify repeated presentation for ten minutes without hangs, flicker, exploding triangles, or growing application allocations.
- [ ] Verify launch without a controller and optional Start exit; stop submission before draining/shutting down graphics.
- [ ] Record frame intervals, geometry/submission CPU time, readiness wait, emitted/clipped vertices, available VRAM, and KOS parameter-buffer high-water use at a low logging frequency. Keep KOS timing units explicit.

**Checkpoint:** Stable animation with no application allocations in the steady-state frame loop and no corruption at clipping boundaries. Target smooth refresh-paced output; emulator speed is not a Dreamcast performance measurement.

### 4. Rebuild and acceptance record

- [x] Repeat a clean build and document exact launch commands, dependency revisions, compiler flags, ELF hash, and active display/emulator settings.
- [ ] Record functional results from the emulator separately from physical-console results.
- [ ] When a console and loader are available, test the same ELF, intended cable/display mode, repeated startup/shutdown, and sustained rotation. Report hardware acceptance as pending until exercised.
- [ ] Record CPU/VRAM use and observed timing; do not turn the research documents' illustrative budgets into measured claims.

**Checkpoint:** A reproducible native executable, build/run instructions, and explicit evidence for tested configurations.

## Deferred work

Editor/export integration, generated runtime, shared scene graph, asset readers and cooking, textures, lighting, audio, VMU saves, engine input APIs, physics, UI, visibility/LOD, custom assembly, SH4ZAM integration, direct store-queue tuning, vertex DMA, render-to-texture, and disc-image production.

The next milestone should route one authored Helengine camera, cube mesh, transform, and unlit material into this proven host. It needs its own runtime compatibility and memory assessment.

## Review state

Standalone scope is confirmed. Native PVR, initial resolution, six face colors, rotation, clipping contract, module boundaries, and the provisional Linux/Docker host route are approved decisions. The earlier statement that this was “a milestone draft for review, not an approved code-level implementation plan” was accurate at draft time and is retained in the design history above; this document is now approved for the companion code-level implementation plan. The approval-time statement that no source changes, toolchain installation, dependency checkout, or emulator execution were part of design approval is historical; current implementation and acceptance status are tracked by the plan and `docs/Validation.md`.
