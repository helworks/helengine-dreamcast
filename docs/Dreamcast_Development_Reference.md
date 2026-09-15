# Dreamcast Development Reference

**An engineering guide built around independently developed, open-source tools**  
Research date: **15 September 2026**  
Audience: C/C++ developers building original Dreamcast software or a portable game-engine backend.

## How to use this document

Start with the toolchain and first executable, then implement a small native PVR renderer. Read the memory, synchronization, and asset-format sections before optimizing it. Keep the deployment and source-path tables open while working.

**Recommended foundation:** KallistiOS, its patched GNU SH-4 toolchain, C or C++, native KOS PVR rendering, offline asset conversion, and repeated testing on a retail console. GLdc is a useful alternative for existing fixed-function OpenGL code.

“License free” here means **no proprietary Sega SDK agreement or SDK royalty is needed for the recommended development stack**. The software still has licenses. Attribution, notices, and sometimes source-distribution obligations apply. This document identifies those obligations and excludes leaked Sega SDKs, Katana/WinCE libraries, retail game assets, and downloaded console firmware from the recommended workflow. It cannot certify every jurisdiction's copyright, trademark, or anti-circumvention law.

**Boot-image exception:** a complete conventional `IP.BIN` contains a Sega-authored region for which the examined image-building project claims no license. Therefore this guide does **not** call self-booting disc images fully license-cleared. The strict baseline is to build your original ELF and use an independently implemented emulator/HLE path; physical-console execution additionally needs a boot/loader route you are authorized to use. See section 14 before producing a distributable disc image.

KOS source paths and configuration values were checked against commit [`fcfa7d869471591ca1c777543261a7bfea7cb726`](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726), dated 14 September 2026. Links to that revision are reproducible; online API documentation tracks a moving branch. Commands and original example code below are a source-reviewed starting point, **not a claim of a completed cross-compilation or hardware benchmark**. Numerical budgets labeled “example” are design proposals, not measured platform limits.

## Contents

1. [Open-source stack and licensing](#1-open-source-stack-and-licensing)
2. [Hardware architecture](#2-hardware-architecture)
3. [Install and build the toolchain](#3-install-and-build-the-toolchain)
4. [File paths and project organization](#4-file-paths-and-project-organization)
5. [First executable and C++ policy](#5-first-executable-and-c-policy)
6. [CPU, memory, caches, and transfers](#6-cpu-memory-caches-and-transfers)
7. [PowerVR2 rendering model](#7-powervr2-rendering-model)
8. [A practical graphics-engine design](#8-a-practical-graphics-engine-design)
9. [Textures and the asset pipeline](#9-textures-and-the-asset-pipeline)
10. [Memory and performance budgets](#10-memory-and-performance-budgets)
11. [Audio and the AICA](#11-audio-and-the-aica)
12. [Controllers, VMU, and peripherals](#12-controllers-vmu-and-peripherals)
13. [Filesystems, disc access, and networking](#13-filesystems-disc-access-and-networking)
14. [Booting and deploying builds](#14-booting-and-deploying-builds)
15. [Debugging and profiling](#15-debugging-and-profiling)
16. [Implementation milestones](#16-implementation-milestones)
17. [Source-code and example index](#17-source-code-and-example-index)
18. [Common mistakes and corrections](#18-common-mistakes-and-corrections)
19. [Release checklist and evidence limits](#19-release-checklist-and-evidence-limits)

## 1. Open-source stack and licensing

### 1.1 What to use

| Component | Purpose | License position and practical choice |
|---|---|---|
| **KallistiOS (KOS)** | Startup, kernel services, hardware drivers, PVR, audio, files, input | BSD-style KOS license; suitable for original commercial or noncommercial software with required notices. |
| **kos-chain** | Builds the patched SH-4 compiler, assembler, linker, C library, debugger | A build system within KOS; individual GNU tools and runtime components retain their own licenses. |
| **GCC + Binutils + GDB** | Compile, link, inspect, and debug | GNU software licenses; using a GPL compiler does not by itself place your game under GPL. Redistributing the tools is a separate matter. |
| **Newlib** | Embedded C runtime | A collection of licenses, largely permissive; retain the applicable notices from the exact build. |
| **libgcc / libstdc++** | Compiler support and C++ standard library | Covered files have the GCC Runtime Library Exception, allowing eligible independently written programs to use them under their own terms. |
| **Native KOS PVR API** | Direct control of the Dreamcast graphics pipeline | Included in KOS; the recommended starting point for a new console-specific renderer. |
| **GLdc, optional** | Hardware-accelerated fixed-function OpenGL compatibility | Check the GLdc section and exact dependency revision; this is a compatibility layer over Dreamcast hardware, not a modern shader API. |
| **kos-ports, optional** | Ported libraries, codecs, formats, scripting, wrappers | Every selected library needs a separate license check. The package collection is not one permissive license. |
| **DreamSDK, alternative host setup** | Packaged Windows development environment | Its project is GPL-3.0; bundled compiler, SDK, libraries, and tools each have their own terms. The packaging license is not automatically your game's license. |

KOS is a library and small operating-system environment linked into your executable. It is not Linux, a desktop process environment, or an engine that provides an editor, scene graph, physics, and asset import. Its ordinary configuration shares an address space; do not assume memory isolation between threads. Sources: [KOS overview](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/doc/README.md), [KOS licensing](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/doc/LICENSE.md), [Newlib licenses](https://sourceware.org/newlib/COPYING.NEWLIB), [libstdc++ license and runtime exception](https://gcc.gnu.org/onlinedocs/libstdc++/manual/license.html), [DreamSDK project](https://github.com/dreamsdk/dreamsdk).

### 1.2 Required attribution is more than a credit line

For KOS source redistribution, preserve copyright notices, license conditions, and the disclaimer. For a binary release, reproduce those materials in the documentation or other accompanying materials. Do not imply that the KOS authors endorse your product. Include the contributors' information for the version used; a short “Made with KOS” splash screen alone does not reproduce the complete license requirements. The authoritative text is [`doc/license/LICENSE.KOS`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/doc/license/LICENSE.KOS).

Recommended release records:

| File in your repository or release | Contents |
|---|---|
| `THIRD_PARTY_NOTICES.md` | Component names, authors, versions, source links, and notice locations. |
| `licenses/` | Unmodified license and copyright texts applicable to shipped code and data. |
| `toolchain-lock.txt` | KOS commit, port revisions, compiler versions, ABI flags, and host-tool revisions. |
| `assets/manifest.csv` | Asset origin, creator, permission/license, modifications, and required credit. |
| `release/build-manifest.txt` | Build identifier, hashes, target hardware assumptions, and included files. |

For GPL or LGPL dependencies, examine the actual integration and distribution terms before selecting them. Static linking can matter, especially for LGPL relinking requirements. Avoid adding a codec or convenience wrapper merely because it is available in kos-ports. A host-only converter's license and the license of code linked into your executable are different questions. A converter can also embed a separately copyrighted bootstrap, font, or template; audit those outputs separately.

### 1.3 What this stack intentionally does not require

You do not need leaked Sega Katana libraries, Sega Windows CE development libraries, commercial SDK headers, original game's source or textures, or an online BIOS download to write and compile an original KOS program. Owning a console does not automatically grant permission to redistribute its ROM, fonts, branding, or retail game data. The boot-image section explains why an open-source image builder still requires a separate review of embedded boot material.

## 2. Hardware architecture

| Subsystem | Working specification | Consequence for development |
|---|---|---|
| Main CPU | 200 MHz SH-4, little-endian target, superscalar execution | One general-purpose core handles game logic and geometry preparation. |
| CPU caches | 8 KiB instruction, 16 KiB operand/data, 32-byte lines | Code size, data locality, and transfer coherence matter. |
| Main memory | 16 MiB on a normal retail console | Executable, stacks, meshes, world state, and staging buffers share a tight budget. |
| Graphics | 100 MHz NEC PowerVR2 implementation | Tile-based deferred rendering with a fixed-function pipeline. |
| Video memory | 8 MiB | Framebuffers, TA/PVR working data, textures, and render targets compete. |
| Sound | Yamaha AICA, 64 hardware voices, ARM7 driver processor | Hardware audio mixing; custom ARM code is a separate firmware task. |
| Sound memory | 2 MiB | Driver, effects, and streaming buffers share their own pool. |
| Input | Maple bus, four external console ports | Enumerate controllers and subdevices; tolerate hot unplug. |
| Storage | Optical disc; VMU saves; optional SD/ATA/ODE arrangements | Streaming and persistence depend on hardware and initialized drivers. |
| Network | Modem or optional Ethernet adapter | Treat networking and host debugging as explicitly configured features. |

The CPU specification comes from [Hitachi's public SH-4 announcement](https://www.hitachi.com/New/cnews/E/1997/971110B.html) and [Renesas's SH7750S information](https://www.renesas.com/en/products/sh7750s); KOS encodes the cache geometry in [its architecture header](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/cache.h). Graphics identification is corroborated by [Imagination's retrospective](https://blog.imaginationtech.com/celebrating-the-20th-anniversary-of-dreamcast-and-powervr/). Memory/device details are represented by the [maintained MAME Dreamcast implementation](https://github.com/mamedev/mame/blob/1a8267fa579f62cee37c65e82aabc09b328a1eb6/src/mame/sega/dccons.cpp) and the KOS subsystem interfaces cited below.

The three memory pools are not one unified 26 MiB heap. The SH-4 communicates with graphics/audio and external devices through distinct memory windows and buses. DMA and hardware engines can overlap some work, but transfer setup, contention, and ownership remain software responsibilities.

Historical MIPS, FLOPS, fill-rate, and polygons-per-second figures describe particular instruction mixes or rendering conditions. They are useful architectural context, not promised game performance. Design around measured frame time and memory high-water marks.

## 3. Install and build the toolchain

### 3.1 Choose a host workflow

Use **Linux or WSL with the KOS source toolchain** for a reproducible command-line environment. Use **DreamSDK** if you prefer a packaged Windows setup and its integrations. macOS is also supported by kos-chain, with platform-specific dependencies. Do not mix compiler executables and prebuilt libraries from unrelated installations.

The current source builder is **`utils/kos-chain`**. Many tutorials use the historical **`utils/dc-chain`** path, old `config.mk` profiles, or `environ_sh`. Those instructions describe a different tree. The reference checkout uses `Makefile.dreamcast.cfg`, a local `Makefile.cfg`, and `environ.sh`. Sources: [kos-chain instructions](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/kos-chain/README.md), [DreamSDK README](https://github.com/dreamsdk/dreamsdk).

### 3.2 Stable toolchain at the researched revision

| Setting | Value |
|---|---|
| CPU target | `sh-elf`, little endian |
| Selected profile | `stable` |
| GCC | `15.2.0` |
| Binutils | `2.45.1` |
| Newlib | `4.6.0.20260123` |
| Default floating-point ABI | `-m4-single` |
| C++ support | Enabled by default in the Dreamcast configuration |
| ARM audio compiler | `arm-eabi`, optional for rebuilding or replacing the AICA driver |

These are **the profile's values at the pinned commit**, not a claim that each is the newest upstream release. Sources: [stable profile](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/kos-chain/profiles/dreamcast/stable.mk), [Dreamcast configuration](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/kos-chain/Makefile.dreamcast.cfg).

### 3.3 Linux/WSL setup sequence

The following is an **adapted Debian/Ubuntu-style recipe**. Review the dependency instructions for your distribution; it is not an installer executed as part of this research. Give your normal user ownership of a new toolchain directory and perform compilation without root privileges.

```bash
sudo apt-get update
sudo apt-get install build-essential git curl wget texinfo bison flex \
    libgmp-dev libmpfr-dev libmpc-dev gettext patch bzip2 xz-utils \
    libjpeg-dev libpng-dev libelf-dev python3 cmake ninja-build pkg-config

sudo install -d -o "$(id -un)" -g "$(id -gn)" /opt/toolchains/dc
cd /opt/toolchains/dc
git clone https://github.com/KallistiOS/KallistiOS.git kos
git -C kos checkout fcfa7d869471591ca1c777543261a7bfea7cb726

cd /opt/toolchains/dc/kos/utils/kos-chain
cp Makefile.dreamcast.cfg Makefile.cfg
# Read Makefile.cfg; keep platform=dreamcast and toolchain_profile=stable.
make
```

The builder handles downloading and building the selected components; build logs are in its `logs/` directory. Do not use a general Linux `sh4-linux-gnu` compiler as a substitute for the KOS-patched `sh-elf` toolchain. Sources: [host dependencies](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/kos-chain/doc/debian.md), [builder README](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/kos-chain/README.md).

Configure and build KOS after the compiler is installed:

```bash
cd /opt/toolchains/dc/kos
cp doc/environ.sh.sample environ.sh
# Review paths in environ.sh if your installation differs from /opt/toolchains/dc.
source environ.sh
make -j"$(nproc)"

sh-elf-gcc --version
sh-elf-g++ --version
git rev-parse HEAD
make -C examples/dreamcast/hello
sh-elf-readelf -h examples/dreamcast/hello/hello.elf
```

Check for an ELF targeting **Renesas/SuperH** with the expected little-endian encoding. Keep the ELF with symbols for debugging. To use an optional port, clone the maintained [kos-ports repository](https://github.com/KallistiOS/kos-ports) into the location configured by `KOS_PORTS`, pin it, inspect the selected package's `Makefile` and upstream license, then follow that package's build instructions. Core hello-world development does not require installing every port.

### 3.4 Environment paths that matter

| Variable | Conventional value | Role |
|---|---|---|
| `KOS_BASE` | `/opt/toolchains/dc/kos` | KOS checkout root. |
| `KOS_PORTS` | `/opt/toolchains/dc/kos-ports` | Ported dependencies and installed port files. |
| `KOS_CC_BASE` | `/opt/toolchains/dc/sh-elf` | Cross-compiler installation. |
| `KOS_CC_PREFIX` | `sh-elf` | Tool executable prefix. |
| `DC_ARM_BASE` | `/opt/toolchains/dc/arm-eabi` | Optional AICA toolchain. |
| `DC_TOOLS_BASE` | `/opt/toolchains/dc/bin` | External host tools such as dc-tool. |
| `KOS_CMAKE_TOOLCHAIN` | `${KOS_BASE}/utils/cmake/kallistios.toolchain.cmake` | CMake cross-build configuration. |
| `KOS_LOADER` | For example, `dc-tool-ip -t 192.168.1.100 -x` | Loader invoked by sample `make run` targets; choose your actual console address. |

Source: [environment template](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/doc/environ.sh.sample). Source `environ.sh` in each build shell. Configure the IDE to launch builds in that environment; editor include-path configuration alone is insufficient.

## 4. File paths and project organization

### 4.1 Four different meanings of “path”

| Kind | Example | Meaning |
|---|---|---|
| Host installation path | `/opt/toolchains/dc/kos` | Files on your development computer. |
| Repository source path | `kernel/arch/dreamcast/include/dc/pvr.h` | A file within the KOS checkout. |
| Target filesystem path | `/cd/assets/main.pak` | Resolved by KOS while the program runs on the Dreamcast. |
| Hardware address | `0x8c000000` | A CPU-visible address; not a filesystem path. |

These namespaces are not interchangeable. Shipping a host pathname in a material file does not make that file available on the console.

### 4.2 Suggested engine layout

This is a proposed layout for your own project, not a KOS requirement.

| Path | Responsibility |
|---|---|
| `src/core/` | Containers, math interfaces, identifiers, scheduling, serialization. |
| `src/game/` | Game state, behaviors, levels, application code. |
| `src/render/` | Portable material and mesh descriptions; visibility and draw preparation. |
| `src/platform/dreamcast/main.cpp` | Entry point and explicit subsystem lifetime. |
| `src/platform/dreamcast/render_pvr.cpp` | PVR contexts, lists, vertex submission, frame synchronization. |
| `src/platform/dreamcast/memory.cpp` | Main-RAM arenas, VRAM ownership, aligned transfer buffers. |
| `src/platform/dreamcast/audio.cpp` | AICA sound effects and streaming. |
| `src/platform/dreamcast/input.cpp` | Maple device discovery and input normalization. |
| `src/platform/dreamcast/storage.cpp` | Asset roots, packs, read scheduling, VMU saves. |
| `tools/assetc/` | Host-side mesh, texture, animation, and audio conversion. |
| `assets/source/` | Editable authoring assets with provenance records. |
| `assets/cooked/dreamcast/` | Generated target-native assets. |
| `romdisk/` | Small embedded startup resources only. |
| `disc/assets/` | Staged disc assets and packs. |
| `build/dreamcast/` | ELF, map file, intermediate objects, generated manifests. |
| `release/` | Reviewed deliverables and notices. |

Use one asset-root resolver, for example choosing `/rd`, `/cd`, or an explicitly configured development mount. Keep device paths out of game logic. Normalize asset identifiers and case during conversion, and validate that all package references resolve before creating the release image.

### 4.3 Source-tree orientation

`include/kos/` exposes portable kernel services. `kernel/arch/dreamcast/include/dc/` exposes console devices, and `kernel/arch/dreamcast/include/arch/` exposes CPU/architecture services. `kernel/arch/dreamcast/hardware/` contains device implementations. `examples/dreamcast/` is the quickest path from an API to a working usage pattern. The detailed source index near the end maps particular systems to headers, implementations, and examples.

## 5. First executable and C++ policy

### 5.1 Minimal entry point

Use the upstream hello example as the first environment test. A small original application entry point can look like this:

```cpp
// src/main.cpp -- illustrative source-reviewed example.
#include <kos.h>
#include <cstdio>

KOS_INIT_FLAGS(INIT_DEFAULT);

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    std::puts("Dreamcast application started.");
    return 0;
}
```

This writes through the configured standard-output/debug path; it is not a graphical text renderer. Returning immediately can make it appear to do nothing if your loader has no visible log. `INIT_DEFAULT` is appropriate for a baseline; add only the subsystem flags you actually require. Do not treat `INIT_NONE` as a harmless optimization: it makes you responsible for initialization. Sources: [hello example](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/hello/hello.c), [initialization flags](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/init_flags.h).

Minimal project `Makefile` with an existing `src/main.cpp`:

```make
.DEFAULT_GOAL := all
TARGET := game.elf
OBJS := src/main.o
CXXFLAGS += -std=c++20 -Wall -Wextra

include $(KOS_BASE)/Makefile.rules

.PHONY: all run clean
all: $(TARGET)

$(TARGET): $(OBJS)
	kos-c++ -Wl,-Map,game.map -o $@ $(OBJS)

run: $(TARGET)
	$(KOS_LOADER) $(TARGET)

clean:
	rm -f $(TARGET) game.map $(OBJS)
```

Recipe indentation must use tabs. This intentionally small Makefile omits production header-dependency generation and asset cooking. Use the KOS compiler wrappers: they carry platform flags, startup/linker settings, and required libraries. Link C++ applications through `kos-c++`, even when some objects were compiled from C. Source: [KOS make rules](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/Makefile.rules), [C++ wrapper](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/build_wrappers/kos-c++).

CMake is another supported entry point after loading the KOS environment:

```bash
cmake -S . -B build/dreamcast \
    -DCMAKE_TOOLCHAIN_FILE="$KOS_CMAKE_TOOLCHAIN" \
    -DCMAKE_BUILD_TYPE=Release
cmake --build build/dreamcast
```

Your project still needs appropriate CMake targets and dependency configuration. Begin with the simpler make-based example before diagnosing an engine's complete CMake dependency graph. Source: [KOS CMake toolchain](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/cmake/kallistios.toolchain.cmake).

### 5.2 C++ is a reasonable default

Use C++ for ownership, interfaces, templates, tools shared with the host, and game logic. Use C-compatible structures at hardware and serialization boundaries. Modern language support is useful, but desktop assumptions about memory, locale, filesystem semantics, exceptions, or threading do not follow automatically from compiler support. KOS carries concrete C++ samples, including threads and filesystem operations; the supported subset depends on the selected toolchain. Sources: [C++ examples](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/cpp), [compiler configuration](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/kos-chain/Makefile.dreamcast.cfg).

Recommended policy, as an engineering choice:

- Prefer fixed-capacity containers or reserved vectors in frequently executed code.
- Allocate scene resources at load time; use reusable frame arenas for temporary draw data.
- Keep virtual dispatch at coarse subsystem boundaries, and measure it before removing it.
- Choose an explicit exception/RTTI policy per project. Disabling them is optional, not a Dreamcast requirement, and can break libraries that need them.
- Use `float`, `1.0f`, and single-precision math functions for renderer math. Retain real `double` where correctness requires it.
- Serialize fixed-width integers, specified endianness, and explicit offsets. Never dump compiler-dependent C++ object layouts or native pointers into asset files.

**ABI warning:** current KOS defaults to `-m4-single`, preserving normal 64-bit `double` support while entering functions in single-precision FPU mode. Historical `-m4-single-only` builds use a different floating-point contract; the KOS template explicitly requires rebuilding KOS and linked dependencies consistently when changing this choice. Do not mix libraries just because both filenames end in `.a`. Sources: [KOS environment ABI documentation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/doc/environ.sh.sample), [GNU SH options](https://gcc.gnu.org/onlinedocs/gcc/SH-Options.html).

## 6. CPU, memory, caches, and transfers

### 6.1 Useful SH-4 properties

The SH-4 has integer execution units and an FPU with special four-component inner-product and matrix-transform operations. `FIPR` and `FTRV` are useful to a renderer; the phrase “128-bit FPU” does not mean it implements a modern general-purpose SIMD instruction set. Read the public [ST/Hitachi SH-4 CPU core manual](https://www.st.com/resource/en/user_manual/cd00147165-sh-4-32-bit-cpu-core-architecture-stmicroelectronics.pdf) for instruction semantics, and the [Renesas hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware?r=1055166) for CPU peripherals and registers.

Prefer KOS's optimized math interfaces before writing assembly. `dc/matrix.h`, `dc/vector.h`, `dc/vec3f.h`, and `dc/fmath.h` expose matrix operations, vector math, and fast trigonometric/reciprocal functions. Matrix storage must satisfy the alignment requirements; some assembly paths require at least 8-byte alignment, with 32-byte alignment recommended for performance. Sources: [matrix API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/matrix.h), [fast math](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/fmath.h).

Fast approximations and `-ffast-math` can change numerical behavior. Apply them to measured hot code after checking acceptable error; do not globally change collision, serialization, or deterministic simulation semantics to improve an unrelated vertex loop. Handwritten assembly must preserve the calling convention and FPU/register-bank state expected by its caller.

### 6.2 Memory windows and aliases

These are orientation addresses for a normal Dreamcast, not an invitation to bypass allocators and drivers.

| Resource | Physical base | Common uncached/cached view | Meaning |
|---|---:|---:|---|
| Main RAM | `0x0c000000` | Cached `0x8c000000`, uncached `0xac000000` | Two views of the same 16 MiB. |
| VRAM, 64-bit aperture | `0x04000000` | `0xa4000000` | PVR memory access window. |
| VRAM, 32-bit aperture | `0x05000000` | `0xa5000000` | Alternate mapping of the same VRAM, with different access layout. |
| AICA wave RAM | `0x00800000` | `0xa0800000` | Sound RAM on the G2 side. |
| Store queues | CPU-internal P4 area | `0xe0000000`–`0xe3ffffff` | Address window controlling two 32-byte queues. |

P1 (`0x80000000`–`0x9fffffff`) is a cacheable direct mapping, while P2 (`0xa0000000`–`0xbfffffff`) is uncached. For applicable externally mapped addresses, the physical-address mask is `0x1fffffff`. Do not apply this as a universal translation rule for every CPU-internal register or active-MMU mapping. Sources: [KOS memory-region definitions](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/memory.h), [architecture memory definitions](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/memory.h), [PVR transfer apertures](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr/pvr_dma.h), [SPU interface](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/spu.h).

Cached and uncached pointers are not independent copies. A CPU write through a cached alias may remain invisible to DMA until written back. Conversely, a CPU cache line can hide a newer device write. VRAM's two apertures also have mapping semantics beyond the ordinary cached/uncached distinction; use KOS's transfer modes and returned PVR pointers.

### 6.3 Cache coherence and buffer ownership

KOS exposes write-back, invalidate, purge, and instruction-cache synchronization operations. **Invalidating a dirty cache line discards newer CPU data.** Buffers and ownership boundaries should therefore align to the 32-byte line size, including transfer tails. `volatile` does not flush caches or establish device completion. Source: [generic cache API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/include/kos/cache.h).

Use this conceptual protocol, adapting the maintenance step to the exact device API:

1. Allocate suitably aligned storage whose entire transfer region has a single owner.
2. Fill CPU-to-device data, then perform the documented map/write-back operation if the API does not already do so.
3. For a device-written destination, establish the required cache state before starting, preserving any dirty bytes that must survive.
4. Start the transfer and keep the buffer alive and untouched by conflicting accesses.
5. Observe transfer completion; perform any documented destination invalidation before cached CPU consumption.
6. Release or reuse the buffer only after all consumers, including later GPU rendering, are finished.

Avoid adding cache calls blindly around an API that already owns this process. When modifying executable code, use instruction-cache synchronization as well as data-cache handling. KOS's DMA mapping helpers describe the appropriate source/destination treatment in [the DMAC API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/dmac.h).

### 6.4 Store queues versus DMA

Store queues are CPU-fed burst writers. They let software assemble 32-byte transfers without ordinary destination-cache allocation. They are particularly useful for PVR command submission and copying preformatted data. The queues are globally shared hardware resources; use KOS's locking wrappers or follow its manual ownership protocol.

`sq_cpy()` takes a byte count divisible by 32, a 32-byte-aligned destination, and a source with the documented alignment. `sq_fast_cpy()` instead takes a **count of 32-byte blocks** and an already translated SQ destination. Confusing these units causes serious overwrite errors. Sources: [store-queue API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/sq.h), [SQ implementation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/sq.c).

The SH-4 has a four-channel DMAC, but KOS reserves channels and exposes device-specific transfer routes. Do not appropriate a channel because it appears idle. PVR transfers, Maple transfers, and G2/AICA traffic do not all share an interchangeable programming interface. Completion callbacks may run in interrupt context: keep them short and signal a thread for substantial processing. Sources: [DMAC implementation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/dmac.c), [PVR DMA API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr/pvr_dma.h), [G2 bus API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/g2bus.h).

Neither DMA nor store queues universally wins. Transfer size, source locality, destination bus, CPU overlap, and synchronization affect the result. Use the [KOS DMA speed test](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/basic/dma/speedtest/speedtest.c) as a starting experiment on your actual console.

### 6.5 Threads and allocation

KOS supplies native threads and C/POSIX/C++ threading interfaces. Its ordinary execution environment has one shared address space and one SH-4 core. Use threads to manage waiting and deadlines, for example audio/network service; use a bounded amount of CPU work per frame. A continuously runnable higher-priority thread can starve lower-priority work. Join or detach created threads so their resources can be reclaimed. Sources: [thread contracts](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/include/kos/thread.h), [scheduler implementation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/thread/thread.c).

Use explicit heaps/arenas for different lifetimes: permanent engine state, level resources, frame temporaries, and DMA buffers. Track both total free space and fragmentation. Avoid retaining an entire compressed file, decoded image, converted texture, and GPU copy longer than necessary. This lifetime planning often saves more memory than shrinking individual structures.

### 6.6 Startup, linker layout, interrupts, and timing

The usual KOS linker script defaults to a load offset of **`0x8c010000`**. Startup establishes the stack and CPU/FPU state, then enters the KOS initialization path before your `main`. The linker also arranges executable/data sections, zero-initialized storage, and runtime metadata. Do not substitute a generic bare-metal linker script without accounting for these contracts. Sources: [linker script](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/ldscripts/shlelf.xc), [startup assembly](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/kernel/startup.S), [runtime initialization](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/kernel/init.c).

The startup source specifically notes that it is copied into the toolchain during patching: editing it in a checkout does not automatically change an already installed compiler's startup object. This is another reason to record the toolchain build rather than only the game commit.

Let KOS own interrupt vectors, dispatch, and device registrations. A custom interrupt callback must respect interrupt-context restrictions and the driver that registered it. Use high-resolution timers for profiling and elapsed-time simulation; use VBlank events for presentation-related work, not as a place to run a whole frame's logic. References: [IRQ interface](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/irq.h), [timer API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/timer.h), [VBlank API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/vblank.h).

## 7. PowerVR2 rendering model

### 7.1 What the CPU and GPU each do

The native PVR API receives screen-space geometry. It does not supply a camera, model/view/projection transformation, skeletal animation, or general lighting pipeline. Your code or a higher-level library performs those on the SH-4. PowerVR2 provides fixed-function rasterization, depth/visibility processing, texture sampling, shading/blending modes, and specialized effects. Source: [native PVR interface](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr.h).

PowerVR2 divides the viewport into **32 × 32 pixel tiles**. The Tile Accelerator accepts primitives, records their parameters, and bins references into affected tiles. The ISP/TSP rendering stages subsequently resolve and shade tile contents, then write finished image data to VRAM. Separate registration/render buffers allow work on adjacent frames to overlap. Hidden opaque coverage can avoid texture/shading work, but geometry transformation, submission, parameter storage, and binning still cost resources. Sources: [Marcus Comstedt's original PowerVR research](https://mc.pp.se/dc/pvr.html), [Imagination's Dreamcast architecture retrospective](https://blog.imaginationtech.com/celebrating-the-20th-anniversary-of-dreamcast-and-powervr/).

A large polygon can touch many tiles, while many small polygons can pressure parameter storage and submission. Therefore “triangle count” alone is a poor model of Dreamcast rendering cost.

### 7.2 The five hardware lists

| KOS list | Purpose |
|---|---|
| `PVR_LIST_OP_POLY` | Opaque polygons. |
| `PVR_LIST_OP_MOD` | Modifier volumes associated with opaque geometry. |
| `PVR_LIST_TR_POLY` | Translucent/blended polygons. |
| `PVR_LIST_TR_MOD` | Modifier volumes associated with translucent geometry. |
| `PVR_LIST_PT_POLY` | Punch-through polygons, useful for binary alpha cutouts. |

Enable only needed lists in `pvr_init_params_t.opb_sizes`; zero disables a list. In ordinary non-DMA submission, each list's data is contiguous and a closed list cannot simply be reopened later in the scene. Headers must identify the correct list, and every strip must end with `PVR_CMD_VERTEX_EOL`. KOS handles enabled empty-list termination internally. Source: [scene implementation and contracts](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/pvr/pvr_scene.c).

The default parameters enable opaque and translucent polygon lists, **not punch-through**. A cutout-material feature must also configure its list and alpha threshold policy. Default per-tile bin sizes are 16 words for those two lists, and the default vertex buffer is 512 KiB. Additional TA buffer sets and overflow allocation consume VRAM; inspect the total allocation rather than treating the `vertex_buf_size` field as the entire renderer cost. Sources: [default parameters](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr.h), [buffer allocation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/pvr/pvr_buffers.c).

### 7.3 Projection and clipping

A useful convention is to define positive camera-space depth `d` for points in front of the camera, then compute `q = 1 / d` and derive screen X/Y from projected coordinates. Native PVR vertex Z commonly stores this reciprocal depth. KOS default contexts use `PVR_DEPTHCMP_GREATER`, so a nearer positive `q` wins. Different camera handedness or projection conventions must be converted deliberately. Sources: [context defaults](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/pvr/pvr_prim.c), [reciprocal-depth clipping sample](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/modifier_volume_zclip/pvr_zclip.h).

Clip polygons crossing the near plane **before dividing by depth**. Interpolate positions and vertex attributes at new intersections, regenerate valid strips/triangles, and project the result. Reject geometry entirely behind the camera or outside coarse bounds before expensive work. Simply clamping a negative depth creates stretched triangles; simply dropping any triangle with one vertex behind the plane creates holes. Make winding, UV orientation, depth, and clipping conventions part of your renderer's written interface.

Hardware user clipping operates in tile coordinates. It is useful for coarse rectangles but is not a general pixel-exact scissor or near-plane clipper. If exact UI clipping is required, implement appropriate geometry clipping or use a library feature whose supported primitive types you have verified. Source: [PVR header/user-clip definitions](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr/pvr_header.h).

### 7.4 Transparency and effects

KOS enables translucent autosorting by default; `autosort_disabled` selects a presorted approach, where your ordering becomes important for conventional blending. Autosort is a major hardware feature, but it does not make every blend equation, depth policy, intersecting surface, and multi-pass dependency equivalent. Build a visual test scene with overlapping quads, cutouts, additive particles, and intersecting transparent surfaces. Sources: [PVR initialization options](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr.h), [PVR miscellaneous state API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr/pvr_misc.h).

Use opaque materials wherever possible. Choose punch-through for foliage, fences, and sprites with binary coverage; use blended materials for actual partial transparency. A translucent full-screen effect can be expensive despite consisting of only two triangles. Modifier volumes and bump maps offer specialized effects, while fog can reduce the visible range and blend distant geometry into a background. Study the KOS samples rather than modeling these features as general programmable shaders: [modifier volumes](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/modifier_volume), [fog implementation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/pvr/pvr_fog.c).

### 7.5 Submission choices

| Method | Use | Important constraint |
|---|---|---|
| `pvr_prim()` | Simple initial renderer and general header/vertex submission | Follow 32-byte alignment and packet-size contracts. |
| `pvr_dr_target()` / `pvr_dr_commit()` | Populate direct store-queue targets in a measured hot loop | Targets are write-only; coordinate store-queue ownership. |
| Vertex DMA with `pvr_set_vertbuf()` | Prepare list data in main RAM and transfer it | Extra RAM/buffering, alignment, capacity, and lifecycle requirements. |
| `pvr_txr_load_dma()` | Transfer preformatted texture payload | Separate from geometry submission; wait before reusing its source. |

The public vertex-buffer contract requires aligned storage, a size divisible by 64, and a buffer for every enabled list when using that mode; buffers are split internally. Do not depend on incidental hybrid behavior observed in implementation code. `pvr_list_flush()` is documented as unimplemented at this revision. Sources: [direct submission API](https://kos-docs.dreamcast.wiki/group__pvr__direct.html), [PVR API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr.h), [direct-strip example](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/pvrmark_strips_direct/pvrmark_strips_direct.c).

### 7.6 Render to texture

Current KOS has **`pvr_scene_begin_rtt(target, render_width, render_height, stride_pixels)`**, with a sized 16-bit target, replacing assumptions behind the deprecated screen-sized `pvr_scene_begin_txr()` path. Finish the RTT scene, then sample its result in an appropriately ordered pass. Keep target allocation, pixel format, stride, and texture-header interpretation consistent. A 128 × 128 16-bit target needs 32 KiB of pixel storage before any alignment/metadata overhead.

The current sample deliberately avoids a redundant CPU wait between GPU-ordered passes. It does use readiness/render-completion waits before CPU access, freeing, or reallocating in-flight storage. Follow that sample's ownership rules; do not insert global GPU waits between every pass or remove the normal scene-readiness boundary. Source: [sized RTT sample](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/pvr_rtt_sized/pvr_rtt_sized.cc).

### 7.7 GLdc as an alternative

GLdc is a partial **fixed-function OpenGL 1.2** implementation over KOS, licensed under **BSD 2-Clause**. It can reduce porting effort for an existing compatible renderer and performs software transform/clipping and PVR state translation. Let `glKosInit()` initialize the graphics system; do not separately initialize PVR underneath it. Sources: [GLdc README](https://gitlab.com/simulant/GLdc/-/blob/aff72dc35d06a6eaed6d728547d728c897afe3c9/README.md), [GLdc license](https://gitlab.com/simulant/GLdc/-/blob/aff72dc35d06a6eaed6d728547d728c897afe3c9/LICENSE).

The inspected GLdc revision is `aff72dc35d06a6eaed6d728547d728c897afe3c9`. Its configuration defaults autosort **off**, unlike native KOS defaults. Its `glFlush()` and `glFinish()` bodies are empty; use the documented GLdc frame/RTT lifecycle instead of importing desktop synchronization assumptions. Sources: [GLdc configuration](https://gitlab.com/simulant/GLdc/-/blob/aff72dc35d06a6eaed6d728547d728c897afe3c9/include/GL/glkos.h), [submission implementation](https://gitlab.com/simulant/GLdc/-/blob/aff72dc35d06a6eaed6d728547d728c897afe3c9/GL/flush.c).

Current source contains EXT-style framebuffer-object support and samples even though its README still lists FBOs as unfinished. This evolving support is constrained to particular attachment/target/level combinations and a 16-bit backing path; it is not general desktop-FBO compatibility. Its NPOT extension also has restrictions on width, wrapping, mipmaps, palettes, and compression. Check source plus samples at the same revision. Sources: [FBO implementation](https://gitlab.com/simulant/GLdc/-/blob/aff72dc35d06a6eaed6d728547d728c897afe3c9/GL/framebuffer.c), [texture implementation](https://gitlab.com/simulant/GLdc/-/blob/aff72dc35d06a6eaed6d728547d728c897afe3c9/GL/texture.c).

Choose native PVR for explicit platform control in a new backend. Choose GLdc when compatible existing OpenGL code meaningfully reduces development work. In either case, the underlying geometry and memory limits remain.

## 8. A practical graphics-engine design

The following architecture is a **proposed engine design**, derived from the hardware constraints. It is not an existing KOS engine or a claim about how every commercial Dreamcast game was implemented.

### 8.1 Split the renderer into four responsibilities

| Layer | Work | Output |
|---|---|---|
| Visibility | Scene traversal, frustum tests, portals or spatial cells, LOD | Visible objects and mesh sections. |
| Geometry preparation | Animation, transforms, lighting, near clipping, projection | Screen-space vertices and attributes. |
| Material scheduling | Classify opaque/cutout/blended; select texture/palette and state | Batches with compiled PVR headers. |
| Submission and lifetime | Emit strips, transfer data, finish lists/scenes, retire resources | Work owned by the TA/PVR until its completion boundary. |

Keep one clear owner of GPU submission, store queues, and frame transitions. Worker threads may prepare data when worthwhile, but the SH-4 is one CPU core: adding threads does not create parallel CPU arithmetic throughput. An initial single render thread is easier to verify.

### 8.2 Frame sequence

1. Read current input and advance simulation on a defined time step.
2. Determine visible objects and choose LODs before doing expensive per-vertex work.
3. Prepare material batches and CPU geometry. Keep unprojected attributes for primitives that need clipping.
4. Follow the synchronization sequence for the chosen submission mode. Direct submission and RAM-buffered vertex DMA wait at different points; do not force an early global wait that defeats DMA preparation overlap.
5. Open the scene, emit each list you actually use with valid headers and strip termination, then finish it. KOS supplies termination for enabled but unused lists at scene finish.
6. Submit/finish the scene and continue independent CPU work where the chosen KOS rendering mode allows overlap.
7. Reuse staging memory and retire textures only when their specific consumers have finished.

Do not infer completion from a function whose purpose is only to enqueue work. In particular, completion of a texture DMA copy, completion of TA input, and completion of rendering that samples that texture are different events. Consult the [PVR scene API](https://kos-docs.dreamcast.wiki/pvr_8h.html) and [DMA contracts](https://kos-docs.dreamcast.wiki/pvr__dma_8h.html).

### 8.3 Small native rendering example

This example draws an untextured triangle in screen coordinates and exits on Start. It demonstrates the lifecycle only: no camera, clipping, assets, or engine abstractions. It was checked against KOS declarations and usage patterns, but has not been cross-compiled or run on a console here.

```cpp
#include <kos.h>
#include <dc/maple.h>
#include <dc/maple/controller.h>
#include <dc/pvr.h>
#include <dc/video.h>

KOS_INIT_FLAGS(INIT_DEFAULT);

static bool start_pressed() {
    maple_device_t* dev = maple_enum_type(0, MAPLE_FUNC_CONTROLLER);
    if(!dev) return false;
    const auto* state = static_cast<const cont_state_t*>(maple_dev_status(dev));
    return state && (state->buttons & CONT_START);
}

int main() {
    vid_set_mode(DM_640x480, PM_RGB565);
    if(pvr_init_defaults() < 0) return 1;
    pvr_set_bg_color(0.04f, 0.02f, 0.08f);

    pvr_poly_cxt_t context;
    pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY);
    context.gen.culling = PVR_CULLING_NONE;
    pvr_poly_hdr_t header;
    pvr_poly_compile(&header, &context);

    while(!start_pressed()) {
        pvr_wait_ready();
        pvr_scene_begin();
        pvr_list_begin(PVR_LIST_OP_POLY);
        pvr_prim(&header, sizeof(header));

        pvr_vertex_t vertex{};
        vertex.flags = PVR_CMD_VERTEX;
        vertex.z = 1.0f;
        vertex.argb = 0xffbf80ff;

        vertex.x = 320.0f; vertex.y = 100.0f;
        pvr_prim(&vertex, sizeof(vertex));
        vertex.x = 160.0f; vertex.y = 360.0f;
        pvr_prim(&vertex, sizeof(vertex));
        vertex.flags = PVR_CMD_VERTEX_EOL;
        vertex.x = 480.0f; vertex.y = 360.0f;
        pvr_prim(&vertex, sizeof(vertex));

        pvr_list_finish();
        pvr_scene_finish();
    }

    pvr_wait_ready();
    pvr_wait_render_done();
    pvr_shutdown();
    return 0;
}
```

The constant `z` here is acceptable because it is a flat screen-space demonstration. A perspective renderer must use the appropriate reciprocal-depth convention, not simply paste its engine's clip-space depth into this field. Video mode selection must also match the supported cable/display modes. Relevant primary examples: [PVR benchmark](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/pvrmark/pvrmark.c), [video API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/video.h).

### 8.4 Material representation

Precompute a material key from list type, texture identity/format, palette bank, filter mode, blending, depth comparison/write state, culling, and shading mode. Build a compatible `pvr_poly_hdr_t` when this state changes, then reuse it while its values remain valid. A cached header containing a texture address becomes stale if the texture moves.

Do not sort every primitive by one global key. Opaque batches can favor state reuse. Punch-through requires its list and alpha-test configuration. Blended geometry must honor the renderer's chosen sorting policy and effect-specific dependencies. Palette changes, render targets, and modifier-volume state may impose additional ordering boundaries.

### 8.5 Geometry, lighting, and animation

Use triangle strips where they reduce emitted data without creating excessive duplication or clipping complexity. Keep an indexed mesh representation for host processing and reusable CPU transforms, then generate a PVR-friendly stream. KOS's raw TA submission is not a modern indexed draw call that automatically provides a desktop-style post-transform vertex cache.

For static worlds, evaluate rooms/portals, a grid, BVH, or another scene-appropriate visibility structure. For animated characters, budget skinning on the SH-4; cap influences and bone work in the converter, and consider baked or lower-rate animation for distant objects. These are choices to profile, not universal numeric limits.

Begin with baked vertex lighting or a small number of per-vertex lights. Lightmaps, environment effects, or multiple material passes add submission and raster work; compare their cost with vertex colors and texture baking. PowerVR bump mapping is specialized fixed-function behavior, not permission to port a tangent-space PBR shader unchanged.

### 8.6 Portability boundary

Expose capabilities such as opaque, cutout, blended, vertex-lit, fogged, and optional extra-pass materials. Let each platform implement those explicitly. Keep a Dreamcast-specific cooked mesh/texture format even if authoring uses glTF or a desktop engine exporter. A portable material system should report unsupported combinations at build time, rather than silently approximate them during gameplay.

## 9. Textures and the asset pipeline

### 9.1 Choose representation per asset

| Representation | Typical use | Tradeoff |
|---|---|---|
| RGB565 | Opaque color textures | No alpha, more green precision. |
| ARGB1555 | Binary coverage/cutout textures | One alpha bit and five bits per color channel. |
| ARGB4444 | Smooth alpha, effects | Four bits per channel; inspect banding. |
| 4-bit palette indices | Small palettes, stylized art | 16 colors per selected bank; global palette management. |
| 8-bit palette indices | Larger indexed-color art | 256 colors per selected bank; palette storage/state. |
| YUV422 | Certain image/video paths | Distinct conversion/filtering behavior; measure the actual use case. |
| PVR bump format | Specialized bump effects | Requires appropriate conversion and rendering setup. |

Twiddling, compression, mipmapping, and pixel format are **different properties**. A file extension alone does not specify all of them. Keep them explicit in the cooked asset header and compiled PVR material. Definitions: [texture format constants](https://kos-docs.dreamcast.wiki/group__pvr__txr__fmts.html).

### 9.2 Twiddling and dimensions

Twiddling reorders texels for two-dimensional sampling locality; it does not reduce the number of bits stored. Convert static textures offline. KOS can twiddle at load time with `pvr_txr_load_ex()`, but this adds CPU work and intermediate-data requirements. `PVR_TXRFMT_TWIDDLED` is encoded as zero, so specifying RGB565 without a non-twiddled flag already implies a twiddled layout. Uploading linear pixels under that description produces corruption. Sources: [texture API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr/pvr_txr.h), [upload implementation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/pvr/pvr_texture.c).

Ordinary texture paths use power-of-two dimensions from 8 through 1024; rectangular non-mipmapped textures are possible. Supported mipmapped converter paths impose additional square/twiddled layout requirements. Strided linear textures are a special case, useful for dynamic images or particular NPOT dimensions, rather than a reason to ignore normal texture constraints.

For the native x32-stride API, the nonzero stride is a multiple of 32 with a documented maximum of **992 pixels**. Its register is global, so simultaneously used strided textures must be compatible with that state. This is distinct from a normal 1024-wide texture. The converter can describe more layouts than a high-level library exposes; validate the full converter–loader–renderer chain. Source: [640 × 480 strided-texture example](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/strided_texture/strided_texture.c).

### 9.3 VQ compression and size calculations

Classic full-codebook 16-bit VQ represents each 2 × 2 texel block with an index into a **2048-byte codebook**. For a non-mipmapped image with dimensions divisible by two:

`classic VQ bytes = 2048 + width × height / 4`

| Texture | Uncompressed 16-bit texels | Classic full-codebook VQ |
|---|---:|---:|
| 64 × 64 | 8,192 bytes | 3,072 bytes |
| 128 × 128 | 32,768 bytes | 6,144 bytes |
| 256 × 256 | 131,072 bytes | 18,432 bytes |
| 512 × 512 | 524,288 bytes | 67,584 bytes |

The table excludes file headers, mip levels, alignment, and specialized small-codebook layouts. The often-quoted 8:1 reduction is asymptotic against 16-bit texels; small textures pay significant codebook overhead. Compression is lossy. Compare visible quality, alpha edges, and repeated patterns before adopting it. Source: [KOS VQ encoder manual](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/vqenc/vqenc.1).

### 9.4 Palettes and mipmaps

Palette RAM contains 1024 addressable entries with a shared entry-format setting. Selector sizes imply four 256-entry banks or sixty-four 16-entry banks; those views share the same storage. Changing an entry while queued geometry expects its previous value is a resource-lifetime problem. KOS warns that filtering paletted textures with ARGB8888 palette entries can substantially reduce rendering speed; prefer 16-bit palette entries when their quality is sufficient. Source: [palette API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr/pvr_pal.h).

A complete uncompressed square mip chain adds approximately one third to the largest level's texel storage. It can improve distant surfaces and sampling locality, but the PVR-specific layout, level offsets, bias, and filter settings must agree. KOS exposes two trilinear pass modes; do not infer a modern single-call trilinear implementation from their names. Use a tested path and verify the result on hardware. Sources: [texture context/filter definitions](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr.h), [GLdc filter handling](https://gitlab.com/simulant/GLdc/-/blob/aff72dc35d06a6eaed6d728547d728c897afe3c9/GL/state.c).

### 9.5 Host-side texture converter

KOS includes **`utils/pvrtex`**, which supports twiddling, VQ including smaller codebooks, palettes, mipmap generation, dithering, and preview output. Its `.dt` output aligns texture data for transfer. A `.dt` file is a container: parse its metadata and upload the payload, not the whole file as if every byte were texture data.

Illustrative commands for already valid input dimensions:

```bash
pvrtex -i wall.png -o wall.dt -f rgb565 -c -m
pvrtex -i smoke.png -o smoke.dt -f argb4444 -d
pvrtex -i interface.png -o interface.dt -f pal8bpp -p interface-preview.png
```

The converter's documentation identifies specific artifact-prone combinations involving filtered paletted, compressed, mipmapped textures. Treat those as combination-specific behavior rather than claiming all paletted VQ or mipmapping is unsupported. Its overall licensing is **LGPL due to included FFmpeg-derived code**. Use it as a host converter; inspect and fulfill redistribution requirements if packaging it with your own tools. Sources: [pvrtex instructions and license statement](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/pvrtex/readme_unformatted.txt), [FFmpeg-derived source notice](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/pvrtex/elbg.c), [DT format implementation](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/pvrtex/file_dctex.h).

### 9.6 Proposed complete asset build

1. **Import on the host.** Read meshes, material definitions, images, animation, and audio from their authoring formats.
2. **Validate.** Reject missing assets, unsupported material combinations, invalid dimensions, oversized skeletons, and unresolved licenses.
3. **Optimize meshes.** Partition by visibility/material, generate LODs, choose strips, remove unused attributes, and precompute bounds.
4. **Convert textures.** Select pixel format, layout, mipmaps, palette bank requirements, and compression; retain a preview.
5. **Convert animation/audio.** Quantize only where error is acceptable; precompute track lookup data and choose supported audio encodings.
6. **Write target packages.** Use explicit little-endian fields, offsets, sizes, checksums, version identifiers, and alignment.
7. **Report budgets.** Output per-scene RAM, VRAM, sample RAM, emitted geometry estimates, and package size.

Keep host formats rich and runtime formats simple. Parsing a complex interchange format or decoding every PNG on the SH-4 is useful during prototyping, but it should be a deliberate release-time cost. Avoid atlas choices that create wasted texture residency or excessive material splits; optimize for what appears together in a scene.

## 10. Memory and performance budgets

### 10.1 Frame time before polygon counts

At a nominal 60 Hz, a frame is about **16.67 ms**; at 30 Hz it is **33.33 ms**. A 200 MHz CPU has approximately **3.33 million** or **6.67 million clock cycles**, respectively. Those are arithmetic envelopes, not usable instruction counts: stalls, interrupts, waits, memory traffic, and all game systems consume them.

Treat steady-state frame throughput as constrained by the longest effective stage after overlap and synchronization. CPU simulation, geometry preparation, TA transfers, rendering, scanout pacing, storage, and audio service can overlap in some combinations and contend in others. Adding all independently measured stage times is not always the actual frame time; ignoring their shared bus and buffer dependencies is equally misleading.

Suggested telemetry:

| Counter | What it explains |
|---|---|
| Simulation and visibility time | Gameplay/scene traversal cost. |
| Transform, lighting, skinning, clipping time | SH-4 geometry cost. |
| Submission time and emitted bytes | CPU/TA front-end cost and strip efficiency. |
| PVR readiness wait and frame interval | Backpressure and missed presentation opportunities. |
| Texture DMA bytes and wait time | Dynamic upload pressure. |
| Main-RAM and VRAM high-water usage | Memory exhaustion risk. |
| Audio underflows and refill deadlines | Whether frame work starves sound. |
| File-read latency distribution | Streaming readiness and seeks. |

KOS provides `pvr_get_stats()` and CPU profiling examples. Use these alongside your own timers; labels such as “render time” must match the measurement boundary rather than being inferred from total FPS. See [PVR statistics API](https://kos-docs.dreamcast.wiki/pvr_8h.html), [gprof sample](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/profiling/gprof).

### 10.2 Example memory envelope

This deliberately conservative starting allocation is **illustrative**. Measure actual KOS overhead, stack usage, buffering, and assets before adopting it.

| Main RAM use | Example reservation |
|---|---:|
| Executable, globals, KOS, stacks | 3 MiB |
| World, collision, game state | 3 MiB |
| Meshes and animation | 3 MiB |
| Render preparation and staging | 2 MiB |
| File/audio streaming buffers | 2 MiB |
| Working heap and contingency | 3 MiB |
| **Total main RAM** | **16 MiB** |

Do not assume the whole top row is available to game code, or that this table guarantees the loader and runtime fit. Inspect the link map and allocator high-water marks; leave additional room if required by the chosen execution path.

| Video RAM use | Example reservation |
|---|---:|
| Two 640 × 480 RGB565 framebuffers | 1,228,800 bytes, about 1.172 MiB |
| TA parameter/object/tile-related buffers | 2 MiB provisional |
| Resident textures | 3.5 MiB provisional |
| Render-target/allocator reserve | About 1.328 MiB |
| **Total video RAM** | **8 MiB** |

The framebuffer calculation is exact for two packed images of that format; actual setup alignment and extra buffers can add overhead. TA allocations depend on KOS initialization and list/bin settings; do not take 2 MiB as a hardware-defined requirement. Audio RAM is a **separate 2 MiB pool**, shared by the sound driver, effects, and streaming data. It is not spare general-purpose texture memory.

### 10.3 Bandwidth arithmetic that informs design

For illustration, 10,000 emitted vertices at 32 bytes each produce **320,000 bytes per frame**, before headers and any other parameter data. At 60 frames/s that is **19.2 MB/s** of emitted vertex payload. A long strip of `n` vertices describes `n − 2` triangles, but real meshes need additional strips, duplicated seams, restarts, and clipping. Count emitted bytes rather than estimating them solely from source triangle counts.

A full 640 × 480 RGB565 upload is **614,400 bytes**; performing it every nominal 60 Hz frame represents **36.864 MB/s** of payload before contention or conversion. Avoid turning a normally resident texture into a full-frame upload by accident. These are computed examples, not measured achievable bandwidth.

### 10.4 Optimization order

1. **Cull before transforming.** Remove invisible rooms, objects, mesh sections, and unnecessary LOD detail.
2. **Reduce expensive CPU work.** Limit skinning and lighting, reuse transforms, remove avoidable allocation, and improve locality.
3. **Reduce emitted geometry and headers.** Compare strip layouts and material batching with realistic scenes.
4. **Prepare textures offline.** Use appropriate formats, mipmaps, and compression; keep commonly used assets resident.
5. **Reduce costly pixels.** Cut excess blended overdraw, full-screen effects, and unnecessary extra passes.
6. **Tune transfers and buffering.** Compare PVR submission methods once the simple path is correct.
7. **Tune compiler flags and assembly locally.** Inspect generated code and benchmark the actual hot loops.

Test a lower resolution to distinguish some pixel-heavy workloads from geometry-heavy ones, then corroborate with counters. An unchanged frame time is evidence against a simple fill-rate bottleneck, not proof that every GPU stage is idle. Similarly, replacing textures or disabling transparency is an experiment, not an optimization to ship blindly.

### 10.5 Video output and UI

Plan for VGA and television output explicitly. Refresh rate, interlace, aspect ratio, overscan, and cable compatibility change what the player sees. Use a mode policy, an adjustable safe area, legible small text, and an appropriate simulation clock. A hardcoded “60 frames equals one second” fails when the chosen presentation mode or workload runs at a different rate. Select modes through the [KOS video API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/video.h); test intended cables and displays on hardware.

## 11. Audio and the AICA

### 11.1 Use the existing sound driver first

AICA offers 64 hardware channels and 2 MiB of sound RAM. KOS's standard driver runs on the audio ARM7 and communicates with the SH-4 through defined queues. A prebuilt driver is included, so an `arm-eabi` compiler is optional until you modify that firmware. `snd_init()` establishes this sound environment; replacing the ARM program invalidates high-level assumptions unless you reproduce its protocol. Sources: [sound interface](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/sound/sound.h), [communication protocol](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/sound/aica_comm.h), [ARM driver source](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/sound/arm).

Avoid treating a frequently repeated AICA/ARM clock figure as available general-purpose CPU throughput. MAME models the AICA device clock at 22.5792 MHz with the ARM receiving one cycle in eight, approximately 2.8224 MHz. That is an implementation model, not a benchmark of custom firmware. Keep ARM-side work small and deterministic and let the audio hardware perform mixing. Source: [MAME's Dreamcast configuration](https://github.com/mamedev/mame/blob/1a8267fa579f62cee37c65e82aabc09b328a1eb6/src/mame/sega/dccons.cpp).

### 11.2 Effects, music, and sample memory

| Requirement | KOS starting point | Design advice |
|---|---|---|
| Short resident effects | `snd_sfx_load*()`, `snd_sfx_play*()` | Preload frequently used effects; unload only after playback no longer uses them. |
| Streamed music | `snd_stream_*()` | Refill ahead of playback and service polling regularly. |
| Custom sample storage | `snd_mem_malloc()` / `snd_mem_free()` | These return sound-RAM offsets, not ordinary main-RAM pointers. |
| Large uploads | `spu_dma_transfer()` / `spu_memload_dma()` | Respect 32-byte alignment, length, cache, and completion rules. |
| Custom ARM firmware | `kernel/arch/dreamcast/sound/arm/` | Own the communication contract and build a separate ARM target. |

AICA supports PCM and Yamaha ADPCM formats through KOS. An arbitrary WAV or ADPCM file is not automatically in a supported format. Stereo effects consume a channel pair, and the sound-effect API has length limits; use streaming for long audio. Sources: [effect manager](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/sound/sfxmgr.h), [stream API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/sound/stream.h), [SPU transfers](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/spu.h).

Decoded 44.1 kHz stereo PCM16 uses **176,400 bytes per second**. A quarter-second of such audio is 44,100 bytes before padding; this calculation helps set buffering headroom. Compression saves storage and I/O, but decoding can move work onto the SH-4. Evaluate decoder licenses and worst-case CPU time before selecting a codec.

Do not block the audio refill path on an unpredictable optical seek, network request, or large allocation. Use a file-read buffer ahead of the decode/output buffer, measure underflows, and test while rendering the most demanding scene. CDDA playback is another supported optical path, but disc layout and interactions with data access deserve explicit testing.

### 11.3 Shared-bus discipline

AICA is accessed through G2. Its write FIFO and bus timing make raw register or wave-RAM loops hazardous when mixed with active sound APIs. KOS's SPU/G2 functions handle that discipline. Interrupt completion should notify a worker; audio file decoding and response processing should not be performed inside an arbitrary hardware interrupt. Reference examples: [sound effects](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/sound/sfx), [multiple streams](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/sound/multi-stream).

## 12. Controllers, VMU, and peripherals

### 12.1 Maple enumeration and input handling

Use `maple_enum_type()` to find devices by function, or `maple_enum_dev()` for a particular port/unit. The first matching controller is not necessarily physically plugged into port A. Check both the returned device and `maple_dev_status()` before reading state, and re-enumerate appropriately after unplug/replug. Sources: [Maple bus API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/maple.h), [controller API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/maple/controller.h).

KOS exposes pressed buttons as set bits, centered signed stick values from −128 to 127, and trigger values from 0 to 255. Apply dead zones and convert these to your engine's input range once. Track previous button state to compute press/release edges, rather than retriggering actions every frame a button is held. Treat extended controller fields as capability-dependent.

Maple also supports keyboard, mouse, light gun, rumble, microphone, camera, and other accessories through separate drivers. Enumerate capabilities and provide a fallback when an optional device is absent. A broad reference is the [Maple example](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/maple/maple.c).

### 12.2 VMU save data

VMU filesystem paths identify controller port and subunit, such as `/vmu/a1`. A normal card exposes 200 user blocks of 512 bytes, or **102,400 bytes**, before your own file/package overhead. VMUs do not provide arbitrary nested directories. Use ordinary VMU filesystem/package APIs rather than writing raw blocks for game saves. Sources: [VMU filesystem](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/vmufs.h), [VMU package format](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/vmu_pkg.h).

A save package should contain recognizable metadata/icons plus a versioned game payload. Keep sizes explicit, validate the package and payload before loading, and plan migrations between game versions. Handle missing/full cards, removal during access, corrupt data, and write errors. If using two generations for recovery, budget their blocks and test how interruption affects each operation; do not assume PC-style atomic rename semantics.

The LCD, clock, beep, and VMU buttons have separate APIs. Keep LCD updates modest and avoid assuming the VMU is always attached or battery-powered. The independent VMU game mode is a separate target/program format, not another SH-4 process. Starting points: [VMU hardware API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/maple/vmu.h), [save-package example](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/vmu/vmu_pkg/vmu.c), [VMU examples](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/vmu).

## 13. Filesystems, disc access, and networking

### 13.1 Runtime mount points

| Path | Contents | Availability and limits |
|---|---|---|
| `/cd` | ISO9660 files on the optical or emulated disc | Requires initialized CD support and an actual appropriate disc/image; read-only. |
| `/rd` | Embedded ROMFS image | Requires a linked/mounted image; consumes main RAM as part of the application. |
| `/pc` | Files exposed by the attached dc-tool host | Development feature; absent from a normal standalone release. |
| `/vmu/a1` | VMU on port A, subunit 1 | Depends on a present device; flat filesystem and small block budget. |
| `/sd` | Your chosen SD filesystem mount | Conventional name only; initialize the driver/block device/filesystem and mount it explicitly. |

Source implementations: [ISO9660](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/fs/fs_iso9660.c), [ROMFS](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/include/kos/fs_romdisk.h), [host filesystem](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/fs/fs_dcload.c), [VMU filesystem](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/fs/fs_vmu.c), [explicit SD/FAT mounting example](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/network/lftpd/lftpd.c).

For a small embedded asset folder, define `KOS_ROMDISK_DIR = romdisk` and link `romdisk.o` using the matching KOS rules. The image must actually be part of the executable; defining a path variable alone does not embed the data. The current rules provide ROMFS construction and initialization support. Regenerate the image when its contents change; production builds should track those dependencies. Reference: [KOS ROM-disk rules](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/Makefile.rules).

### 13.2 Optical access and asset packs

Start with normal file I/O through KOS. Low-level `cdrom_*` APIs expose sector reads, disc information, streaming, and CDDA. Follow their alignment and cache rules for DMA destinations; do not mix manual controller access with a live high-level request. Reference: [CD-ROM API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/cdrom.h), [CD streaming example](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/cdrom/stream/cd-stream-test.c).

For a release engine, pack related assets together and favor predictable sequential reads. Maintain a package index in memory rather than repeatedly scanning directories. Design asynchronous load states so gameplay and audio do not pause for arbitrary reads. A pack index can include asset ID, offset, compressed size, decoded size, alignment, and checksum. Bound decoded size before allocating from untrusted or damaged data.

An optical drive, an ODE, host-mounted files, and serial SD have different latency and throughput. Measure each supported release path. Do not size streaming buffers from a desktop file server benchmark.

### 13.3 SD and ATA

The serial SD driver supplies block I/O, not a filesystem automatically. Its partition helper recognizes MBR partitions; keep the `kos_blockdev_t` alive while the filesystem uses it. `/sd` is your mountpoint decision. Source: [SD driver API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/sd.h).

G1 ATA modifications are another hardware-specific option. KOS exposes PIO/DMA and block-device interfaces; this is not the same as a serial SD adapter or an optical-drive emulator. Follow bus arbitration and the documented device arrangement rather than assuming PC IDE code will work directly. Source: [G1 ATA interface](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/g1ata.h).

### 13.4 Network programming

For ordinary KOS network initialization, use `KOS_INIT_FLAGS(INIT_DEFAULT | INIT_NET)`. Networking is not implied by the ordinary default flag set. KOS provides socket interfaces and an IPv4/IPv6 stack; use `socket`, `connect`, `send`, `recv`, and related APIs instead of binding game code to a particular adapter driver. Sources: [initialization definitions](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/include/kos/init.h), [network API](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/include/kos/net.h), [network implementation directory](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/net).

Use nonblocking operations or a service thread for DNS, connection setup, and remote I/O. Bound message sizes, handle disconnections, and keep simulation independent of unpredictable packet arrival. If using dcload-IP simultaneously, respect KOS's integration with its transport; taking over network registers can interrupt debugging. Start from the [basic networking sample](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/network/basic/basic.c).

## 14. Booting and deploying builds

### 14.1 Choose an iteration path

| Method | What you load | Advantages | What it does not establish |
|---|---|---|---|
| Flycast direct ELF with HLE | `game.elf` | Fast functional iteration without a downloaded BIOS or disc bootstrap | Real-console timing, retail boot, and optical filesystem behavior. |
| dcload-IP plus Ethernet adapter | ELF sent by `dc-tool-ip` | Hardware execution, host files/logs, optional GDB | Rights to the means used to boot the loader; standalone-disc behavior. |
| dcload-serial plus coder's cable | ELF sent by `dc-tool-ser` | Hardware execution with a serial host link | Same boot caveat; transfer speed is much lower. |
| SD storage with a compatible loader | Files copied to SD | Convenient removable storage | It does not itself create a host debugger or make the console boot from SD. |
| CD-R self-boot image | Properly mastered CDI/MIL-CD | Tests a physical optical release path | Universal console compatibility or license clearance of embedded boot material. |
| Optical-drive emulator | Supported disc image | Avoids burning discs during iteration | A network debugger, optical-drive timing, or blanket firmware/boot-image rights. |

For frequent hardware development, an existing compatible Ethernet loader setup is generally the most convenient; use a coder's cable when that is the available host transport. **These recommendations are conditional on an authorized boot path.** A loader's open-source code license does not resolve the separately embedded code used to start it. Sources: [dcload-IP](https://github.com/KallistiOS/dcload-ip/blob/a78576cde53220887f38bba68143265b2c66f5a5/README.md), [dcload-serial](https://github.com/KallistiOS/dcload-serial/blob/e4b29bffcea2149d078ddd91c7d395a94e49c4e5/README.md), [Flycast ELF behavior](https://github.com/flyinghead/flycast/blob/8933f9fe0ccd3f7cbe2e77717ea3a6db8b472129/core/emulator.cpp).

### 14.2 dcload and host tools

The Dreamcast runs **dcload**; your computer runs **dc-tool**. With dcload-IP already running, use your actual console IP address:

```bash
dc-tool-ip -t 192.168.1.100 -x game.elf
```

The host tool can serve files under `/pc`; its `-m` option selects the exported host root, while `-n` declines the normal console/fileserver attachment. Export only the project data you intend the program to access. Retain the full ELF locally for debugger symbols and exception analysis. Source: [dc-tool-IP options and implementation](https://github.com/KallistiOS/dcload-ip/blob/a78576cde53220887f38bba68143265b2c66f5a5/host-src/tool/dc-tool.c).

For a compatible serial coder's cable, a conservative starting command is:

```bash
dc-tool-ser -t /dev/ttyUSB0 -b 115200 -x game.elf
```

Replace the device and choose a rate supported by both the loader and your adapter. Higher rates are hardware-dependent. A **serial SD reader is not this cable**: it uses the port for storage, does not connect a host terminal/GDB transport, and normally occupies the same console socket. Sources: [serial loader instructions](https://github.com/KallistiOS/dcload-serial/blob/e4b29bffcea2149d078ddd91c7d395a94e49c4e5/README.md), [KOS SD interface](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/sd.h).

dcload-IP and dcload-serial are GNU GPL projects; preserve their source/license obligations if redistributing them. That does not automatically relicense a separately loaded original game. Audit bundled disc/bootstrap files separately from the loader's C source. Sources: [IP loader COPYING](https://github.com/KallistiOS/dcload-ip/blob/a78576cde53220887f38bba68143265b2c66f5a5/COPYING), [serial loader COPYING](https://github.com/KallistiOS/dcload-serial/blob/e4b29bffcea2149d078ddd91c7d395a94e49c4e5/COPYING).

### 14.3 ELF, raw binary, `1ST_READ.BIN`, and `IP.BIN`

| Artifact | Meaning |
|---|---|
| `.elf` | Linker output with entry point, sections, and optionally symbols. Preferred development artifact. |
| Raw `.bin` | Extracted machine-code/data image without the ELF container. A loader must know the expected address/entry behavior. |
| `1ST_READ.BIN` | Conventional root-level boot executable filename; representation depends on the boot format. |
| `IP.BIN` | Initial program/header/bootstrap region used by conventional disc boot. It is separate from the game executable. |
| `.cdi` | Disc-image container used for common MIL-CD homebrew mastering. |
| `.gdi` plus track files | GD-style disc-image descriptor and associated tracks for compatible emulators/ODEs. Keep the track set together. |

The MIL-CD boot path expects the executable in its scrambled form; GD-style boot uses an unscrambled representation. This is format-specific rearrangement, not a security feature or a copyright license. A current builder can consume ELF and handle the proper transformation. Do not scramble twice or assume that renaming `game.elf` to `1ST_READ.BIN` produces a valid executable. Sources: [mkdcdisc CLI format options](https://gitlab.com/simulant/mkdcdisc/-/blob/4d74e40dd2122e14389a305ed1d86dd024201389/docs/cli-reference.md), [KOS scrambling utility](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/scramble).

### 14.4 The unresolved `IP.BIN` licensing boundary

KOS `makeip` is BSD-2-Clause, and its default template uses LiENUS's independently rewritten bootstrap. Its historical `ipalt.tmpl` uses Sega's AIP code and is excluded from this guide's recommended path. **Even the replacement does not make the entire boot region freely licensed.** The examined mkdcdisc notice identifies bytes **`0x0300` through `0x36ff`**, **13,312 bytes**, as mandatory Sega-authored license-screen/authentication code and claims no license for that region. Sources: [makeip license](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/makeip/LICENSE), [template provenance](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/makeip/rsrc/README.md), [mkdcdisc third-party notices](https://gitlab.com/simulant/mkdcdisc/-/blob/4d74e40dd2122e14389a305ed1d86dd024201389/THIRD-PARTY-NOTICES.md).

For your requirement to use only clearly licensed components, **do not treat a generated complete `IP.BIN` as cleared for redistribution**. Keep original ELF/HLE development as the baseline and resolve the boot material's authorization or applicable legal exception before distributing a conventional self-booting image. Public availability, source-built tooling, console ownership, and a long history of community use do not themselves supply the missing license.

This document does not distribute that region, a retail-disc `IP.BIN`, Sega SDK binaries, or a BIOS image. It also does not recommend the old prebuilt `make-cd/IP.BIN` bundled in dcload-serial without a separate provenance review.

### 14.5 Disc-building workflow, conditional on resolving boot rights

The following is a technical reference for **after the preceding boot-image rights issue is resolved**. It is not the fully license-cleared baseline.

The inspected [mkdcdisc project](https://gitlab.com/simulant/mkdcdisc) at `4d74e40dd2122e14389a305ed1d86dd024201389` builds with Meson. Its documented Linux prerequisites include `meson`, `pkg-config`, `libisofs-dev`, and `libpng-dev`. A typical source build is:

```bash
git clone https://gitlab.com/simulant/mkdcdisc.git
cd mkdcdisc
git checkout 4d74e40dd2122e14389a305ed1d86dd024201389
meson setup builddir
meson compile -C builddir
./builddir/mkdcdisc -h
```

From a project directory, using that executable or an installed `mkdcdisc`:

```bash
mkdcdisc -e game.elf -D disc/ -o game.cdi
mkdcdisc -e game.elf -D disc/ -o game.gdi
```

Here `disc/` contains your own staged assets. Do not manually pre-scramble the ELF input. Avoid custom boot templates until you understand the boot filename, format, and rights implications. Sources: [build instructions](https://gitlab.com/simulant/mkdcdisc/-/blob/4d74e40dd2122e14389a305ed1d86dd024201389/BUILDING.md), [usage examples](https://gitlab.com/simulant/mkdcdisc/-/blob/4d74e40dd2122e14389a305ed1d86dd024201389/docs/examples.md).

mkdcdisc's own code is MIT, but its assembled program and resources include components with other licenses, including libisofs and makeip. Its notices also identify uncertainty around upstream scrambling-code licensing. Accordingly, this guide does not endorse distributing an arbitrary mkdcdisc binary as an entirely MIT package. Review the exact component inventory. Source: [third-party notices](https://gitlab.com/simulant/mkdcdisc/-/blob/4d74e40dd2122e14389a305ed1d86dd024201389/THIRD-PARTY-NOTICES.md).

### 14.6 Physical compatibility

MIL-CD support varies with console revision; there is no verified universal serial/date rule established by this research. Test the actual intended console. An ODE can change the image-loading route, but ODE success does not prove a burned disc will boot in a stock drive. Prefer standard/full images for compatibility testing; truncated GDIs are a nonstandard optimization with device-specific support. Sources: [CD-image compatibility notes](https://gitlab.com/simulant/mkdcdisc/-/blob/4d74e40dd2122e14389a305ed1d86dd024201389/docs/cd-images.md), [GDI notes](https://gitlab.com/simulant/mkdcdisc/-/blob/4d74e40dd2122e14389a305ed1d86dd024201389/docs/gd-image.md).

For genuine GDEMU, the author's documented target is **VA1**; VA2 is incompatible, and VA0 voltage differences can damage hardware. Do not generalize this to other ODEs or clones. GDEMU firmware is not established here as freely redistributable open source. Source: [GDEMU author's hardware details](https://gdemu.wordpress.com/details/gdemu-details/).

### 14.7 DreamShell is outside the permissive baseline

DreamShell can be useful as a loader/storage environment, but its current core and DreamShell-specific components, including ISO Loader, use **PolyForm Noncommercial 1.0.0**. That is source-available with a noncommercial restriction, not a permissive SDK license. Commercial use requires separate permission under the project's terms. Therefore it is not part of this guide's general commercial-capable baseline. Source: [DreamShell license at the researched revision](https://github.com/DC-SWAT/DreamShell/blob/1eeeed1efb529945ed20ee03fe99846b68ef9def/LICENSE).

## 15. Debugging and profiling

### 15.1 Retain useful build artifacts

Keep `game.elf` with symbols, a link map, the exact sources, compiler version, ABI flags, and cooked asset hashes. Produce a separate release image rather than stripping the only copy of your debugging ELF. When a crash gives a program counter, use the matching binary:

```bash
sh-elf-addr2line -e game.elf -f -C 0x8c012340
sh-elf-objdump -d -S game.elf > game.disasm.txt
sh-elf-size game.elf
sh-elf-nm -S --size-sort game.elf
```

The address is an example; substitute the actual fault address. A different build can map the same address to unrelated code. Logs should identify the build and preserve the exception registers needed to understand a bad pointer, alignment fault, stack error, or illegal instruction.

### 15.2 Hardware GDB through dcload

The kos-chain default compiler build does not necessarily install GDB. At the researched revision, `make gdb` builds it explicitly and `make all` includes it. Source: [builder targets](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/kos-chain/Makefile).

For a debug-specific build, include `<arch/gdb.h>` and call `gdb_init()` early in execution. With the loader already running, start its GDB transport and then attach a matching `sh-elf-gdb` session:

```bash
dc-tool-ip -t 192.168.1.100 -g -x game.elf
```

In another terminal:

```text
sh-elf-gdb game.elf
(gdb) target remote :2159
(gdb) break main
(gdb) continue
```

If `gdb_init()` is inside `main`, execution has already entered it before the initial stub break. Set a breakpoint on a subsequent function/line when needed, rather than expecting `break main` to rewind startup. Transport startup details depend on the loader revision. Sources: [KOS GDB stub](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/gdb.h), [dcload-IP debugging instructions](https://github.com/KallistiOS/dcload-ip/blob/a78576cde53220887f38bba68143265b2c66f5a5/README.md).

A debugger changes timing and may stop audio/network service. Use it to understand correctness failures, then measure an ordinary execution build separately.

### 15.3 Emulator workflow and its limits

Flycast is an open-source GPL project and supports direct ELF loading through its HLE path. This is a useful starting workflow that avoids requiring a downloaded retail BIOS or the conventional disc bootstrap. It also bypasses precisely the boot stages a physical release needs to validate. Sources: [Flycast license](https://github.com/flyinghead/flycast/blob/8933f9fe0ccd3f7cbe2e77717ea3a6db8b472129/LICENSE), [ELF-loading implementation](https://github.com/flyinghead/flycast/blob/8933f9fe0ccd3f7cbe2e77717ea3a6db8b472129/core/emulator.cpp).

Its optional built-in GDB server requires a build with `ENABLE_GDB_SERVER=ON`, an enabled runtime debugger setting, and uses port **3263** by default. A connection can use `set architecture sh4`, `set endian little`, then `target remote :3263`. This is a **different debugger route** from dcload's port 2159 and KOS stub. Sources: [Flycast build option](https://github.com/flyinghead/flycast/blob/8933f9fe0ccd3f7cbe2e77717ea3a6db8b472129/CMakeLists.txt), [server defaults](https://github.com/flyinghead/flycast/blob/8933f9fe0ccd3f7cbe2e77717ea3a6db8b472129/core/debug/gdb_server.h).

The inspected server has limitations around hardware breakpoints/watchpoints and advanced commands. Attaching a debugger disables the dynamic recompiler and can slow execution considerably. Emulator FPS with or without the debugger is not a hardware performance measurement. Sources: [GDB server implementation](https://github.com/flyinghead/flycast/blob/8933f9fe0ccd3f7cbe2e77717ea3a6db8b472129/core/debug/gdb_server.cpp), [debugger UI behavior](https://github.com/flyinghead/flycast/blob/8933f9fe0ccd3f7cbe2e77717ea3a6db8b472129/core/ui/settings.cpp).

### 15.4 Profiling that answers a concrete question

Use timers and `pvr_get_stats()` to distinguish CPU geometry, registration, rendering, and waiting. KOS also provides `gprof` and `gcov` examples; their output paths and instrumentation have costs. Test native CPU logic and asset conversion on the host where practical, but validate target assembly, cache, DMA, timing, and display behavior on the console. Sources: [PVR stats definitions](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr/pvr_misc.h), [gprof example](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/profiling/gprof/README.md), [gcov example](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/profiling/gcov/README.md).

Create a small set of repeatable workloads: geometry-heavy, blended-pixel-heavy, texture-upload-heavy, animated, and storage/audio streaming. Record median and worst-case frame intervals, memory high-water marks, and underflows for each build. A faster average that introduces a long periodic stall is usually a regression for a real-time game.

## 16. Implementation milestones

| Stage | Concrete deliverable | Exit condition |
|---|---|---|
| 1. Toolchain | Pinned KOS/compiler setup and hello ELF | Correct target ELF; output received through a working loader. |
| 2. Frame loop | Triangle, controller exit, stable timing | Boots repeatedly on intended display modes. |
| 3. Texture path | Converted opaque, cutout, and blended test assets | Layout, alpha, palette, and filtering match on hardware. |
| 4. 3D camera | Transform/project/clip a small mesh | No spikes or inversions when crossing the near plane. |
| 5. Asset packs | Offline conversion and a single package reader | Release reads work without host-mounted files. |
| 6. World visibility | Cells/rooms or another spatial structure plus LOD | Hidden objects stop consuming geometry work. |
| 7. Animation/audio | Budgeted animated character and streamed music | No deadline misses under the chosen worst-case scene. |
| 8. Saves | Versioned VMU package and recovery logic | Full, removed, and corrupted-card cases handled. |
| 9. Performance | Repeatable scene and memory measurements | Target frame interval maintained with measured headroom. |
| 10. Release | Reproducible image, license bundle, hardware notes | Cold boot and full play path pass without development dependencies. |

For the first vertical slice, keep the scene small: one room, one animated object, one sound loop, one VMU save. Make the complete load–play–save–reload path reliable before expanding the asset library or introducing advanced transfer modes.

## 17. Source-code and example index

Paths below are relative to the KOS checkout root. Every listed KOS path was checked at the pinned revision. Study example structure, but review the license of any included image, font, music, or other sample asset before reusing it.

### 17.1 Toolchain, startup, and kernel

| Concern | Primary path | Related implementation/example |
|---|---|---|
| Toolchain build/configuration | [utils/kos-chain/README.md](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/kos-chain/README.md) | [utils/kos-chain/Makefile.dreamcast.cfg](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/kos-chain/Makefile.dreamcast.cfg) |
| Environment and platform ABI | [doc/environ.sh.sample](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/doc/environ.sh.sample) | [environ_dreamcast.sh](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/environ_dreamcast.sh) |
| C/C++ make build | [Makefile.rules](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/Makefile.rules) | [utils/build_wrappers/kos-c++](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/build_wrappers/kos-c++) |
| CMake cross-build | [utils/cmake/kallistios.toolchain.cmake](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/cmake/kallistios.toolchain.cmake) | — |
| Startup and initial stack | [kernel/arch/dreamcast/kernel/startup.S](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/kernel/startup.S) | [kernel/arch/dreamcast/kernel/init.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/kernel/init.c) |
| Link addresses and sections | [utils/ldscripts/shlelf.xc](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/ldscripts/shlelf.xc) | — |
| Threads and scheduling | [include/kos/thread.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/include/kos/thread.h) | [kernel/thread/thread.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/thread/thread.c) |
| Interrupts and exceptions | [kernel/arch/dreamcast/include/arch/irq.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/irq.h) | [kernel/arch/dreamcast/kernel/irq.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/kernel/irq.c) |
| Timers | [kernel/arch/dreamcast/include/arch/timer.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/timer.h) | [kernel/arch/dreamcast/kernel/timer.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/kernel/timer.c) |
| Hardware CPU counters | [kernel/arch/dreamcast/include/dc/perfctr.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/perfctr.h) | [kernel/arch/dreamcast/include/dc/perf_monitor.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/perf_monitor.h) |
| Cache and DMA | [kernel/arch/dreamcast/include/arch/cache.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/cache.h) | [kernel/arch/dreamcast/include/arch/dmac.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/dmac.h) |
| SQ implementation | [kernel/arch/dreamcast/include/dc/sq.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/sq.h) | [kernel/arch/dreamcast/hardware/sq_fast_cpy.s](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/sq_fast_cpy.s) |
| Optimized matrices | [kernel/arch/dreamcast/include/dc/matrix.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/matrix.h) | [kernel/arch/dreamcast/math/matrix.s](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/math/matrix.s) |

### 17.2 Graphics and texture examples

| Concern | Primary path | Related implementation/example |
|---|---|---|
| Public PVR entry point | [kernel/arch/dreamcast/include/dc/pvr.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr.h) | — |
| Compiled polygon headers | [kernel/arch/dreamcast/include/dc/pvr/pvr_header.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr/pvr_header.h) | [kernel/arch/dreamcast/hardware/pvr/pvr_prim.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/pvr/pvr_prim.c) |
| Scene scheduling | [kernel/arch/dreamcast/hardware/pvr/pvr_scene.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/pvr/pvr_scene.c) | [kernel/arch/dreamcast/hardware/pvr/pvr_irq.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/pvr/pvr_irq.c) |
| VRAM buffers/allocator | [kernel/arch/dreamcast/hardware/pvr/pvr_buffers.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/pvr/pvr_buffers.c) | [kernel/arch/dreamcast/include/dc/pvr/pvr_mem.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr/pvr_mem.h) |
| Texture upload | [kernel/arch/dreamcast/include/dc/pvr/pvr_txr.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr/pvr_txr.h) | [kernel/arch/dreamcast/hardware/pvr/pvr_texture.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/pvr/pvr_texture.c) |
| DMA transfers | [kernel/arch/dreamcast/include/dc/pvr/pvr_dma.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr/pvr_dma.h) | [examples/dreamcast/basic/dma/speedtest/speedtest.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/basic/dma/speedtest/speedtest.c) |
| Basic PVR workload | [examples/dreamcast/pvr/pvrmark/pvrmark.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/pvrmark/pvrmark.c) | — |
| Strips and direct submission | [examples/dreamcast/pvr/pvrmark_strips/pvrmark_strips.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/pvrmark_strips/pvrmark_strips.c) | [examples/dreamcast/pvr/pvrmark_strips_direct/pvrmark_strips_direct.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/pvrmark_strips_direct/pvrmark_strips_direct.c) |
| 4-bit and 8-bit palettes | [examples/dreamcast/pvr/palette/4bpp/4bpp.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/palette/4bpp/4bpp.c) | [examples/dreamcast/pvr/palette/8bpp/8bpp.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/palette/8bpp/8bpp.c) |
| Strided texture | [examples/dreamcast/pvr/strided_texture/strided_texture.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/strided_texture/strided_texture.c) | — |
| Modifier volumes and clipping | [examples/dreamcast/pvr/modifier_volume/modifier.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/modifier_volume/modifier.c) | [examples/dreamcast/pvr/modifier_volume_zclip/pvr_zclip.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/modifier_volume_zclip/pvr_zclip.c) |
| Sized render-to-texture | [examples/dreamcast/pvr/pvr_rtt_sized/pvr_rtt_sized.cc](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/pvr_rtt_sized/pvr_rtt_sized.cc) | — |
| GLdc examples in KOS | [examples/dreamcast/gldc](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/gldc) | — |
| Offline texture conversion | [utils/pvrtex/readme_unformatted.txt](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/pvrtex/readme_unformatted.txt) | [utils/vqenc/vqenc.1](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/vqenc/vqenc.1) |

### 17.3 Audio, input, storage, and debugging

| Concern | Primary path | Related implementation/example |
|---|---|---|
| AICA low-level and driver | [kernel/arch/dreamcast/include/dc/spu.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/spu.h) | [kernel/arch/dreamcast/sound/arm](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/sound/arm) |
| Effects and streaming | [kernel/arch/dreamcast/include/dc/sound/sfxmgr.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/sound/sfxmgr.h) | [kernel/arch/dreamcast/include/dc/sound/stream.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/sound/stream.h) |
| Sound examples | [examples/dreamcast/sound/sfx/main.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/sound/sfx/main.c) | [examples/dreamcast/sound/multi-stream/main.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/sound/multi-stream/main.c) |
| Maple/controller input | [kernel/arch/dreamcast/include/dc/maple/controller.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/maple/controller.h) | [examples/dreamcast/maple/maple.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/maple/maple.c) |
| VMU package | [kernel/arch/dreamcast/include/dc/vmu_pkg.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/vmu_pkg.h) | [examples/dreamcast/vmu/vmu_pkg/vmu.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/vmu/vmu_pkg/vmu.c) |
| VMU LCD | [examples/dreamcast/vmu/vmu_lcd/lcd.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/vmu/vmu_lcd/lcd.c) | — |
| Optical reads/streaming | [kernel/arch/dreamcast/include/dc/cdrom.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/cdrom.h) | [examples/dreamcast/cdrom/stream/cd-stream-test.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/cdrom/stream/cd-stream-test.c) |
| SD block device | [kernel/arch/dreamcast/include/dc/sd.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/sd.h) | [examples/dreamcast/filesystem/sd](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/filesystem/sd) |
| ATA device | [kernel/arch/dreamcast/include/dc/g1ata.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/g1ata.h) | [examples/dreamcast/g1ata/atatest/atatest.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/g1ata/atatest/atatest.c) |
| Network sockets | [include/kos/net.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/include/kos/net.h) | [examples/dreamcast/network/basic/basic.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/network/basic/basic.c) |
| ROMFS and host I/O | [include/kos/fs_romdisk.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/include/kos/fs_romdisk.h) | [kernel/arch/dreamcast/fs/fs_dcload.c](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/fs/fs_dcload.c) |
| GDB API and implementation | [kernel/arch/dreamcast/include/arch/gdb.h](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/arch/gdb.h) | [kernel/arch/dreamcast/gdb](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/gdb) |
| CPU profiling | [examples/dreamcast/profiling/gprof/README.md](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/profiling/gprof/README.md) | [examples/dreamcast/profiling/gcov/README.md](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/profiling/gcov/README.md) |
| Core license | [doc/LICENSE.md](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/doc/LICENSE.md) | [doc/license/LICENSE.KOS](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/doc/license/LICENSE.KOS) |
| Bootstrap provenance caveat | [utils/makeip/rsrc/README.md](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/makeip/rsrc/README.md) | [utils/makeip/LICENSE](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/makeip/LICENSE) |

## 18. Common mistakes and corrections

| Mistake | Correction |
|---|---|
| “Open source means no license obligations.” | Keep required notices; audit linked code and embedded assets separately. |
| “Any `IP.BIN` found online is fine.” | Establish provenance for every embedded bootstrap and logo. |
| “A GPL compiler forces the game to be GPL.” | Distinguish host-tool licensing, runtime exceptions, and libraries actually linked. |
| “Old `dc-chain` instructions describe today's tree.” | Use the pinned revision's `kos-chain` and environment files. |
| “All SH-4 `.a` files are ABI-compatible.” | Keep compiler/runtime/FPU ABI consistent across all objects. |
| “The GPU transforms or skins the mesh.” | Budget those operations on the SH-4 or in a software layer. |
| “Tile-based rendering makes every hidden polygon free.” | Hidden input still costs CPU, submission, binning, and parameter storage. |
| “All transparency is order-independent without restrictions.” | Choose and test list, sorting, depth, and blending policy. |
| “A twiddled texture is compressed.” | Layout and compression are separate properties. |
| “Every texture must be square.” | Ordinary dimensions and mipmapped/strided special cases have different rules. |
| “The entire 8 MiB VRAM is texture space.” | Reserve framebuffers and TA/PVR working buffers first. |
| “DMA makes cache coherence automatic.” | Follow the exact API contract for direction, alignment, ownership, and cache maintenance. |
| “DMA finished, therefore the texture is no longer used.” | Rendering may still sample it after the copy completes. |
| “An SD serial adapter is a debugger cable.” | Storage and host-debug transport are separate device functions. |
| “`/sd` or `/pc` always exists.” | Mounts depend on drivers, initialization, loader, and host setup. |
| “More CPU threads mean more CPU cores.” | Threading helps scheduling and waiting; there is one SH-4 core. |
| “Emulator FPS measures Dreamcast speed.” | Measure performance and final correctness on the target console. |
| “A benchmark's polygon rate is the game budget.” | Benchmark your textures, visibility, clipping, animation, transparency, and audio together. |

## 19. Release checklist and evidence limits

### 19.1 Required project checks

- Record exact KOS, port, toolchain, converter, image-builder, and loader versions.
- Build all target objects with a consistent ABI and retain the symbol-bearing ELF and map file.
- Include applicable license texts, copyright notices, contributor credits, and any required dependency source/relinking materials.
- Verify provenance of embedded boot files, fonts, images, audio, libraries, and templates.
- Test cold boot and release filesystem paths with the development computer disconnected.
- Check controller removal, missing/full VMU, interrupted or invalid saves, and low-memory handling.
- Measure the most demanding scenes while streaming audio and using the real storage path.
- Confirm no DMA buffer is reused prematurely and no in-flight texture is overwritten or freed.
- Test intended output modes, hardware revisions, and deployment devices.
- Keep unsupported hardware, optional adapters, and emulator-only behavior explicit in the release notes.

### 19.2 What the evidence establishes

This reference was assembled from maintained open-source implementations, their license files, public API documentation, and original hardware research/manufacturer sources. KOS paths and current build configuration were checked in an actual checkout at the pinned revision. Mathematical size/time examples were calculated explicitly. The recommended engine layout, memory reservations, and development milestones are engineering proposals.

This work did **not** build the complete cross-toolchain, run the illustrative application on a Dreamcast, certify a finished image, measure actual rendering throughput, or provide a jurisdiction-specific legal opinion. Open-source code availability establishes a usable licensed development route; it does not establish that an arbitrary third-party download or every console modification is authorized. Where boot assets or old source terms remain uncertain, the document leaves that uncertainty visible instead of claiming permission.

The most useful next action is to build the upstream hello sample, establish one reliable loader, and complete the small native rendering example. That validates the compiler, runtime, display, input, and deployment path before engine complexity obscures a basic setup problem.
