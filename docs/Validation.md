# Dreamcast v0.1 validation evidence

This matrix records observed evidence for the Dreamcast target. Flycast results use internal Lua callbacks and raw emulated framebuffer memory through the 32-bit PVR aperture. Start shutdown and missing-controller startup passed bounded probes on the final default ELF. Physical hardware remains untested.

## Build identities

| Ref | Build ID / selector | Source commit | KOS commit | SDK image ID | ELF SHA-256 |
|---|---|---|---|---|---|
| A | `dreamcast-v01-acceptance-20260915-002` / default cube | `d7fe241` | `fcfa7d869471591ca1c777543261a7bfea7cb726` | `sha256:a33ea383336b428d8ac9bc507e00fd066092d4b0c8fcea29adccd61b2025f067` | `7b02423a8027768623ff960d01f3f384ce344e03d4e82f1d3b9ffabe3efccad6` |
| B | `dreamcast-v01-acceptance-20260915-001` / ten-minute soak | `8673e6a` | `fcfa7d869471591ca1c777543261a7bfea7cb726` | `sha256:a33ea383336b428d8ac9bc507e00fd066092d4b0c8fcea29adccd61b2025f067` | `45d36cfcb879405ab130a40920d1cd38d7721887caae50fa4811c9a4cc3503a5` |
| C | `dreamcast-v01-acceptance-near-20260915-001` / near-plane probe | `d7fe241` | `fcfa7d869471591ca1c777543261a7bfea7cb726` | `sha256:a33ea383336b428d8ac9bc507e00fd066092d4b0c8fcea29adccd61b2025f067` | `85c37a62dd1b11d73eb2f68f9845f2f7e50299516b7a47677144d1cd7bd52dee` |
| D | `dreamcast-v01-acceptance-static-reverse-20260915-002` / static reverse order | `8673e6a` | `fcfa7d869471591ca1c777543261a7bfea7cb726` | `sha256:a33ea383336b428d8ac9bc507e00fd066092d4b0c8fcea29adccd61b2025f067` | `fc9042a7317a00b68cec8dd72b880ee39ea81c80377776a541ab54fbdecc1659` |
| E | `dreamcast-v01-acceptance-timed-20260915-001` / `StopAfterSeconds=1` | `d7fe241` | `fcfa7d869471591ca1c777543261a7bfea7cb726` | `sha256:a33ea383336b428d8ac9bc507e00fd066092d4b0c8fcea29adccd61b2025f067` | `de64419e0682401e790ea532bbc6bfbf2055d37a55c946d2f8fbe1138fcb021d` |
| F | `dreamcast-v01-validation-final-0dd-triangle-20260915-001` / screen triangle | `0ddfb43` | `fcfa7d869471591ca1c777543261a7bfea7cb726` | `sha256:469db4c6155dd4ce1c6b6597b4a135c7c22a6e49c6f7dbb7a374e1d5ab4ef25a` | `b6407cb724f0fce29865e912c761e24af953311873aed039a2c21162d519e4c3` |
| G | `dreamcast-v01-validation-final-0dd-clear-20260915-001` / clear | `0ddfb43` | `fcfa7d869471591ca1c777543261a7bfea7cb726` | `sha256:dbba6bed5fb6435ff703ba72875fd171fbd450b954a2d280602772433664d561` | `9ace1e76a09a4afb99b5af45d5a31bc69f76c284f1c98bb63a785ce90c2c3e7a` |
| H | `dreamcast-v01-validation-final-0dd-static-nocull-20260915-001` / culling disabled | `0ddfb43` | `fcfa7d869471591ca1c777543261a7bfea7cb726` | `sha256:dbba6bed5fb6435ff703ba72875fd171fbd450b954a2d280602772433664d561` | `d7296994efaf1fd1e5125a1de80112c28f30bde0dad2a9ad7565726ee78f5f1d` |
| I | `dreamcast-v01-validation-final-0dd-edge-20260915-001` / screen edge | `0ddfb43` | `fcfa7d869471591ca1c777543261a7bfea7cb726` | `sha256:dbba6bed5fb6435ff703ba72875fd171fbd450b954a2d280602772433664d561` | `d463534e9ff44bfd4af6d3c031c23f10234b6e8d42de0c35f0dd0fda98217225` |
| J | dreamcast-v01-validation-final-0dd-static-20260915-001 / static cube | 0ddfb43 (dirty; source hashes retained) | fcfa7d869471591ca1c777543261a7bfea7cb726 | sha256:dbba6bed5fb6435ff703ba72875fd171fbd450b954a2d280602772433664d561 | 79a55d00f0dd2394dc0ecda5c0366fa969151f42b2f68f04aaf1b349b377ed9e |

`A` is the current clean default acceptance artifact. `B` is byte-identical to the actual 603-second soak artifact `dreamcast-v01-validation-final-0dd-default-20260915-001` (source `0ddfb43`, historical SDK `469db4c6...`); it is distinct from current `A`. The only native change in A adjusts the near diagnostic pose used by C. A passed a fresh 300-VBlank smoke and both input probes. Historical dirty builds retain exact per-file source hashes in their manifests.

## Evidence matrix

| Evidence row | Command / route | Identity | Observed result | Artifact path |
|---|---|---|---|---|
| Host geometry tests | Pinned SDK `make -C /src BUILD_ROOT=/out host-test`, plus direct ASan/UBSan host tests | Host verification at source `0ddfb43`, KOS `fcfa7d869471591ca1c777543261a7bfea7cb726`, SDK `sha256:469db4c6155dd4ce1c6b6597b4a135c7c22a6e49c6f7dbb7a374e1d5ab4ef25a` | Geometry and raster seam tests passed with exit code 0; sanitizer builds reported no findings. | [verification.txt](C:/dev/helworks/builds/helengine-dc/root-verification-20260915/verification.txt) |
| Final host-test rerun | Final Makefile make BUILD_ROOT=/out host-test | B source and SDK | Geometry and raster seam suites both passed. Later native change only adjusts the near diagnostic pose; test sources and geometry pipeline are unchanged. | [host-tests.log](C:/dev/helworks/builds/helengine-dc/dreamcast-v01-acceptance-20260915-001/host-tests.log) |
| KOS hello parity | Pinned KOS hello example | PENDING | No standalone hello runtime artifact with a complete build manifest is retained. | `C:\dev\helworks\builds\helengine-dc\bootstrap\hello.elf` |
| KOS PVR example parity | Pinned KOS PVR sample route | PENDING | The pvrmark Flycast run has framebuffer evidence, but its standalone sample build identity was not captured in a manifest. | [emulator-validation-report.md](C:/dev/helworks/builds/helengine-dc/bootstrap/emulator-validation-report.md); [pvrmark-validation.csv](C:/dev/helworks/builds/helengine-dc/bootstrap/pvrmark-validation.csv) |
| ELF header, map, and hash | `scripts/build.ps1`, pinned `sh-elf-readelf -h` | A | ELF32 SuperH little-endian header, map, manifest, and SHA-256 are present; clean build exit code 0. | `C:\dev\helworks\builds\helengine-dc\dreamcast-v01-acceptance-20260915-002\helengine_dc.elf`; `helengine_dc.map`; `build-manifest.txt`; `elf-header.txt` |
| Dark clear screen | Flycast direct ELF/HLE, `rend.EmulateFramebuffer=yes`, clear diagnostic | G | Status 2/error 0, zero submitted geometry, stable clear framebuffer hash `0000c6f6`. | [engine-validation-final-clear-0dd.csv](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-validation-final-clear-0dd.csv); [engine-final-clear-0dd-frame-0060-32.png](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-final-clear-0dd-frame-0060-32.png) |
| Screen-space triangle | Flycast direct ELF/HLE, triangle diagnostic | F | Status 2/error 0, `diag_tris=1`, `diag_verts=3`, framebuffer hash `00035588`. | [engine-validation-final-triangle.csv](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-validation-final-triangle.csv); [engine-final-triangle-frame-0060-32.png](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-final-triangle-frame-0060-32.png) |
| Centered fixed cube | Flycast static-pose diagnostic | J | Exterior green front, cyan top, blue right. Static, no-cull and reversed-order raw frames share SHA-256 2c13dc96efabe106aae6beac98426f32eea4d8637e18360a8f7efccbde2ae804. | [static frame](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-final-static-frame-0060-32.png); [static CSV](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-validation-final-static.csv) |
| Culling-disabled diagnostic | Flycast static pose with culling disabled | H | Status 2/error 0, stable exterior cube hash `0000edb1`, 432,607 nonzero bytes. | [engine-validation-final-static-nocull.csv](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-validation-final-static-nocull.csv); [engine-final-static-nocull-frame-0060-32.png](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-final-static-nocull-frame-0060-32.png) |
| Reverse submission order | Flycast static pose with reversed triangle order | D | Same fixed-pose hash `0000edb1` and nonzero byte count as the static comparison, with status 2/error 0. | [engine-validation-final-static-reverse.csv](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-validation-final-static-reverse.csv); [engine-final-static-reverse-frame-0060-32.png](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-final-static-reverse-frame-0060-32.png) |
| Near-plane crossings | Flycast corrected near-plane probe | C | Frame 60: `diag_tris=16`, `diag_verts=48`, `diag_clipped=4`, 456,181 nonzero bytes, hash `0000eba9`; later frames remain nonzero and change. | [engine-validation-acceptance-near.csv](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-validation-acceptance-near.csv); [engine-final-acceptance-near-frame-0060-32.png](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-final-acceptance-near-frame-0060-32.png) |
| Screen-edge crossings | Flycast screen-edge probe | I | Status 2/error 0 with nonzero changing framebuffer; frame 240 hash `00066624`, 426,343 nonzero bytes. | [engine-validation-final-edge.csv](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-validation-final-edge.csv); [engine-final-edge-frame-0060-32.png](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-final-edge-frame-0060-32.png) |
| Ten-minute animation | Flycast hidden process, Lua vblank/frame diagnostics | B | `vblank=36100`, `captures=126`, `elapsed_wall_s=603`; final sample status 2/error 0, 12 triangles/36 vertices, VRAM 6,937,344 bytes. The earlier 18,000-VBlank run was only approximately five minutes and is not counted here. | [engine-validation-final-long.csv](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-validation-final-long.csv) |
| Missing-controller startup | Flycast all four ports set with input:deviceN=10 | A | Effective bus 1 type 10; frame advanced continuously to 448 at VBlank 480, no shutdown/reset, one REIOS boot. | [no-controller.csv](C:/dev/helworks/builds/helengine-dc/input-validation/no-controller.csv); [input report](C:/dev/helworks/builds/helengine-dc/input-validation/input-validation-report.md) |
| Start exit | Flycast input:device1=0; Lua active-low Start injection after healthy frame 60 | A | Native status 2/frame 60 became status 4/frame 62, then status 0/frame 0; exactly two REIOS boots. Process exit came later from the explicit probe bound. | [controller.csv](C:/dev/helworks/builds/helengine-dc/input-validation/controller.csv); [input report](C:/dev/helworks/builds/helengine-dc/input-validation/input-validation-report.md) |
| Timed stop diagnostic | Flycast hidden process with StopAfterSeconds=1 | E | Active frame diagnostics followed by native frame resets and repeated REIOS boots. The 6.09-second process duration ended at the explicit 300-VBlank Lua bound; it is not application shutdown latency. | [engine-validation-acceptance-timed.csv](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-validation-acceptance-timed.csv) |
| Emulator functional result | Flycast vblank/Lua callbacks plus raw internal framebuffer | A | `callbacks_ready`, start, active 640x480 PVR, status 2/error 0, advancing frame diagnostics, and nonzero RGB565 raw framebuffer. | [engine-validation-acceptance002.csv](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-validation-acceptance002.csv); [engine-final-frame-0060-32.png](C:/dev/helworks/builds/helengine-dc/bootstrap/engine-final-frame-0060-32.png) |
| Physical-console result | Authorized loader/display path | PENDING | No hardware or physical-console run was performed. | PENDING |

## Reusable Flycast route and measurement limits

For interactive use, follow [Build.md](Build.md). To repeat the bounded no-controller probe, use its retained isolated profile. Flycast reads configuration beside its executable; setting only the working directory is insufficient. Copy the profile to a fresh external directory before rerunning to preserve the original evidence.

```powershell
$profile = 'C:\dev\helworks\builds\helengine-dc\input-validation\no-controller'
$profileExe = Join-Path $profile 'flycast.exe'
$elf = 'C:\dev\helworks\builds\helengine-dc\dreamcast-v01-acceptance-20260915-002\helengine_dc.elf'
$flycastArgs = @('-config','log:LogToFile=yes',
  '-config','config:UseReios=yes',
  '-config','config:rend.EmulateFramebuffer=yes',
  '-config','config:rend.ThreadedRendering=no',
  '-config','input:device1=10','-config','input:device2=10',
  '-config','input:device3=10','-config','input:device4=10',
  '-config','config:LuaFileName=input-validation.lua', $elf)
Start-Process -FilePath $profileExe -WorkingDirectory $profile -ArgumentList $flycastArgs -WindowStyle Hidden -Wait
```

The Lua probe reads framebuffer memory at `0x05000000 + FB_W_SOF1`; the P2 alias `0xA5000000 + FB_W_SOF1` produces the same hash. The old `0x04000000` aperture is not valid evidence for DX11 writeback. The diagnostic snapshot stores nanoseconds: typical healthy samples were interval about 16.68 ms, geometry about 572,000 ns, submission about 177,600 ns, and wait about 15.7 ms. Integer millisecond fields that display zero for geometry/submission are truncation, not zero work.

Flycast's Dreamcast `TA_ITP_CURRENT` stayed at `0x00400000`, the initialized `TA_ISP_BASE`, and `diag_param_high=0` throughout. This emulator counter does not expose parameter-buffer consumption for the Dreamcast TA path, so it must not be reported as zero consumption. `diag_vram=6,937,344` is stable allocator evidence. Physical-console acceptance remains pending until the same ELF is exercised through an authorized loader and display path.

# Dreamcast v0.2 authored scene evidence

The v0.2 rows below were produced on 2026-09-15 from the `feature/dreamcast-v02` worktree with the pinned SDK `sha256:a33ea383336b428d8ac9bc507e00fd066092d4b0c8fcea29adccd61b2025f067`, KOS `fcfa7d869471591ca1c777543261a7bfea7cb726`, Flycast 2.7 (`f41c1a4243274caf9a183114bd610407835521a011a48b18326e5e957e6f4e6b`), isolated engine `v02/helengine-source` (`5a0696ca`) and isolated generator `v02/csharpcodegen-source` (`c1d476f`). All artifacts live under `C:\dev\helworks\builds\helengine-dc\v02`. Hidden runs use the Lua probes recorded beside each artifact; the only visible run is the user launch row. Physical hardware remains untested.

## Build identities

| Ref | Build | Source commit | ELF SHA-256 | Notes |
|---|---|---|---|---|
| V4a | `execution/task4-authored-01` (direct `build.ps1 -Mode Authored`) | `62fd903` + uncommitted Task 4 (`f225880` content) | `6ae09f285c4a08489148210710b40b624b148bf7b67478b18fc7e53d05edbc93` | Normal game, no RuntimeInitProbe, staged inputs from `root-graph-task4-v1` |
| V4b | `root-graph-task4-v2/build/authored-a11df066839b4e738fd37ea5105cb4eb` (full generic graph) | `f225880`, clean | same as V4a | Byte-identical to V4a; proves pipeline reproducibility |
| V5 | `execution/task5-authored-01` (direct, Task 5 native) | `f225880` + Task 5 (`73deb6e` content) | `c4577de13d7fff6d862c92e1f6428e7b80e59813030fa6178e04aba0f65f7633` | Authored rendering, resident span 2,060,984 bytes |
| D5 | `execution/task5-diagnostic-01` (Diagnostic, StaticPose) | same tree as V5 | `49aafabf01baf46994646345460099aa9467c1f67d63ea6246ce74cc96b356f4` | Diagnostic regression after shared renderer changes |

## Evidence matrix

| Evidence row | Route | Identity | Observed result | Artifact path |
|---|---|---|---|---|
| Builder tests | `dotnet test builder.tests` against isolated engine | commit `e19363b` | 29/29 passed (definition, cooker, stager, catalog writer, failure paths, cooked-content validator). | `execution/task6-builder-tests/task6-builder-tests.trx` |
| Build-input script tests | `scripts/tests/authored-build-input.tests.ps1` | `f225880` | PASS: missing roots, missing Core.hpp, missing catalog ABI (probe and non-probe), compiler failure, wrong image; no success manifests. | `execution/task4-build-input-tests.log` |
| Native host tests | pinned SDK `make host-test` | `73deb6e` | Geometry, transform, mesh, raster seam, content path and authored submission suites all passed; submission suite also clean under ASan/UBSan. | `execution/task5-host-check-01/submission-asan.log` |
| Normal boot, hidden probe | Flycast Lua, runtime probe symbol `0x8c1f0ed4` | V4a | Core-ready and scene-ready at vblank 35; frame counter 1/60/120/180; checked free heap 14,545,264 B at Core-ready, 14,540,760 B running; Start injected at frame 180; terminal status 5/stage 6, all four flags 1, shutdown free 14,544,392 B at vblank 262. | `execution/task4-boot-01/authored-boot-probe.csv` |
| Full generic graph | `prepare-authored-fixture.ps1 -Build` | V4b | Generation, cooking, staging, catalog, native executor and manifest completed with exit code 0; `GitDirty=false`. | `execution/task4-graph-v2-console.log` |
| Visible user launch | `launch_in_emulator.ps1 -Visible` | V4b | Process owner `DESKTOP-HELENA\Helena`, main window handle 1379886, title `Flycast - helengine_dc`. | `execution/task4-visible-launch.log` |
| Authored rendering, hidden probe | Flycast Lua with `rend.EmulateFramebuffer=yes`, symbols `0x8c202674`/`0x8c2026c4`/`0x8c200328` | V5 | cameras=1, drawables=1, 12 triangles per frame, presented frames equal loop frames (240), clear ARGB `ff080d14`; framebuffer at loop frames 120 and 180 identical (SHA-256 `774e28d5d7fe4933f035fbc9823ac061c1b8028ae8d238f9070643bf5e409e67`), center pixel `0xd104` = (208,32,32) authored red, corner `0x0862` = authored clear; Start at frame 240 then runtime status 5/stage 6 and authored stage 9/error 0 at vblank 294. | `execution/task5-render-01/authored-render-probe.csv`, `authored-render-probe-frame-0120.png` |
| Diagnostic regression | Flycast Lua, diagnostics symbol `0x8c075e84` | D5 | Status 2, error 0, 12 triangles, frame advancing; captured frame SHA-256 `2c13dc96efabe106aae6beac98426f32eea4d8637e18360a8f7efccbde2ae804`, byte-identical to the v0.1 static-pose reference above. | `execution/task5-diagnostic-probe-01/diagnostic-probe.csv` |
| Verifier self-check | `scripts/verify-authored-scene.ps1` | V5 | PASS; frame hashes identical to the manual render probe. | `execution/task6-verify-task5-01/verify-report.txt` |
| Missing-controller operation | verifier `-NoController` (all four maple ports absent) | V5 | Scene ready, frames advanced to 600 with 12 triangles each and checked free heap 14,429,864 B; no shutdown or reset. | `execution/task6-nocontroller-01/verify-report.txt` |
| Sixty-second run | verifier, Start at loop frame 3600 | V5 | 3600 presented frames; 60 periodic heap samples all 14,426,048 B checked free (no per-frame growth in native or generated allocations); captures at frames 600 and 3000 identical to frame 120; Start then runtime status 5/stage 6 and authored stage 9/error 0. | `execution/task6-soak-01/verify-report.txt`, `execution/task6-soak-01-nocapture/verify-probe.csv` |
| Baseline through the graph (shared cache) | `task6/run-variants.ps1`: fixture tool, `prepare-authored-fixture.ps1 -CacheRoot`, verifier | `graph-baseline/build/authored-98d5be530cb34a39ac079eff5b06f722` | Graph exit 0 in 232 s with generated Core reused; ELF SHA-256 identical to V5; verifier PASS with frame 120 hash `774e28d5...`. | `task6/summary.txt`, `task6/verify-baseline/verify-report.txt` |
| Transform variant (mesh +1 X) | same driver | `graph-transform/build/authored-2aa3ffbb3c3142009236fe15c1e10596`, ELF `127b3db4...` | Red cube bbox x [291,564] versus baseline [201,459]: about 96 px right, the predicted focal/6 shift; frame hash `ffccf618...`. | `task6/variant-comparison.txt` |
| Color variant (green material) | same driver | `graph-color/build/authored-d4358ad77ff6418fa23e16b765455e72`, ELF `006d3721...` | Center pixel `0x268a` = (32,208,80), the authored #20D050; same silhouette area as baseline; frame hash `fca46428...`. | `task6/variant-comparison.txt` |
| Camera variant (camera +1 X) | same driver | `graph-camera/build/authored-58e28d2af57945d88508473db94f938d`, ELF `f49de740...` | Cube bbox x [112,354]: moved left, opposite to the transform variant; frame hash `f0be9134...`. | `task6/variant-comparison.txt` |
| Geometry variant (halved local X) | same driver | `graph-geometry/build/authored-63abc1600d73490b932b56c5d7a1cbc9`, ELF `6c0d77a4...` | Cube bbox x [244,415], 46,002 red pixels versus 71,626: narrower silhouette, unchanged entity transform; frame hash `f170ecbf...`. | `task6/variant-comparison.txt` |
| Negative: missing model | driver, fixture `missing-model` | `task6/graph-missing-model` | Graph failed: `FileNotFoundException: Model source file was not found.`; no manifest. | `task6/graph-missing-model.log` |
| Negative: textured material | driver, fixture `textured-material` | `task6/graph-textured-material` | Engine material import failed before the cooker: `imported texture path 'textures/cube.png' without a current texture asset id`; no manifest. The cooker's own texture rejection is covered by `DreamcastMaterialCookerTests`. | `task6/graph-textured-material.log` |
| Negative: translucent material | driver, fixture `transparent-material` | `task6/graph-transparent-material` | Cooker failed: `Dreamcast dc-standard-v1 requires opaque materials.`; no manifest. | `task6/graph-transparent-material.log` |
| Negative: two cameras | driver, fixture `two-cameras` | `task6/graph-two-cameras` | Builder validator failed: `requires exactly one enabled camera component in the startup scene; found 2.`; no manifest. | `task6/graph-two-cameras.log` |
| Negative: over-limit mesh | driver, fixture `over-limit-mesh` (131 positions, 130 triangles) | `task6/graph-over-limit-mesh` | Builder validator failed: `Model 'cooked/imported/models/cube.hasset' has 131 positions; Dreamcast v0.2 supports 1 to 64.`; no manifest. | `task6/graph-over-limit-mesh.log` |
| Wrong startup association (engine graph behaviour) | fixture `wrong-startup` with `sceneRouting.bootSceneId=scenes/missing` (run 1) and additionally the build configuration selecting `scenes/missing` (run 2) | `task6/graph-wrong-startup-routing-only`, `task6/graph-wrong-startup` | Both runs built a normal game whose ELF equals baseline (`c4577de1...`): the generic editor graph substitutes the available project scene instead of failing, so a nonexistent boot scene cannot be turned into a build failure from authored data without changing the shared engine. The builder-level guard (startup id absent from the resolved scene set, lone unrelated scene file) is covered by `DreamcastSceneCompatibilityValidator`/stager unit tests. Recorded as an engine finding, not a v0.2 rejection. | `task6/graph-wrong-startup.log`, `task6/fixture-wrong-startup/user_settings/build_config.json` |

# Dreamcast v0.3 lighting evidence

The v0.3 rows below were produced on 2026-09-16 at Dreamcast repo commit
`95a755c746ef97b73d38f9c49c4c0f95e8e05336` (clean). **On-screen shading proof
for a running authored ELF was not obtained.** Two independent, well-diagnosed
environment blockers prevented it; both are recorded below with full evidence
rather than worked around. Diagnostic-mode evidence (unaffected by either
blocker) and static compile-level evidence (light gathering ABI, budgets) were
obtained and are also recorded below.

## Build identities

| Ref | Build | Route | ELF SHA-256 | Notes |
|---|---|---|---|---|
| L1 | `v03/fixtures/lit-graph/build/authored-82d6c504b679427ba75f245660fb533c` | `prepare-authored-fixture.ps1 -Build`, fixture `lit`, engine version `0.4.0`, live `EngineRoot` | n/a (compile failed) | Fresh codegen; blocked by Blocker 1 below. |
| CT1 | `v03/cube-test/graph-1/build/authored-2790ac458d7943bf99d210b64bbb5900` | same script, `FixtureRoot=C:\dev\helprojs\demodisc`, engine version `1.0.0+ec2d7ff8a5b2624bbc51784bd9eb0c9babed1843` | n/a (compile failed) | Fresh codegen; blocked by the identical Blocker 1, confirming it is not fixture-specific. |
| L2 | `v03/fixtures/lit-direct2/output/authored-d1b154ae99f24c2da1f8a1aeb9168a2e` | direct `DreamcastPlatformAssetBuilder.BuildAsync` (own scratch driver, deleted after use) reusing the 2026-09-15 `v02/cube-test/cache/.../d87d222c.../generated-core`, staging the real `lit` fixture's model bytes, a directly-built `dc-standard` `Lit=true` red `PlatformMaterialAsset`, and a from-scratch scene (camera + lit red cube + `DirectionalLightComponent` sun at yaw -0.65/pitch -0.85, matching the fixture writer's `lit` variant) | `2fdd9fc19ee0847c9bc6d793acb6e943e454e0fc9f22ef1e25bc262455a71a4b` | Compiled and linked successfully (proves the light-gathering/shading code itself compiles against a real lit scene); crashes at runtime under Blocker 2 below before any frame is presented. |
| SC1 | `v03/fixtures/sanity-check/sanity-check-01` | `scripts/build.ps1 -Mode Authored` reusing V5's own already-staged content/catalog byte-for-byte (`v02/cube-test/cache/.../native/dreamcast-build-510d40824e044cd899f2932d3d17eb1d/{content,catalog}`) | `c0438bc60d94bfb2c56631f2b25b8c5dbcbd36ffe51b39cfe818f36dcc9d974e` | Byte-identical to V5 (task5-render-01), which verified successfully on 2026-09-15 with visible red shading. Rebuilt today only to prove the toolchain/Docker/ELF-compile path is still deterministic and unaffected (it is); this ELF also now crashes at runtime under Blocker 2. |
| D1 | `v03/fixtures/diagnostic/task7-diagnostic-01` | `scripts/build.ps1 -Mode Diagnostic -StaticPose` | `d9063e05f39ded287fbb60e24dcab6bb6c7551f6fd9cf26ae948dcc72cdc06c9` | Diagnostic regression build (Step 5). Runs successfully; unaffected by either blocker. |

## Blocker 1: fresh Authored codegen fails with an undeclared `he_cpp_raise`

Building **either** the `lit` fixture (L1) or demodisc's `cube_test` (CT1)
through the full graph (`prepare-authored-fixture.ps1 -Build`, which
regenerates Core C++ fresh) fails identically during the one-time
`generated_unity.o` compile:

```
/generated/GeneratedRuntimeCameraComponentDeserializer.cpp: ... error: 'he_cpp_raise' was not declared in this scope
/generated/GeneratedRuntimeDirectionalLightComponentDeserializer.cpp: ... error: 'he_cpp_raise' was not declared in this scope
/generated/GeneratedRuntimeMeshComponentDeserializer.cpp: ... error: 'he_cpp_raise' was not declared in this scope
/generated/runtime/runtime_scene_catalog_manifest.cpp: ... error: 'he_cpp_raise' was not declared in this scope
/generated/runtime/runtime_code_module_manifest.cpp: ... error: 'he_cpp_raise' was not declared in this scope
/generated/runtime/runtime_physics3d_scene_feature_manifest.cpp:43:1: error: control reaches end of non-void function [-Werror=return-type]
cc1plus: all warnings being treated as errors
make: *** [Makefile:147: /out/obj/generated/generated_unity.o] Error 1
```

`he_cpp_raise` is not defined anywhere in either generated-core snapshot
searched (confirmed by grep across all 784/800 files of both the broken
today's-engine generation and the working 2026-09-15 `d87d222c...` cache,
which uses a different, working raise mechanism entirely). The failure is not
specific to lighting: it also hits `GeneratedRuntimeCameraComponentDeserializer.cpp`
and the code-module/physics3d manifests, which are present and required by
every authored build regardless of scene content. It reproduces with two
different fixture identities (`lit`, engine version `0.4.0`; `cube_test`,
engine version `1.0.0+ec2d7ff8...`) against the same live `EngineRoot`
(`C:\dev\helworks\helengine`), whose `.git` directory was last written
2026-09-16 (today) — i.e. it changed since the 2026-09-15 cache that still
works. This points to a version-skew regression introduced today in the live
shared engine's codegen emission (now calling a `he_cpp_raise` helper) that
the pinned isolated generator (`v02/csharpcodegen-source`, unchanged by this
task) does not yet define. Fixing it requires either re-pinning/patching that
generator or the live engine change, both outside this task's scope, and no
generated C++ was edited to work around it. `he_cpp_raise` counts: 86 error
occurrences in the `cube_test` attempt's `docker-build.log`, matching pattern
in the `lit` attempt's log.

## Blocker 2: authored ELFs crash under Flycast today regardless of content

Verifying L2 (`verify-authored-scene.ps1 -CaptureFrames "120,180"`) failed:
`The Lua probe did not reach a terminal result.` Flycast's own log shows the
runtime never reaches the first vblank probe read (magic stays `0`):

```
00:01:xxx REIOS: Booting up
00:02:xxx REIOS: Booting up
00:03:xxx E[COMMON]: Exception: Fatal: SH4 exception when blocked
00:03:xxx E[COMMON]: Flycast has stopped: Fatal: SH4 exception when blocked
```

This is not caused by L2's hand-built content: the byte-identical rebuild SC1
(reusing V5's own real, previously-staged content) crashes identically, and
re-running **the original, untouched `v03/v03-task5/helengine_dc.elf` file
already on disk since Task 5** (no rebuild at all) crashes the same way, both
hidden (`verify-authored-scene.ps1`) and visible (`launch_in_emulator.ps1
-Visible`, exit code 0 after ~3.5 s with the identical log line). Task 5's own
report recorded this exact ELF rendering successfully on 2026-09-15. Diagnostic
mode (D1, no generated core, no ROMFS) runs perfectly today on the same
machine with the same Flycast binary (hash-pinned, unchanged) at the same
moment. This isolates the regression to Authored-mode execution specifically,
today, on this machine — unrelated to Tasks 1-6's code, to the `lit` fixture,
or to anything staged by this task. **Consequently no `lights=N` verifier
output, no captured frame, and no dominant-color/shading measurement could be
collected for any authored build today**, including the `lit` fixture that is
this task's primary evidence target.

## Evidence actually collected

| Evidence row | Route | Identity | Observed result | Artifact path |
|---|---|---|---|---|
| Lit fixture write | `helengine.dreamcast.fixture.exe --output ... --variant lit` | rebuilt fixture tool | Exit 0; wrote `model=...\lit\assets\models\cube.hasset`, `material=...\lit\assets\materials\cube.helmat`, `scene=...\lit\assets\scenes\startup.helen`. | `v03/fixtures/lit/assets/` |
| Lit content compiles against a real lit scene | direct `BuildAsync` (scratch driver) | L2 | Compiled and linked; 0 errors; deterministic docker build. Proves `DreamcastRenderManager3D.cpp`'s `gatherLights`, the lit shading pass and the `dc-standard`/`Lit` material path all build correctly against authored content that actually contains a `DirectionalLightComponent`. | `v03/fixtures/lit-direct2/output/authored-d1b154ae99f24c2da1f8a1aeb9168a2e/docker-build.log` |
| Embedded cooked payload size | `romdisk.img` on disk | L2 | 3.0 KB, well under the 1 MiB budget. | `v03/fixtures/lit-direct2/output/authored-d1b154ae99f24c2da1f8a1aeb9168a2e/romdisk.img` |
| Resident ELF span | pinned `sh-elf-readelf -l` | L2 | LOAD segments span `0x8c010000`..`0x8c21cb58` = 2,149,208 bytes (~2.05 MiB), well under the 8 MiB budget. | computed from `sh-elf-readelf -l` output, not separately saved to a file |
| Free heap after startup | n/a | — | **Not measured.** Requires a running authored frame loop, which Blocker 2 prevents for every authored build attempted today. The last pre-regression figure on record (Task 5, 2026-09-15, unrelated build) was 14,545,264 bytes at Core-ready; that number is historical context only, not evidence collected by this task. | — |
| cube_test fresh build | `prepare-authored-fixture.ps1 -Build` | CT1 | Blocked by Blocker 1 (see above); scene content hash re-synced to `sha256:b908a7c342763d1000bdb51088fe859b5c86f0d77bb04974ea3c6c370014cf21` in `build_config.json` immediately before the attempt, confirmed unchanged after the failed attempt (not a hash race). Not retried a second time per the plan's ruling, since the failure is deterministic, not a race. | `v03/cube-test/graph-1/build/authored-2790ac458d7943bf99d210b64bbb5900/docker-build.log` |
| Diagnostic regression | `scripts/build.ps1 -Mode Diagnostic -StaticPose`, probe copied to a fresh directory with the pinned `sh-elf-nm` address (`0x8c075fa4`) re-resolved for this ELF | D1 | `status=2 error=0 tris=12 verts=36 clipped=0 nonzero=432607`; captured frame SHA-256 `2c13dc96efabe106aae6beac98426f32eea4d8637e18360a8f7efccbde2ae804` — **exact match** to the pinned regression hash. Unlit diagnostic path fully unchanged by this plan. | `v03/fixtures/diagnostic/probe-01/diagnostic-frame-0090.raw`, `.../diagnostic-probe.csv` |
| Visible launch | `launch_in_emulator.ps1 -Visible` | D1 (the only build that actually runs today; no authored build could be launched visibly per Blocker 2) | Process owner `DESKTOP-HELENA\Helena`, `MainWindowHandle=1709150` (nonzero), title `Flycast - helengine_dc`, `Responding=True`. Left running for the user. | `v03/fixtures/diagnostic/task7-diagnostic-01/emulator-stdout.log` |

Physical hardware remains untested, as for all prior rows in this document.

# Dreamcast v0.3 lighting evidence: resolved 2026-09-17

Both blockers above are cleared and **on-screen shading proof for a running
authored ELF was obtained.** The rows below supersede the "not obtained"
statement at the head of the v0.3 section; that section is kept as the record
of what was true on 2026-09-16.

Blocker 1 was not an undefined `he_cpp_raise`. Once the generator gained its
capability-aware runtime, the Dreamcast platform definition was still
declaring `native-file-stream-descriptor-apis`, an option the generator no
longer reads, so the hosted file-stream path stayed enabled and called
`fileno`, `ftruncate` and `fstat`, which KallistiOS does not declare. The
option is now `codegen-use-hosted-file-system=false`. Two further generator
faults surfaced behind it and are recorded under "Generator defects" below.

Blocker 2, the Authored-mode runtime crash, does not reproduce. Every authored
run today reached a clean shutdown with `au_error=0`.

## Build identity

| Ref | Build | Route | ELF SHA-256 |
|---|---|---|---|
| L3 | `v02/cube-test/graph-8/build/authored-bc9852d6ad5e4b00bce07b4ad1bff852` | `prepare-authored-fixture.ps1 -Build`, `FixtureRoot=C:\dev\helprojs\demodisc`, engine version `1.0.0+ec2d7ff8...`, fresh generated Core | `7457faea2c6966e54846ceed004ba3489c626f4a86d1ef46c0336f8ddf6808be` |

Repo commit `86ab188020cd65d7d192031ed86ea038bba1df3b`, `GitDirty=false`.
Shared checkouts at run time: helengine `main cd9b996d`, csharpcodegen
`master c3c0521` (also the engine's vendored submodule, which is what the
graph actually builds the generator from; `-CodegenToolPath` does not steer it).

## Evidence

| Evidence row | Route | Observed result | Artifact path |
|---|---|---|---|
| Fresh Core exposes the camera field of view | generated header inspection | `virtual float get_FieldOfView() = 0;` and its setter present, so the projection reads authored camera state rather than a platform constant. | `v02/cube-test/cache-fix-1/.../generated-core/ICamera.hpp` |
| Light deserializer generated | generated source listing | `GeneratedRuntimeDirectionalLightComponentDeserializer.cpp` emitted; its absence was one of the two 2026-09-16 blockers. | `v02/cube-test/cache-fix-1/.../generated-core/` |
| Cooked materials carry the renamed profile | content grep | Both `engine/materials/standard.hasset` and `materials/rendering/cube_test/cubetestsolid.hasset` contain `dc-standard`; no `dc-unlit` anywhere in cooked content. The stale-content blocker is gone. | `v02/cube-test/cache-fix-1/.../content/cooked/` |
| Cube material is lit | cooked byte inspection | `Lit=01`, base colour `ff ff ff ff` (white), no texture, single sided. | `.../cooked/materials/rendering/cube_test/cubetestsolid.hasset` |
| Lights gathered and published | verifier snapshot | `lights=1` in the authored snapshot, at `au_version=2`. | `v02/cube-test/verify-8/verify-report.txt` |
| Authored run, 242 frames | `verify-authored-scene.ps1 -CaptureFrames 120,180` | PASS. `cameras=1 drawables=1 tris=12 clipped=0 rejected=0 presented=242 lights=1 au_error=0`, clean shutdown, checked free heap 14,511,760 B. | `v02/cube-test/verify-8/verify-report.txt` |
| Authored run, 602 frames | `verify-authored-scene.ps1 -CaptureFrames 30,150,300,450 -StartAfterFrames 600` | PASS. `presented=602 lights=1 au_error=0`, clean shutdown. | `v02/cube-test/verify-8-spin/verify-report.txt` |
| **Directional shading visible on screen** | framebuffer capture, frame 450 | The rotated cube shows its lit face at `0xad35` = (173,166,173) and its away-facing face at `0x2104` = (33,32,33) in the same frame, from one directional light on a white base colour. Roughly 68 per cent versus 13 per cent of white: a Lambert term on the lit face and ambient only on the other. | `v02/cube-test/verify-8-spin/frame-0450.png`, `.raw` |
| Shading tracks rotation | four captures across one run | Frame 300 is face-on and uniform at `0x8c71` = (140,142,140); frames 30, 150 and 450 show two distinct face shades. Frames differ by up to 41,844 bytes. Shading changes with orientation, which is what per-vertex Lambert under a fixed world light must do. | `v02/cube-test/verify-8-spin/frame-{0030,0150,0300,0450}.raw` |

## Generator defects found while unblocking this

Neither was worked around in emitted C++, and the shared generator was not
modified. Both are worth fixing upstream.

1. `runtime/native_exceptions.hpp` gives `SelectMessage`, `MessageWithParameter`
   and `MessageWithFileName` a `fallback` parameter that only the compact-message
   branch consumes. The full-message branch silences `message` but not
   `fallback`, so any target compiling generated headers under
   `-Werror=unused-parameter` fails. Dreamcast now selects compact messages,
   which suits a 16 MiB console, but the next target wanting full messages will
   hit this.
2. Under compact messages the generated deserializers still emit
   `receivedVersion`, `hasReceivedVersion`, `receivedMemberCount` and
   `hasReceivedMemberCount`, which exist only to be interpolated into the full
   message text. With no message to build they are set and never read, tripping
   `-Werror=unused-but-set-variable` in four deserializers. Demoted for
   generated translation units only; our own sources keep full `-Werror`.

Physical hardware remains untested, as for all prior rows in this document.
