# Dreamcast v0.2 — first authored Helengine scene

**Status:** Draft for user review. This request authorizes planning; implementation has not started.
**Goal:** Build a normal Helengine project for platform dc, boot the generated Helengine runtime, load its cooked startup scene, and render the authored camera, mesh and material through the existing PVR renderer.
**Executor:** gpt-5.6-luna at xhigh. One sequential implementation session; no automatic additional agents, model changes or independent reviews.

## Baseline and source findings

- Dreamcast main is f0c66b2 in C:/dev/helworks/helengine-dc. Native video, reciprocal-depth projection, opaque PVR triangles and diagnostic cube are implemented and emulator-tested.
- Helengine was inspected at 08e336ed. Recheck its revision and local changes before execution. The generic platform/build graph is the integration point.
- C:/dev/helworks/helengine-n64-impl at 7a70159 contains a developing authored-scene port. It is a source-navigation aid, not evidence that its implementation or stale design status is correct. Do not copy its hardcoded camera, silent defaults, ownership assumptions or artifact-identity fallbacks.
- DevelopmentPlatformProvider and the installation/descriptor stores discover builder assemblies. PlatformBuildRequest supplies Manifest and GeneratedCoreCppRootPath. The generic graph resolves dependencies, cooks content and generates C++.
- RenderManager3D owns runtime model/material construction and camera queue visitation. Entity.WorldTransformMatrix preserves the complete parent transform chain. Use the generated equivalent, rather than reconstructing from decomposed world position/scale/orientation when that would lose shear.
- ICamera exposes near/far distances, viewport, clear settings, parent and render queues. It has no field-of-view property. v0.2 uses a documented 45-degree vertical FOV, matching the inspected PSP backend; adding authored FOV to the shared engine is outside this milestone.
- ModelAsset contains Positions, Indices16, Indices32 and Submeshes. PlatformMaterialAsset is the existing cooked-material contract.
- The current Dreamcast pipeline has CubeMeshView, 24 output triangles and near-only clipping. These must become explicit general-mesh capacity and near/far behavior before authored meshes are accepted.

## Alternatives and decision

1. **Recommended: generated Core + existing cooked assets + embedded ROMFS.** Exercises the real engine path while retaining direct-ELF launch. Requires runtime and backend adapters.
2. A custom cube/scene converter would be smaller, but would bypass scene loading and generated runtime compatibility; it does not satisfy this milestone.
3. Full disc packaging and a broad renderer would extend coverage but add unrelated delivery and graphics work.

Use option 1. KOS supports embedding a ROMFS and mounting it at /rd; the image remains resident in system RAM, so include its size in memory accounting. See the [KOS ROMFS documentation](https://kos-docs.dreamcast.wiki/fs__romdisk_8h_source.html) and the pinned local include/kos/fs_romdisk.h. An embedded asset package is not a disc image.

## Scope and exact constraints

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
- Emulator: C:/dev/helworks/emus/dreamcast/flycast.exe, verified against the existing hash lock. Interactive launches must run under Helena's desktop account, outside the sandbox, with a visible window. A running sandbox process is not evidence of a visible window.

## Data flow and ownership

Project -> normal editor build graph -> generated Core plus cooked scene/model/material -> dc builder validation and staging -> startup catalog + ROMFS -> fresh ELF.

Native host initializes video/PVR and ROMFS before Core; installs platform content source, runtime catalog and minimal required backends; loads the selected scene through SceneManager; commits pending operations at the normal boundary; validates scene cardinality; then uses Core.Update(delta) and Core.Draw().

The Dreamcast RenderManager3D visits the camera's normal ordered render queue. It converts cooked assets once into owned bounded native model/material resources and submits the authored world/view transform to the geometry pipeline. CPU geometry finishes before PVR scene submission. Resources outlive submissions; shutdown waits for rendering before releasing Core-owned assets, adapters and ROMFS. Document actual generated ownership rules before writing delete/Dispose calls.

Content source accepts only normalized content-relative paths and resolves them under /rd. Missing files, truncated content, wrong types, unsupported required features and count overflow produce explicit diagnostics with scene/asset identity. No directory scanning as a substitute for manifest dependencies, basename matching, or selecting an arbitrary sole scene artifact.

## Checkpoints and acceptance

A. Generate and compile the minimum actual Core with required adapters using the current SDK. Boot to Core initialized and record memory, before implementing the complete scene/render pipeline.
B. Build through the normal Helengine graph, mount packaged files, load the startup scene, and observe exactly one camera/drawable with expected model/material IDs.
C. Render those submissions. An authored baseline and independent transform, color, camera and mesh-data variants must visibly differ as predicted without editing native constants. A static authored cube must not inherit diagnostic rotation.
D. Negative tests reject missing/ambiguous scene artifacts, unsupported content and malformed geometry. Failure cannot publish a valid success manifest or launch a stale build.
E. Existing diagnostic mode still passes; authored mode runs for at least 60 wall seconds, keeps native buffers stable and exits via Start. Report native and generated-runtime memory separately. Physical hardware remains pending.

Baseline fixture is a normal serialized Helengine project with a cube at the origin, nonidentity positive-scale transform, opaque red material, and an authored camera positioned to see it. A fixture utility may create it through current project/scene serializers, but the acceptance build must run the real generic graph. A hand-built binary payload or test-only native cube is insufficient.

If generated Core needs broad shared-runtime/compiler surgery or fails the memory gates, report the exact compiler/link/runtime evidence and smallest proposed change before expanding scope. Do not silently replace Core, raise limits or change models. Routine integration fixes and focused regression tests remain within the plan.
