# Dreamcast v0.2 authored scene build

This document describes how a normal Helengine project becomes a Dreamcast ELF
that boots generated Core, loads its startup scene from an embedded ROMFS and
renders that scene through the PVR. It complements [Build.md](Build.md), which
covers the standalone Diagnostic cube, and [Validation.md](Validation.md), which
records the evidence rows.

## What v0.2/v0.3 supports

- Platform `dc`, builder `helengine.dreamcast.builder` 0.2.0, engine 0.4.0,
  graphics profile `dc-standard` (renamed from `dc-unlit`), material schema
  `dc-standard-v1` (renamed from `dc-unlit-v1`).
- One startup scene with exactly one enabled camera component and exactly one
  enabled static mesh component. Transform-only parent entities are allowed.
- One model with 1..64 positions, 1..128 triangles, exactly one nonempty index
  buffer and one submesh covering the whole triangle list. A model may declare
  per-vertex normals (`MeshView::hasNormals`); a lit material requires them.
- One opaque, untextured, single-sided material whose authored RGB is used as
  the flat or lit base color. Vertex-color, textured, double-sided or
  translucent materials are rejected at cook time and again at build time.
  Both `unlit` (flat vertex color) and `lit` lighting modes are supported
  (`lighting-mode` field, cooked to the material's `Lit` flag).
- Lighting (v0.3): a lit material shades with per-vertex Lambertian diffuse
  only, no specular. For each vertex, the authored normal is transformed to
  world space and combined with every enabled ambient and directional light
  gathered from the scene: `diffuse = ambient_rgb + sum(light.color *
  max(0, -dot(unitNormal, light.direction)))` per enabled directional light
  (lights facing away contribute nothing), then `finalColor =
  clamp01(materialColor * diffuse)` per channel with alpha fixed at 1. Up to
  four enabled directional lights are supported (`kMaxDirectionalLights = 4`);
  a fifth fails the frame with status `too-many-directional-lights`
  (`AuthoredFrameStatus::TooManyDirectionalLights`). Ambient lights are not
  capped and sum unconditionally. A lit material on a mesh without normals
  fails with status `lit-mesh-without-normals`
  (`AuthoredFrameStatus::LitMeshWithoutNormals`). A non-finite ambient color,
  or a non-finite or near-zero-length directional light direction/color,
  fails with status `invalid-light` (`AuthoredFrameStatus::InvalidLight`)
  rather than silently producing black.
- Camera: authored rigid world transform, near/far planes, vertical field of
  view and clear RGB, full viewport, back buffer only, color and depth clear
  enabled, no stencil. The aspect ratio is 640/480. The field of view is read
  from `ICamera.FieldOfView`, so a scene inside the engine's legal range frames
  the same way here as on every other platform; a camera that never authored one
  carries the engine default of 45 degrees. Outside that range the platforms
  diverge deliberately: `CameraProjectionUtils.ClampFieldOfView` clamps, while
  Dreamcast fails the frame with status `invalid-camera`
  (`AuthoredFrameStatus::InvalidCamera`) before any packet is emitted, as it does
  for the near and far planes. A non-finite value fails the same way, which the
  engine's clamp does not catch, since `Math.Min`/`Math.Max` propagate NaN.
- Model transforms must be finite affine with a positive determinant. Nonuniform
  scale is fine; mirrored or singular transforms are rejected per frame.
- Budgets: one MiB of embedded cooked content, eight MiB resident ELF span
  including BSS and ROMFS, at least four MiB free heap after startup.
- Start on controller port 0 exits the game cleanly. No other input, no audio,
  physics, scripts, UI, animation, textures or scene switching.

## Pipeline

1. The editor build graph discovers `dc` from an isolated `platforms.json`,
   regenerates Core C++ with the Dreamcast codegen profile (C++20, 4-byte
   pointers, little endian, `native-file-stream-descriptor-apis=false`), cooks
   the scene, model and platform material, and stages the package source.
2. `DreamcastPlatformAssetBuilder.BuildAsync` validates the manifest, resolves
   each scene to its `cooked-relative-path` artifact, stages only declared
   artifacts into a fresh `dreamcast-build-<guid>/content` tree, validates the
   staged scene/model/material with the normal deserializers, and writes the
   external startup catalog (`dc_runtime_scene_catalog.hpp/.cpp`).
3. `DreamcastNativeBuildExecutor` runs `scripts/build.ps1 -Mode Authored` from
   `HELENGINE_DC_REPOSITORY_ROOT` with the PowerShell 7 host named by
   `HELENGINE_DC_POWERSHELL`. The Makefile compiles the generated unity file
   plus the finalized late units exactly once, embeds the staged content as a
   ROMFS with the pinned `genromfs`/`bin2c` and KOS `romdiskbase.c`, links the
   ELF and records source/external input hashes before and after compilation.
4. The build output is `<Output>/authored-<guid>/` with `helengine_dc.elf`,
   `helengine_dc.map`, `build-manifest.txt`, `romdisk.img` and the docker log.

## Commands

All paths below assume the isolated layout used for v0.2 acceptance:
`$dcRoot` is the Dreamcast checkout, set `$v02` to
`Join-Path $dcRoot '.dreamcast\v02'`, and use a PowerShell 7 host
(`pwsh` is not on PATH on the acceptance machine; the executor is given the
host explicitly).

Write a fixture and run the generic graph:

```powershell
& "$v02\dotnet-fixture\bin\helengine.dreamcast.fixture\debug\helengine.dreamcast.fixture.exe" --output "$v02\fixtures\baseline" --variant baseline
pwsh -NoProfile -File "$dcRoot\scripts\prepare-authored-fixture.ps1" `
    -FixtureRoot "$v02\fixtures\baseline" -OutputRoot "$v02\graph-baseline" -CacheRoot "$v02\cache" `
    -EngineRoot "$v02\helengine-source" `
    -BuilderAssemblyPath "$v02\root-dotnet-check\bin\helengine.dreamcast.builder\debug\helengine.dreamcast.builder.dll" `
    -CodegenToolPath "$v02\csharpcodegen-source\codegen\bin\Release\net9.0\codegen.exe" -Build
```

`prepare-authored-fixture.ps1` writes the scoped `platforms.json`, sets and
restores `HELENGINE_ENGINE_USER_SETTINGS_ROOT`, `HELENGINE_DC_REPOSITORY_ROOT`
and `HELENGINE_DC_POWERSHELL`, and invokes `scripts/build-platform.ps1` from the
engine checkout. Passing the same `-CacheRoot` and the same fixture path for
several variants reuses the generated Core; each run still gets its own build
output, staging directory and native invocation.

Verify a build in a hidden bounded emulator session:

```powershell
pwsh -NoProfile -File "$dcRoot\scripts\verify-authored-scene.ps1" `
    -BuildRoot "$v02\graph-baseline\build\authored-<guid>" `
    -FlycastPath C:\dev\helworks\emus\dreamcast\flycast.exe `
    -EvidenceRoot "$v02\verify-baseline"
```

The verifier checks the manifest and ELF hash first, resolves the diagnostic
symbols with the pinned `sh-elf-nm`, runs Flycast hidden with a Lua probe,
captures raw RGB565 frames through the 32-bit VRAM aperture at the requested
loop frames, injects Start, and requires the runtime probe (status 5, stage 6)
and authored snapshot (stage 9, error 0) terminal states. `-NoController`
configures all four maple ports as absent and requires the frame counter to
reach `-StartAfterFrames` instead. `-StartAfterFrames 3600` produces a sixty
second run.

Launch visibly for a person:

```powershell
pwsh -NoProfile -File "$dcRoot\scripts\launch_in_emulator.ps1" `
    -BuildId authored-<guid> -OutputRoot "$v02\graph-baseline\build" `
    -FlycastPath C:\dev\helworks\emus\dreamcast\flycast.exe -Visible
```

`-OutputRoot` is the directory that contains the `authored-<guid>` directory.
The launcher re-hashes the ELF and Flycast before starting.

## Runtime behaviour

`main` calls `runDreamcastAuthoredScene`, which initializes video and the PVR
renderer (clockwise authored front faces, because the generated camera-to-native
Z reflection reverses screen winding), builds the generated scene catalog from
the external ABI, initializes Core with the generated startup metadata, loads
the startup scene, requires one loaded scene/camera/drawable/model/material, and
then runs `Core::Update(delta)`, `Core::Draw()`, `Core::CompleteFrameBoundary()`
per frame with a monotonic delta bounded to 100 ms. `Core::Draw` reaches
`DreamcastRenderManager3D::Draw`, which gathers the camera and its ordered
render queue, validates everything through the generated-header-free
`AuthoredFrameAssembler`, runs the bounded geometry pipeline and presents one
PVR scene with the authored clear color. On Start the host drains the PVR,
unloads the scene, verifies zero owned references, disposes Core, stops PVR and
video, and publishes the terminal snapshots.

Three diagnostic symbols are exported and resolved per ELF: the v0.1
`helengine_dreamcast_diagnostics` renderer snapshot, the runtime probe
`helengine_dreamcast_runtime_probe`, and the authored snapshot
`helengine_dreamcast_authored_snapshot` (magic `HEAS`, version 2 as of v0.3:
stage, error, scene digest, camera/drawable counts, triangle counts, presented
frames, first presented frame, clear ARGB, checked free heap, loop frames,
gathered directional light count).

## Failure behaviour

Every unsupported input fails with an identified error and no success
manifest: missing authored roots, absent `Core.hpp`, absent catalog ABI,
compiler failure, manifest scenes without a unique cooked path, more or fewer
than one camera or mesh, over-limit models, textured/translucent materials, a
lit material on a mesh without normals (`lit-mesh-without-normals`), more than
four enabled directional lights (`too-many-directional-lights`), a non-finite
or degenerate light (`invalid-light`), or a missing referenced cooked file. A nonexistent boot-scene id in the project
routing or build configuration is not rejected by the generic engine graph, which
substitutes the available project scene; only the builder-level startup/scene
association check guards that case. The Diagnostic build mode and its selectors
are unchanged and remain available.

## Building the demodisc cube_test scene

`C:\dev\helprojs\demodisc` (project "city") is the cross-platform reference
project; its `scenes/rendering/cube_test.helen` is generated by the project's
own `CubeTestSceneFactory`, which excludes the instruction UI and orbit camera
on minimal-runtime targets (`n64` and `dc`) and keeps the camera, the engine
cube and the project's `CubeTestSpinComponent`, which spins through generated
Core. As of the lighting plan, the scene's directional light (`CubeTestSun`)
is no longer excluded on `dc` — nor, per the last observed state of the
shared project, on `n64` either, since that platform separately gained
lighting support — so the sun is included and lights the cube on Dreamcast;
this scene and its `build_config.json` `dc` entry live in a
shared, actively-edited working tree, so its exact exclusion set can change
between sessions — re-check before relying on it. The Dreamcast recipe
mirrors the N64 one:

1. Project side (done once, 2026-09-15): `dc` added to `project.heproj`
   `supportedPlatforms` and `settings/platforms.json`, `settings/platform.dc.json`
   copied from the N64 file, the factory's exclusion set extended with `dc`,
   the project code rebuilt (`dotnet build cache/generated_code/projects/DemoDiscEditorTools/DemoDiscEditorTools.csproj`),
   the scene regenerated with `helengine.editor.app.dll --project ... --editor-command menu.generate-cube-test-scene`,
   and a `dc` entry added to `user_settings/build_config.json` (cube_test only,
   `release`/`dc-standard`/`default`/`romfs-package`/`dreamcast-elf`, no prebuild).
   The editor only compiles project code that was built; regenerate after every
   factory change or the exclusions are silently stale. Running
   `menu.generate-cube-test-scene` also rewrites `build_config.json` and drops
   the `dc` entry entirely (it round-trips through a service that does not
   know about `dc`); recreate it by copying the `n64` entry and overriding the
   fields above plus the current scene content hash if this happens.
2. Toolchain: the shared engine at `C:\dev\helworks\helengine` reports
   `1.0.0+ec2d7ff8...`, which the project requires, so the builder is compiled
   against it (`--artifacts-path v02/dotnet-main -p:HelengineRoot=C:/dev/helworks/helengine`).
   Main `csharpcodegen` lacks the KOS file-descriptor capability, so the
   isolated generator `v02/csharpcodegen-source` stays registered for `dc`.
3. Build and verify:

```powershell
pwsh -NoProfile -File "$dcRoot\scripts\prepare-authored-fixture.ps1" `
    -FixtureRoot C:\dev\helprojs\demodisc -OutputRoot "$v02\cube-test\graph-N" -CacheRoot "$v02\cube-test\cache" `
    -EngineRoot C:\dev\helworks\helengine -EngineVersion 1.0.0+ec2d7ff8a5b2624bbc51784bd9eb0c9babed1843 `
    -BuilderAssemblyPath "$v02\dotnet-main\bin\helengine.dreamcast.builder\debug\helengine.dreamcast.builder.dll" `
    -CodegenToolPath "$v02\csharpcodegen-source\codegen\bin\Release\net9.0\codegen.exe" -Build
pwsh -NoProfile -File "$dcRoot\scripts\verify-authored-scene.ps1" -BuildRoot "$v02\cube-test\graph-N\build\authored-<guid>" `
    -FlycastPath C:\dev\helworks\emus\dreamcast\flycast.exe -EvidenceRoot "$v02\cube-test\verify-N"
```

The demodisc package declares 30 instruction textures and two fonts; the
stager leaves them out because the runtime cannot load them, so the ROMFS holds
only the scene, the engine cube model and the two dc-unlit materials. First
accepted build: `v02/cube-test/graph-4/build/authored-ab183787812a49cc9573d20f1d027dd4`,
ELF SHA-256 `57c97951e90b5994f4f13e823f2ec05d8f985c797588214b65d2ca6c45be18b1`;
verifier PASS with frames 120 and 180 differing (spinning white cube on the
authored clear), clean Start shutdown, checked free heap 14,345,208 bytes at
Core-ready.
