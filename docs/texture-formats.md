# Dreamcast texture formats

The texture and font-atlas platform settings publish these PVR formats:

| Format ID | GPU storage | Available alpha |
| --- | --- | --- |
| `DcArgb1555` | 16-bit ARGB1555 | Opaque, Binary |
| `DcRgb565` | 16-bit RGB565 | Opaque |
| `DcArgb4444` | 16-bit ARGB4444 | Opaque, Binary, A4 |
| `DcYuv422` | 16-bit horizontal YUV422 pairs | Opaque |
| `DcBump` | 16-bit spherical normal coordinates | Opaque |
| `DcPal4` | 4-bit indices + 16 RGBA8888 palette entries | Opaque, Binary, A4, A8 |
| `DcPal8` | 8-bit indices + 256 RGBA8888 palette entries | Opaque, Binary, A4, A8 |

Base IDs use square Morton/twiddled storage. Every base ID has a `_Mipmapped`
variant. The five direct-color formats also have `_Linear`, `_Vq`, and
`_VqMipmapped` variants. The 29 native IDs are platform-owned strings, available
in the editor's Dreamcast texture settings. `Rgba32` remains the historical
default setting: it produces a version-one ARGB4444 texture.

Sources are padded to a square power of two, from 8 through 512 pixels. The
header retains the original image extent. Ordinary textures default to a
64-pixel resolution cap; font atlases default to 512. Downscaling and mipmaps
use box filtering with premultiplied alpha. Binary-alpha mipmaps are thresholded
again after filtering. One palette is shared across an indexed texture's mip
chain. PAL4 stores the first Morton texel in the low nibble; the 1x1 mip uses
the hardware's special high-nibble position.

VQ uses a complete 2048-byte codebook of 256 2x2 vectors, followed by one byte
per block. Images with at most 256 distinct vectors encode exactly in their
target pixel format. More complex images use deterministic frequency-ranked
representatives and nearest expanded-RGBA matching; compression is lossy.
Every mip uses that same codebook. All GPU upload payloads are padded to 32
bytes so the store-queue copy includes the end of the index/mip data.

## Native file import

The Windows editor registers `dc-pvr` for `.pvr`. The builder can also decode
an authored `.pvr` directly, without System.Drawing. Both use the portable
`DreamcastPvrTextureImporter` and return an RGBA32 preview/source asset.

The importer reads Sega `PVRT` chunks, with an optional preceding `GBIX` chunk,
and accepts standard Sega or KOS whole-chunk length conventions. It handles
square twiddled textures, twiddled rectangles, row-major rectangles, stride
containers, mipmaps (including the alternate layout code), VQ and small VQ.
Mipmapped imports select the highest-resolution level; cooking builds the
target mip chain from that source. Native RGB555 and BGRA8888 file pixels can
also be previewed, in addition to the hardware direct-color formats.

Paletted PVRT images contain indices and require an external palette. Place a
KallistiOS `DPAL` `.pal` file beside the image with the same basename, or call
`ImportTexture(Stream, byte[] rgbaPalette)` with explicit RGBA8888 entries.
Missing palettes and out-of-range indices fail with an error. `.pvm` archives
and modern mobile PowerVR PVR v2/v3 containers are separate container formats
and are not registered as Dreamcast images.

## Cooked contract and runtime

All multibyte fields are little-endian. DCTX version two has the same 16-byte
header size as version one:

| Byte offset | Field |
| --- | --- |
| 0 | `DCTX` magic |
| 4 | `uint16` version: 2 |
| 6 | `uint16` flags: pixel code in bits 0..2; VQ in bit 3; mipmaps in bit 4; linear in bit 5 |
| 8 | `uint16` padded side |
| 10 | `uint16` used width |
| 12 | `uint16` used height |
| 14 | `uint16` palette entry count: 0, 16 or 256 |
| 16 | Format-dependent payload, including codebook/mips and 32-byte tail padding |
| after payload | RGBA8888 palette entries |

Pixel codes 0..6 follow the PVR hardware order: ARGB1555, RGB565, ARGB4444,
YUV422, bump, PAL4, PAL8. The native parser rejects undefined bits, invalid
palette counts, incompatible VQ/linear/indexed layouts and incorrect lengths.
Version-one ARGB4444 files retain their existing header interpretation.

Runtime textures own a VRAM allocation and, when indexed, a distinct palette
bank. PAL8 banks align to 256 entries; PAL4 banks align to 16. All banks use
the PVR's global ARGB8888 palette interpretation. Disposal releases both
resources. The renderer submits the native flags, bank and mipmap setting.
The package validator enforces a total 4 MiB texture-VRAM budget and 1024
palette entries. Runtime allocation also detects exhausted or fragmented
palette banks.

Bump sources are RGB normal maps; their preview expands native spherical
coordinates back into RGB. Standalone bump sprites use a fixed frontal light.
Material-controlled multi-pass bump lighting and textured 3D mesh rendering
require additional renderer work beyond this texture-format pipeline.

References: [KallistiOS PVR definitions](https://kos-docs.dreamcast.wiki/pvr_8h_source.html),
[KallistiOS pixel/mip implementation](https://github.com/KallistiOS/KallistiOS/blob/master/utils/pvrtex/pvr_texture.c),
[PVRT storage-code definitions](https://github.com/nickworonekin/puyotools/blob/master/src/PuyoTools.Core/Textures/Pvr/PvrDataFormat.cs).
