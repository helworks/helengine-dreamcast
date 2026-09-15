# Dreamcast v0.1 build and launch

The supported route uses Docker Engine 29.6.1, PowerShell 7+, the pinned KallistiOS checkout, and Flycast 2.7. The compiler comes from the pinned `pcercuei/dreamcast-toolchain` base image; the image is rebuilt locally from [docker/Dockerfile](../docker/Dockerfile) and recorded in [toolchain-lock.txt](../toolchain-lock.txt).

The locked inputs are:

- Upstream image: `pcercuei/dreamcast-toolchain@sha256:504ed29fad5e5565579bf00235fc011fdde656004e50bde4aa24a93b59e2a7bb`.
- Local image tag: `helengine-dc-sdk:v01` (the current recorded image ID is in `toolchain-lock.txt`; rebuilding may produce a different ID).
- KOS checkout: `C:\dev\helworks\builds\helengine-dc\dependencies\kos`, commit `fcfa7d869471591ca1c777543261a7bfea7cb726`.
- ABI: `sh-elf`, SuperH little-endian, `-m4-single -ml`; the wrapper is `kos-c++` inside the image.
- Flycast executable: `C:\dev\helworks\builds\helengine-dc\dependencies\flycast-2.7\flycast.exe`, version 2.7.

From the worktree root, prepare the image and create a fresh external build directory:

```powershell
pwsh -File .\scripts\prepare_toolchain.ps1 -KosPath C:\dev\helworks\builds\helengine-dc\dependencies\kos -ImageTag helengine-dc-sdk:v01
pwsh -File .\scripts\build.ps1 -BuildId dreamcast-v01-20260915-001 -DockerImage helengine-dc-sdk:v01
pwsh -File .\scripts\launch_in_emulator.ps1 -BuildId dreamcast-v01-20260915-001 -FlycastPath C:\dev\helworks\builds\helengine-dc\dependencies\flycast-2.7\flycast.exe -Visible
```

`build.ps1` refuses to reuse an existing build ID. It mounts source and KOS read-only, mounts only the selected external build directory for writes, checks the exact KOS commit and local image ID, runs the pinned `sh-elf-readelf`, and writes `build-manifest.txt` with the ELF hash, source commit, KOS commit, image identifiers, compiler flags, and command. The ELF, map, logs, and manifest are under `C:\dev\helworks\builds\helengine-dc\<build-id>`.

The image contains a compiled KOS copy at `/opt/toolchains/dc/kos-built`. The validated checkout is still mounted read-only at `/opt/toolchains/dc/kos` and its commit is checked before the build; the image-local copy prevents a read-only source mount from hiding the compiled libraries. Diagnostic selectors are recorded in the manifest and map to the native compile-time configuration:

```powershell
pwsh -File .\scripts\build.ps1 -BuildId dreamcast-v01-clear-20260915-001 -Scene ClearOnly -StaticPose -DisableCulling -StopAfterSeconds 1
```

`-Scene` accepts `Cube`, `ClearOnly`, or `ScreenTriangle`; `-StaticPose`, `-DisableCulling`, `-ReverseTriangleOrder`, `-NearPlaneProbe`, `-ScreenEdgeProbe`, and `-StopAfterSeconds` select the approved diagnostics.

The first execution route is a direct ELF/HLE emulator run. `launch_in_emulator.ps1` verifies the manifest hash before starting, creates an isolated Flycast profile under that build ID, enables HLE BIOS, and redirects emulator stdout/stderr to build-local logs. It is hidden by default; pass `-Visible` for interactive use. The launcher does not create a disc image or `IP.BIN`.

Physical-console acceptance is a separate evidence row. It requires an authorized loader and display path and is recorded independently from emulator results.
