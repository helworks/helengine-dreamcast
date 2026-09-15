# Dreamcast v0.1 build and launch

The supported route uses Docker Engine with BuildKit and Linux-container support, PowerShell 7+, the pinned KallistiOS checkout, and Flycast 2.7. Docker Engine 29.6.1 was tested. The compiler comes from the pinned `pcercuei/dreamcast-toolchain` base image; the image is rebuilt locally from [docker/Dockerfile](../docker/Dockerfile) and recorded in [toolchain-lock.txt](../toolchain-lock.txt).

The locked inputs are:

- Upstream image: `pcercuei/dreamcast-toolchain@sha256:504ed29fad5e5565579bf00235fc011fdde656004e50bde4aa24a93b59e2a7bb`.
- Local image tag: `helengine-dc-sdk:v01`, current authoritative prepared image ID `sha256:a33ea383336b428d8ac9bc507e00fd066092d4b0c8fcea29adccd61b2025f067`.
- Historical note: preparation previously produced `sha256:dbba6bed5fb6435ff703ba72875fd171fbd450b954a2d280602772433664d561` and `sha256:469db4c6155dd4ce1c6b6597b4a135c7c22a6e49c6f7dbb7a374e1d5ab4ef25a`; the initial bootstrap image was `sha256:164e4966dcc0e273ea8e73d6d53d0e32518e2ee34c09abbba1efe396ab79793c`. The latest preparation result is recorded in the lock file.
- KOS checkout: `C:\dev\helworks\builds\helengine-dc\dependencies\kos`, commit `fcfa7d869471591ca1c777543261a7bfea7cb726`.
- ABI: `sh-elf`, SuperH little-endian, `-m4-single -ml`; the wrapper is `kos-c++` inside the image.
- Flycast executable: `C:\dev\helworks\builds\helengine-dc\dependencies\flycast-2.7\flycast.exe`, version 2.7.

On a fresh machine, create the external KOS checkout and place it at the locked path before running the preparation script. The preparation script validates this checkout and does not fetch or change it implicitly:

```powershell
git clone https://github.com/KallistiOS/KallistiOS.git C:\dev\helworks\builds\helengine-dc\dependencies\kos
git -C C:\dev\helworks\builds\helengine-dc\dependencies\kos fetch --depth 1 origin fcfa7d869471591ca1c777543261a7bfea7cb726
git -C C:\dev\helworks\builds\helengine-dc\dependencies\kos checkout --detach fcfa7d869471591ca1c777543261a7bfea7cb726
```

The pinned Flycast 2.7 executable is supplied separately at the path above; preparation requires the known release SHA-256 `f41c1a4243274caf9a183114bd610407835521a011a48b18326e5e957e6f4e6b`, records it in `toolchain-lock.txt`, and the launcher checks it before launch.

From the worktree root, prepare the image and create a fresh external build directory:

```powershell
pwsh -File .\scripts\prepare_toolchain.ps1 -KosPath C:\dev\helworks\builds\helengine-dc\dependencies\kos -ImageTag helengine-dc-sdk:v01
pwsh -File .\scripts\build.ps1 -BuildId dreamcast-v01-20260915-001 -DockerImage helengine-dc-sdk:v01
pwsh -File .\scripts\launch_in_emulator.ps1 -BuildId dreamcast-v01-20260915-001 -FlycastPath C:\dev\helworks\builds\helengine-dc\dependencies\flycast-2.7\flycast.exe -Visible
```

`build.ps1` refuses to reuse an existing build ID. It mounts source and KOS read-only, mounts only the selected external build directory for writes, checks the exact KOS commit and local image ID, runs the pinned `sh-elf-readelf`, and writes `build-manifest.txt` with the ELF hash, source commit, dirty status, source-input aggregate hash, KOS commit, image identifiers, compiler flags, diagnostic selectors, and command. It hashes the build inputs before and after Docker runs and refuses to write a manifest if they differ; `source-hashes-before.txt` and `source-hashes.txt` record the per-file SHA-256 values. The ELF, map, logs, and manifest are under `C:\dev\helworks\builds\helengine-dc\<build-id>`.

The image contains a compiled KOS copy at `/opt/toolchains/dc/kos-built`. The validated checkout is still mounted read-only at `/opt/toolchains/dc/kos` and its commit is checked before the build; the image-local copy prevents a read-only source mount from hiding the compiled libraries. Diagnostic selectors are recorded in the manifest and map to the native compile-time configuration:

```powershell
pwsh -File .\scripts\build.ps1 -BuildId dreamcast-v01-clear-20260915-001 -Scene ClearOnly -StaticPose -DisableCulling -StopAfterSeconds 1
```

`-Scene` accepts `Cube`, `ClearOnly`, or `ScreenTriangle`; `-StaticPose`, `-DisableCulling`, `-ReverseTriangleOrder`, `-NearPlaneProbe`, `-ScreenEdgeProbe`, and `-StopAfterSeconds` select the approved diagnostics.

The first execution route is a direct ELF/HLE emulator run. `launch_in_emulator.ps1` verifies the manifest hash before starting, creates an isolated Flycast profile under that build ID, enables HLE BIOS, and redirects emulator stdout/stderr to build-local logs. It is hidden by default; pass `-Visible` for interactive use. The launcher does not create a disc image or `IP.BIN`.

Physical-console acceptance is a separate evidence row. It requires an authorized loader and display path and is recorded independently from emulator results.
