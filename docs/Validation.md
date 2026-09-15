# Dreamcast v0.1 validation evidence

This matrix records observed evidence by build ID. A blank or `PENDING` row is not an acceptance claim. Every populated row must name the exact KOS commit, local SDK image ID, ELF SHA-256, route, and artifact path.

| Evidence row | Command / route | Build ID | KOS commit / SDK image ID / ELF SHA-256 | Observed result | Artifact path |
|---|---|---|---|---|---|
| Host geometry tests | Pinned `helengine-dc-sdk:v01`: `make -C /src BUILD_ROOT=/out host-test` | dreamcast-v01-20260915-001 | KOS `fcfa7d869471591ca1c777543261a7bfea7cb726`; SDK `sha256:469db4c6155dd4ce1c6b6597b4a135c7c22a6e49c6f7dbb7a374e1d5ab4ef25a`; ELF `835053e54b142ba1d487bc753aa31cfb19eaabf83f2594bd11a079de7144c74b` | Geometry and raster seam tests passed; exit code 0. | `C:\dev\helworks\builds\helengine-dc\dreamcast-v01-20260915-001\dreamcast-geometry-tests`; `dreamcast-raster-seam-tests` |
| KOS hello parity | Pinned KOS hello example build/run | PENDING | PENDING | PENDING | PENDING |
| KOS PVR example parity | Pinned KOS PVR example build/run | PENDING | PENDING | PENDING | PENDING |
| ELF header, map, and hash | `scripts/build.ps1 -BuildId dreamcast-v01-20260915-001`; pinned `sh-elf-readelf -h` | dreamcast-v01-20260915-001 | KOS `fcfa7d869471591ca1c777543261a7bfea7cb726`; SDK `sha256:469db4c6155dd4ce1c6b6597b4a135c7c22a6e49c6f7dbb7a374e1d5ab4ef25a`; ELF `835053e54b142ba1d487bc753aa31cfb19eaabf83f2594bd11a079de7144c74b` | Build exit code 0; ELF32, 2's complement little-endian, Renesas/SuperH SH; map present. | `C:\dev\helworks\builds\helengine-dc\dreamcast-v01-20260915-001\helengine_dc.elf`; `helengine_dc.map`; `build-manifest.txt`; `elf-header.txt` |
| Dark clear screen | Flycast direct ELF/HLE with clear-only diagnostic | PENDING | PENDING | PENDING | PENDING |
| Screen-space triangle | Flycast direct ELF/HLE with triangle diagnostic | PENDING | PENDING | PENDING | PENDING |
| Centered fixed cube | Flycast direct ELF/HLE with fixed cube pose | PENDING | PENDING | PENDING | PENDING |
| Culling-disabled diagnostic | Flycast direct ELF/HLE with culling disabled | PENDING | PENDING | PENDING | PENDING |
| Reverse submission order | Flycast direct ELF/HLE with reverse order | PENDING | PENDING | PENDING | PENDING |
| Near-plane crossings | Flycast direct ELF/HLE near-plane probe | PENDING | PENDING | PENDING | PENDING |
| Screen-edge crossings | Flycast direct ELF/HLE screen-edge probe | PENDING | PENDING | PENDING | PENDING |
| Ten-minute animation | Flycast direct ELF/HLE timed run; inspect logs and allocation diagnostics | PENDING | PENDING | PENDING | PENDING |
| Missing-controller startup | Flycast direct ELF/HLE with no controller attached | PENDING | PENDING | PENDING | PENDING |
| Start exit | Flycast direct ELF/HLE; press Start on an available controller | PENDING | PENDING | PENDING | PENDING |
| Emulator functional result | Flycast vblank/Lua or equivalent internal framebuffer and log evidence | PENDING | PENDING | PENDING | PENDING |
| Physical-console result | Authorized loader/display path using the same ELF | PENDING | PENDING | PENDING | PENDING |

Emulator observations must be labeled as Flycast evidence and cite the build-local logs or internal framebuffer/Lua artifacts. Screenshots are optional and are not required for this record. Physical-console evidence remains pending until the same ELF is exercised on hardware with an authorized loader and display path. Timing, VRAM, and allocation claims are measured only when the corresponding diagnostic output exists; research documents are not evidence.
