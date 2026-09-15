# Dreamcast Rendering Optimization

## 1. Recommended direction

For a general 3D engine, the strongest starting point is **KallistiOS, SH4-accelerated vertex processing, an offline mesh/texture conversion pipeline, and explicit management of PowerVR polygon lists**. Use GLdc when its OpenGL interface saves substantial integration work; consider a native PVR path for measured hot spots. The architecture rewards reducing vertex work and submission traffic while keeping geometry preparation overlapped with rendering.[^1][^2][^3]

The first optimization question is **which stage is limiting the frame**. CPU transformation, Tile Accelerator registration, texture sampling, translucent layers, and synchronization require different remedies. A faster copy routine cannot recover time spent shading overlapping transparent particles; texture compression cannot remove expensive CPU skinning.

The following is an engineering priority order, not a promised ranking for every scene:

| Priority | Change | Likely benefit | Condition to check |
|---|---|---|---|
| 1 | Measure CPU work, registration, rendering, waits, and buffer use separately | Identifies the actual limiting stage | A 60 FPS cap can conceal improvements |
| 2 | Cull objects or spatial chunks before transforming their vertices | Saves work throughout the pipeline | Culling must cost less than the work rejected |
| 3 | Precompute useful triangle strips and material groups | Reduces emitted vertices and repeated setup | Preserve spatial culling granularity |
| 4 | Use FTRV-based transforms and appropriate SH4 math | Reduces CPU geometry cost | Avoid replacing a tuned loop with more matrix loads |
| 5 | Cache polygon headers and remove transient allocations | Reduces CPU setup and cache traffic | Invalidate headers when material addresses/state change |
| 6 | Convert textures offline; test twiddling, mipmaps, and VQ | Reduces conversion work, VRAM pressure, and potentially texture traffic | Compare quality and actual render time |
| 7 | Keep solid, cutout, and blended materials in appropriate lists | Avoids unnecessary blending and sorting | Material classification must match visual semantics |
| 8 | Compare direct Store Queue submission with buffered DMA | Can reduce stalls or copies | Measure memory use and input latency as well as throughput |
| 9 | Reduce large translucent layers and repeated effects passes | Reduces pixel and geometry work | Evaluate the busiest view, not an empty scene |
| 10 | Tune small CPU kernels, cache layout, and packet formats | Extracts remaining performance | Only after profiling the complete renderer |

Sources below distinguish **documented behavior**, **observed code implementations**, **developer-reported results**, and **proposed experiments**. No new Dreamcast hardware timings are claimed in this report.

## 2. The hardware model that changes optimization decisions

### 2.1 Responsibilities and overlap

The Dreamcast uses a 200 MHz SH4 with an 8 KiB instruction cache and a 16 KiB data cache. Its PowerVR system has 8 MiB of video memory and bins geometry into 32 × 32 pixel tiles. Binning a later frame and rendering an earlier frame can overlap when separate buffers are available.[^4][^5]

| Stage | What happens | What an engine can improve |
|---|---|---|
| SH4 | Visibility decisions, animation, transformation, lighting, clipping, command generation | Fewer processed vertices; efficient math, memory layout, and batching |
| Tile Accelerator, or TA | Accepts commands, converts geometry, and associates primitives with tiles | Fewer submitted records; useful strips; adequate parameter and object-pointer storage |
| ISP/TSP | Resolves visibility and performs texturing/blending for tiles | Less expensive visible texturing, fewer translucent layers, fewer effects passes |
| Display | Scans out the completed framebuffer | Correct synchronization and an appropriate resolution/refresh target |

KOS explicitly manages vertex/parameter storage, object-pointer bins, and tile descriptors. Its normal buffered pipeline separates collection from rendering. Turning off vertex-buffer double buffering forces rendering to finish before the TA prepares another frame, exchanging overlap for memory savings.[^6][^7]

For planning, 200 MHz provides approximately **3.33 million CPU cycles per 60 Hz frame** or **6.67 million per 30 Hz frame**. These are calculated budgets for the whole CPU workload, including game logic and stalls—not geometry budgets.

### 2.2 What hidden-surface removal does and does not save

PowerVR defers opaque texturing until visibility has been resolved. Consequently, hidden opaque surfaces need not incur the same repeated texture/shading work as on a simple immediate renderer. They still reached the geometry pipeline: their CPU transforms, submission, binning, and visibility work are not retroactively removed.[^5]

This leads to three recommendations:

1. **Cull before transformation when rejection is inexpensive.** GPU hidden-surface removal does not replace object visibility management.
2. **Treat opaque state grouping as a more promising starting point than an expensive global front-to-back sort.** Retain a depth sort only if measurements show a net benefit.
3. **Do not add a conventional depth prepass by default.** It repeats geometry work to solve a problem the deferred architecture already addresses in part. A special multipass effect needs its own justification.

The word *deferred* here describes visibility/texturing behavior. It does not mean a modern shader-based G-buffer renderer. Likewise, the absence of a conventional full-screen external Z-buffer does not mean depth comparisons are absent: KOS exposes depth-test modes explicitly.[^8]

## 3. What contemporary commercial developers actually reported

### 3.1 Kalisto: batching, strips, and prepared data

Sebastian Wloch’s March 1999 article describes optimizing a Windows CE/Direct3D Dreamcast application through larger submissions, state grouping, aligned data, useful strips, and coarse visibility tests. He reports improving the application from below 10 FPS to nearly 60 FPS after the combined changes. This is an author-reported result for a particular application and API, without isolated timings for each technique.[^9]

The transferable lesson is to make assets and submissions fit the machine. The article’s Direct3D vertex types, flags, and internal strip-generation behavior should not be assumed to describe KOS or GLdc. Its broad claims about unrestricted fill performance also need qualification: blended layers, texture access, geometry processing, and finite storage still cost time.

### 3.2 Draconus: fewer polygons did not mean proportional savings

Jamie Fristrom reports that Draconus’s progressive-mesh system was disappointing: the lowest-detail model still took about half the rendering time of the highest-detail model because of overhead and cache-related problems. He concluded that several simpler prebuilt LOD meshes would have been preferable. He also describes discovering improvements too late because they required different renderer data structures.[^10]

**Application:** start with discrete offline LODs. Time animation, transformation, submission, and rendering together. Reducing triangle count while leaving a large fixed CPU cost untouched may produce a poor return.

### 3.3 Tony Hawk: optimize a concrete worst case

Treyarch targeted a consistent 30 FPS, including two-player split screen. The team tracked a demanding scenario at approximately 60 ms and worked toward 33 ms. For shadows, it rendered a shadow texture and projected it onto nearby world geometry in an additional pass; correct clipping was part of making the effect usable.[^11]

**Application:** establish a worst-case camera and content configuration early. Budget effects by the receiver geometry and screen area they affect. A convincing local shadow may be more valuable than a general effect applied everywhere.

## 4. Source code worth reading first

The table identifies useful entry points. Sections that follow explain the techniques and their limitations. Snapshot identifiers are recorded in Section 15.

| Project | Exact entry points | Why it matters |
|---|---|---|
| [KallistiOS](https://github.com/KallistiOS/KallistiOS) | `kernel/arch/dreamcast/hardware/pvr/`; `kernel/arch/dreamcast/include/dc/pvr.h` | Scene lifecycle, TA submission, memory allocation, DMA, interrupts, profiling |
| [KOS direct-strip benchmark](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/pvrmark_strips_direct) | `pvrmark_strips_direct.c`, especially `do_frame()` | Small reference for cached headers and direct vertex emission[^12] |
| [KOS Bruce’s Balls](https://github.com/KallistiOS/KallistiOS/tree/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/sh4zam/bruces_balls) | `cache_polygon_header()`, `setup_projection_view()`, `render_sphere()` | SH4ZAM transforms, scheduling, Store Queue reuse, and a specialized high-throughput benchmark[^13] |
| [SH4ZAM](https://github.com/gyrovorbis/sh4zam) | `include/sh4zam/inline/sh4/`; `example/pvr_dma/pvr_dma.c` | Reusable math primitives and a more substantial buffered renderer[^3][^14] |
| [GLdc, current upstream](https://gitlab.com/simulant/GLdc) | `GL/attributes.c`, `GL/draw.c`, `GL/draw_fastpath.inc`, `GL/platforms/sh4.c` | Array conversion, transform loops, primitive generation, clipping, and TA packets[^15][^16][^17][^18] |
| [QuakeSpasm-DC](https://github.com/maximqaxd/quakespasm) | `Quake/pvr_backend.c`, `pvr_rsurf.c`, `pvr_alias.c`, `pvr_clip.c`, `pvr_texmgr.c` | A complete game-engine context with native PVR and GLdc build paths[^19] |
| [DCUE](https://github.com/skmp/dcue) | `tools/DreamExporter.cs`, `tools/repacker.cpp`, `main.cpp` | Unity export, offline stripping and compact clusters, specialized runtime processing[^20][^21][^22] |
| [Simulant](https://gitlab.com/simulant/simulant) | `simulant/renderers/batching/render_queue.cpp`; `simulant/renderers/gl1x/gl1x_render_queue_visitor.cpp` | Material grouping and a portable renderer architecture[^23] |
| [sh4zam_pvr](https://github.com/dfchil/sh4zam_pvr) | `docs/`, `code/`, `include/sh4zamsprites/` | Focused tutorials on transformations, hardware sprites, lighting, and OCRAM[^24] |

**Suggested reading order:** direct-strip benchmark → Bruce’s Balls → SH4ZAM DMA example → GLdc submission path → one complete engine. This separates hardware fundamentals from framework complexity.

## 5. Geometry: reduce work before making the loop faster

### 5.1 Measure source vertices, transformed vertices, and submitted vertices separately

These counts are not interchangeable:

- **Source vertices:** unique position/attribute tuples in the asset.
- **Transformed vertices:** how many times the SH4 executes the transformation work.
- **Submitted vertices:** how many vertex records reach the TA after indexing, clipping, and strip generation.
- **Visible triangles:** geometry contributing to the final image.

In the inspected GLdc `generateElementsFastPath()`, each index occurrence invokes the transform operation. An indexed API call therefore does not automatically establish a reusable post-transform cache. Native strips can reduce index occurrences and output records, while an engine-managed transformed-vertex cache is a separate design choice.[^16]

### 5.2 Why strips are especially useful

For an ideal strip containing `T` triangles, `T + 2` vertex records are sufficient. Separate triangles require `3T` records. With `S` independent strips and no degenerates, the strip total is `T + 2S`.

For the common 32-byte packed-color PVR vertex format, the following is **calculated submission payload**, excluding polygon headers, clipping, alignment, and TA-internal representation:[^12]

| 1,000 triangles represented as | Vertex records | Payload |
|---|---:|---:|
| Separate triangles | 3,000 | 96,000 bytes |
| 100 strips, 10 triangles each | 1,200 | 38,400 bytes |
| 20 strips, 50 triangles each | 1,040 | 33,280 bytes |

The second row saves 60% of this vertex payload. That does **not** imply a 60% reduction in frame time. Savings depend on whether transformation, command traffic, or something else dominates. Some PVR formats use larger records; use the actual format’s size.

A useful exporter should record strip count, average useful triangles per strip, duplicate boundary vertices, and degenerate triangles. Optimize within material and spatial groups. Merging an entire level into a few huge strips can impair culling enough to lose the expected gain.

### 5.3 Compact clusters and transform reuse

DCUE’s `process_mesh()` runs TriStripper and then assembles clusters with a 128-vertex capacity. It packs local indices into bytes, using the high bit to mark strip endings. This is an engine-specific compact encoding—not a Dreamcast hardware meshlet facility.[^21]

Its runtime contains separate meshlet routines for transformations, UV copying, baked vertex colors, diffuse lighting, and point lighting. This demonstrates how offline grouping can support specialized CPU processing rather than one enormous general-purpose vertex function.[^22]

For a new engine, compare two designs:

| Design | Advantage | Cost |
|---|---|---|
| Transform directly in strip order | Sequential input and no intermediate transformed buffer | Repeated vertices may be transformed again |
| Transform unique vertices in a small cluster, then emit strip indices | Reuses expensive transformation/lighting work | Temporary storage, gathers, and extra reads |

The second option becomes more attractive as skinning and lighting become more expensive. The first can win for inexpensive static geometry. Choose cluster size by measurement; DCUE’s 128 is an example, not a universal optimum.

### 5.4 Visibility and LOD policy

Recommended asset/runtime policy:

1. Divide the world into regions small enough to reject independently.
2. Test object or cluster bounds before touching their vertex streams.
3. Use conservative bounds for animated objects.
4. Keep ordinary face culling enabled; add CPU-side rejection only where a cheap group test saves substantial work.
5. Select prebuilt LODs before animation and transformation where possible.
6. Consider portals or precomputed visibility for interiors when world structure supports them.

QuakeSpasm-DC’s world-rendering integration retains PVS traversal and per-texture surface chains, illustrating how world visibility and state grouping can coexist.[^25] Avoid per-triangle CPU tests merely because they remove many triangles; compare their cost against simply submitting the affected geometry.

## 6. SH4 vertex processing and memory layout

### 6.1 Use the FPU’s actual strengths

| Operation | Relevant SH4 mechanism | Practical use |
|---|---|---|
| 4 × 4 matrix times vector | `FTRV`, using `XMTRX` | Positions and suitable batches of dot products |
| Four-component dot product | `FIPR` | Lighting and geometric tests |
| Approximate reciprocal square root | `FSRRA` | Normalization where its accuracy/domain are acceptable |
| Sine/cosine pair | `FSCA` | Rotation setup through established wrappers |
| Multiply-add | `FMAC` | Arithmetic kernels and interpolation |

SH4ZAM provides SH4 implementations and a software backend. Its XMTRX and vector code are better starting points than hand-written inline assembly without a tested interface.[^3][^26] KOS also provides SH4 matrix routines, including direct transformation helpers.[^27]

For rigid meshes, concatenate the required transforms once per object/batch and keep the useful matrix resident while processing multiple vertices. Repeatedly loading a matrix for every vertex throws away much of the advantage.

### 6.2 Schedule the work, not just the instruction

Bruce’s Balls arranges its projection so the component needed for reciprocal calculation becomes available early, interleaves independent work, and handles Store Queue writes directly. It also caches the polygon header and reuses constant queue fields. These are tightly coupled optimizations for a controlled workload.[^13]

The broader recommendation is to inspect the compiler’s assembly for the hot loop. Look for spills, redundant loads, unnecessary conversions, and immediate use of results that creates dependency stalls. Benchmark compiler options and inlining on the complete engine; a larger unrolled loop can worsen instruction-cache behavior.

Start from the compiler options supplied by the chosen KOS toolchain. Do not copy an old compiler command line as a universal optimum. Floating-point mode and ABI choices must remain consistent across linked code.[^28]

### 6.3 Cache-aware input and temporary data

The SH7750 cache is **direct-mapped with 32-byte lines**. In the normal 16 KiB operand-cache configuration, input and output locations separated by multiples of 16 KiB can compete for the same cache index. This is an architectural inference from the mapping: two individually small working sets can still evict each other. If a copy/transform loop has unexpectedly high misses, test buffer placement as well as total size.[^49]

Using 8 KiB as on-chip RAM leaves only 8 KiB as operand cache. OCRAM can help a carefully bounded kernel, but the rest of the engine pays for the smaller cache. Also avoid borrowing the larger SH7750R cache specifications from a family datasheet and applying them to the Dreamcast configuration.[^49]

The data cache is small enough that additional passes over a large mesh can be costly. Practical experiments include:

- Sequentially stored vertices and indices in their expected processing order.
- Separate compact streams when a pass needs only positions or only normals.
- Interleaved input when a single loop consumes every field.
- Small transformed batches with bounded temporary storage.
- Alignment appropriate to the transfer/operation, instead of padding every source vertex to 32 bytes indiscriminately.

GLdc’s array fast path processes batches of 60 vertices and separates attribute-copy/transform loops. The source explicitly identifies this as an empirically selected size. It is evidence for measuring chunk sizes, not proof that 60 is appropriate for every engine.[^17]

`PREF`, cache-line allocation, writeback, and invalidation are advanced tools. Prefetching too late or excessively can introduce stalls; allocating a line without fetching old content is suitable only when the complete relevant line is safely overwritten. Begin with well-behaved sequential data before experimenting with explicit cache control.[^13][^26]

### 6.4 Animation and lighting

Recommended cost controls are per-object light selection, baked static lighting, lower animation update rates for distant actors, fewer skin influences in distant LODs, and rigid attachment transforms where deformation is unnecessary. These are proposed policies; their benefit depends on scene content.

QuakeSpasm-DC provides a concrete quality/performance tradeoff: `PVR_LightVertex()` samples world lightmap information at vertices, folds in lightstyle and dynamic-light contributions, and renders texture modulation in a single geometry pass. This sacrifices per-pixel lightmap detail, particularly on large polygons, but avoids a separate lightmap geometry pass.[^25]

Use that approach for assets designed around vertex lighting. Preserve a higher-quality path for scenes where fine lighting detail materially affects the result.

## 7. Projection and clipping: keep the fast path valid

A typical native perspective path submits screen-space `x` and `y`, with reciprocal depth such as `1/w` in the PVR vertex’s depth field. Establish one projection convention and apply it consistently to clipping, depth comparison, fog, and viewport mapping. Orthographic rendering needs a deliberate depth mapping because its homogeneous `w` is constant.[^18]

Do not confuse viewport clipping with near-plane clipping. The TA’s tile handling does not supply a complete substitute for clipping geometry before the perspective divide.

Recommended processing branches:

| Classification | Action |
|---|---|
| Object outside view bounds | Reject before vertex processing |
| Object demonstrably clear of near plane | Use a fast transform/submission path |
| Triangle fully behind near plane | Reject |
| Triangle crossing near plane | Clip in the matching pre-divide space, interpolate attributes, then project |

QuakeSpasm-DC’s `pvr_clip.c` includes visibility masks and a separate clipping path. Its clipping expression matches its particular SH4ZAM projection. GLdc uses another convention, so mixing one engine’s matrix with the other’s clipping equation is unsafe.[^29][^18]

For an edge whose signed plane distances are `dA` and `dB`, a conventional intersection parameter is:

`t = dA / (dA - dB)`

Interpolate position and pre-divide attributes consistently. Protect degenerate cases in the clipping implementation. A reciprocal approximation that computes `1 / abs(x)` cannot replace signed division in this formula. Verify near-plane crossings, winding after clipping, and finite projected values before timing the fast path.

## 8. Opaque, cutout, translucent, and modifier lists

| Material/effect | Starting list or mechanism | Main consideration |
|---|---|---|
| Solid world and characters | `PVR_LIST_OP_POLY` | Opaque visibility removal and ordinary depth behavior |
| Binary cutouts | `PVR_LIST_PT_POLY` | Alpha threshold and edge appearance |
| Smoke, glass, fades | `PVR_LIST_TR_POLY` | Blending and ordering cost |
| Modifier effects | `PVR_LIST_OP_MOD` / `PVR_LIST_TR_MOD` | Additional volume geometry and compatible receiver state |

KOS exposes these as separate lists. Direct submission should group each list’s commands rather than repeatedly interleave incompatible list traffic.[^8][^2]

### 8.1 Transparency sorting is a policy decision

KOS can enable or disable translucent autosorting. In presorted mode, the application is responsible for suitable ordering. Hardware sorting is useful for complicated intersecting transparency, but its cost is not equivalent to opaque rendering.[^7]

Test both modes only with a scene where each produces the intended image. A UI or explicitly ordered effect may not need autosort; general intersecting 3D glass may. Turning sorting off and accepting incorrect blending is a quality change, not an equivalent optimization.

An important implementation distinction: at the inspected revision, **GLdc’s default configuration sets `autosort_enabled` to false**, whereas KOS’s default initialization leaves autosort enabled. GLdc’s public configuration also warns about depth-semantics consequences when autosort is enabled. Check the actual configuration rather than inferring behavior from hardware capability.[^30]

### 8.2 Budget translucent screen coverage

For proposed particle/effect experiments, record approximate screen coverage and overlap depth, not only particle count. Ten large smoke quads can represent more blending work than many tiny sparks.

Potential changes include shrinking transparent margins in textures, reducing particle size/count near the camera, using cutouts where appropriate, and reducing stacked full-screen overlays. Hardware sprites can reduce command/vertex preparation for appropriate quads, but cannot remove the cost of their covered pixels. The `sh4zam_pvr` sprite tutorials provide a focused implementation reference.[^24]

Avoid promising that punch-through is identical in cost to opaque rendering for every workload. Compare the actual alpha format, filtering, coverage, and depth behavior.

## 9. Texture conversion and bandwidth

### 9.1 Select format by content

| Format | Suitable starting use | Tradeoff |
|---|---|---|
| RGB565 | Opaque color textures | No alpha |
| ARGB1555 | Cutout surfaces | Binary alpha and reduced color precision |
| ARGB4444 | Soft transparency | Four bits per channel |
| 4-bit palette | Small color sets, stylized art, UI | Palette management and quantization |
| 8-bit palette | Broader indexed art | Larger palette footprint |
| PVR bump format | Selected bump effects | Specialized encoding and rendering path |

The Dreamcast-specific `texconv` documentation describes these formats and its texture containers. It is unrelated to Microsoft’s identically named DirectXTex utility.[^31]

**Recommended asset default:** convert static images before deployment and keep their native payload ready for upload. Loading a PNG is convenient for a demo, but decoding, format conversion, twiddling, or compression during gameplay consumes CPU time and temporary memory.

KOS provides raw, twiddling, and image-based upload paths. Its documentation distinguishes direct transfers from conversion paths; a loader that twiddles data cannot automatically use the same DMA/Store Queue route as an already prepared payload.[^32]

### 9.2 Twiddling and mipmaps

Twiddled texture layouts and mipmaps are useful candidates for world textures, especially minified or rotated surfaces. The bundled `pvrtex` supports offline twiddling, mipmap generation, VQ compression, previews, and small codebooks. Strided textures are useful for particular update/render-target workflows, but the tool documentation notes performance tradeoffs relative to twiddled textures.[^33]

A complete square mip chain adds approximately one third to the uncompressed texel count, calculated from `1 + 1/4 + 1/16 + ...`. Actual file/VRAM allocations include format-specific padding and metadata. Enable the corresponding sampling state; storing mip levels alone does not select them.

For atlases, generate padding and mip levels deliberately. Mip bleeding and UV seams can defeat an otherwise efficient material atlas. This is an asset-quality constraint to test with minification, not just a packing problem.

### 9.3 VQ’s real size benefit

For standard 16-bit VQ with a full 256-entry codebook and no mipmaps, the payload model is:

`VQ bytes = 2,048 + width × height / 4`

Each codebook entry represents a 2 × 2 block of 16-bit texels. The following values are calculated and exclude container headers and alignment:[^31]

| Texture | Uncompressed 16-bit | VQ with full codebook | Size reduction |
|---|---:|---:|---:|
| 32 × 32 | 2,048 B | 2,304 B | 12.5% larger |
| 64 × 64 | 8,192 B | 3,072 B | 62.5% smaller |
| 256 × 256 | 131,072 B | 18,432 B | 85.9% smaller |
| 512 × 512 | 524,288 B | 67,584 B | 87.1% smaller |

Thus the often quoted 8:1 ratio is an asymptotic result, not the ratio of every texture. Small-codebook formats can change the small-texture result. Loader support must match the chosen output container and codebook representation.

For a pre-sized square input, useful `pvrtex` experiments include:

```sh
pvrtex -i wall.png -o wall.dt -f rgb565 -c 256 -m
pvrtex -i smoke.png -o smoke.dt -f argb4444 -c 64 -m
```

These illustrate conversion options, not a complete engine loader. Compare the generated preview, loaded byte count, and render time. A `.dt` container is not itself a raw VRAM image.[^33]

### 9.4 Palette and global-state traps

The PVR palette table has 1,024 entries, and only one palette format is active at a time. KOS warns that filtered indexed textures using ARGB8888 palette entries can render at roughly half speed relative to the faster case. This is a specific documented performance trap worth an A/B test.[^34]

Palette banks, global texture stride, fog state, and other global settings require coordination across the deferred scene. An application API call is not proof that earlier geometry has finished using the affected resource. Use resource lifetime rules and the SDK’s synchronization rather than changing global state opportunistically while old frames are in flight.

## 10. Submission: direct rendering versus DMA

### 10.1 Direct Store Queue path

The SH4 Store Queues support 32-byte burst transfers. KOS documents their advantage for relatively small transfers with low setup overhead, while noting that DMA can be preferable for consistently large transfers.[^35]

For direct PVR rendering, current KOS exposes `pvr_dr_target()` and `pvr_dr_commit()`. The former alternates the queue target; the latter commits it. The destination is write-only from the application’s perspective. `pvr_send_to_ta()` is a separate 32-byte transfer path that does not consume the Store Queues.[^36]

An engine’s direct path should:

1. Prepare visibility, materials, and source streams.
2. Start the scene through the supported KOS lifecycle.
3. Open one polygon list and submit a cached material header.
4. Transform/project vertices and write the expected packet fields.
5. Terminate each strip with the correct end flag.
6. Finish lists and the scene.

Start with fully initialized packets. Advanced reuse of constant queue fields needs strict control over both queues and anything else that writes to them. The benchmark’s assumptions may not hold when textures, materials, or packet types alternate.

### 10.2 Buffered vertex DMA

Buffered submission can separate geometry generation from TA stalls and transfer larger contiguous blocks. It also introduces main-memory buffers, additional memory traffic, and lifetime constraints. The SH4ZAM DMA example includes in-buffer clipping and explicit list-buffer management.[^14]

KOS’s vertex-DMA mode requires an assigned vertex buffer for every enabled list. Its internal pipeline documentation describes an additional buffered stage and potential latency. Treat the diagram as a description of that implementation’s scheduling, not a fixed latency law for every custom DMA renderer.[^6][^7]

| Compare | Direct Store Queues | Buffered DMA |
|---|---|---|
| CPU output destination | TA-bound queues | Main-memory command buffers |
| Extra command storage | Low | Potentially substantial |
| Exposure to TA backpressure | Can stall vertex generation | Can move stalls away from generation |
| CPU work available during transfer | Depends on submission schedule | Can overlap independent work |
| Key measurement | CPU/registration time | Total throughput, buffer cost, and latency |

### 10.3 Transfer correctness is part of performance

KOS’s PVR DMA API requires 32-byte-aligned source and destination addresses and transfer sizes that are multiples of 32. Completion callbacks run in interrupt context. Particular PVR Store Queue upload functions explicitly prohibit concurrent PVR DMA transfers.[^37]

DMA and Store Queue operations do not make CPU cache coherency automatic in every custom path. Inspect which maintenance the chosen API performs. Preserve upload buffers until completion, and do not overwrite textures still referenced by an earlier rendering frame. `pvr_wait_render_done()` is a safe synchronization tool for the latter, but calling it unnecessarily can destroy useful overlap.[^2]

The better long-term design for frequently updated resources may be alternating buffers or delayed reuse. Confirm that the memory cost fits before adding a blanket wait or another copy.

## 11. VRAM and tile-bin budgeting

Texture memory competes with framebuffers, TA parameter storage, object-pointer bins, tile descriptors, and render targets. An “8 MiB texture budget” is therefore incorrect for a normal 3D scene.[^5][^6]

At 640 × 480 there are **300 tiles**, calculated as `20 × 15`. KOS bin-size constants describe word counts. For example, 16 words per tile for one enabled list reserves `300 × 16 × 4 = 19,200` bytes before overflow provisioning and multiple buffer sets.[^38]

Here is a **calculated planning example**, not a recommendation to hard-code these settings:

| Allocation | Assumption | Bytes |
|---|---|---:|
| Framebuffers | Two 640 × 480, 16-bit buffers | 1,228,800 |
| TA vertex/parameter buffers | Two allocations of 512 KiB | 1,048,576 |
| Object-pointer buffers | Three lists, 16 words/tile, three additional overflow sets, two buffered sets | 460,800 |
| Subtotal | Before tile descriptors, textures, render targets, alignment, and allocator overhead | 2,738,176 |

The modeled subtotal leaves approximately **5.39 MiB** of the 8 MiB pool before the omitted allocations. This illustrates why buffer sizing belongs in the content budget. Use actual KOS allocations and available-memory reports for the final numbers.

Tune with worst-case views:

- Too-small parameter storage or bins can cause missing geometry or tile-boundary flicker.
- Larger allocations consume texture capacity and may increase fragmentation pressure.
- Enabling unused lists reserves resources without helping the image.
- Large screen-spanning geometry and dense local geometry can stress bins differently even at the same triangle count.

`opb_overflow_count` specifically reserves extra bin sets. Track `vtx_buffer_used_max` alongside visual stress tests. That vertex-storage counter alone is not a complete measure of bin pressure.[^7][^39]

## 12. Effects that fit the platform

| Effect | Practical starting approach | Inspect |
|---|---|---|
| Static illumination | Baked vertex colors; evaluate lightmaps where detail warrants the pass | QuakeSpasm-DC `pvr_rsurf.c` |
| Local dynamic lights | Choose a small affecting-light set per object/cluster | DCUE’s specialized lighting routines |
| Contact/shadow volumes | PVR modifier facilities with constrained geometry | KOS `pvr/cheap_shadow/shadow.c`, `pvr/modifier_volume/modifier.c` |
| Projected character shadow | Small render target plus nearby receivers | Tony Hawk postmortem; KOS RTT sample |
| Bump detail | A restricted set of compatible surfaces | KOS `pvr/bumpmap/bump.c` |
| Fog | Hardware table fog where it matches the art direction | QuakeSpasm-DC `pvr_fog.c` |

KOS provides working modifier and cheap-shadow examples. Modifier volumes still require volume commands and correct receiver configuration; they should be profiled against simpler shadows.[^40][^41]

Its bump-map sample sets up distinct bump and visible-surface contexts. That is a useful reminder to inspect the actual passes behind an effect rather than interpreting hardware support as zero cost.[^42]

For render-to-texture, current KOS has `pvr_scene_begin_rtt()` with explicit dimensions and stride. The `pvr_rtt_sized` example demonstrates small targets. This matters because older advice and the deprecated wrapper assume screen-sized backing storage.[^43] A small reflection or shadow target can save memory and rendering work, but it also creates scheduling and resource dependencies.

## 13. GLdc-specific optimization and alternatives

### 13.1 Use the current implementation’s fast path

The GitHub GLdc README points to GitLab as upstream. The README’s feature checklist is not a reliable current feature matrix: the inspected tree already contains framebuffer and texture-matrix-related implementation files. Prefer the relevant source and sample for feature decisions.[^15][^30]

At the inspected revision, `_glIsVertexDataFastPathCompatible()` checks enabled attributes for these layouts:[^15]

| Enabled attribute | Checked format |
|---|---|
| Position | 3 × `GL_FLOAT` |
| UV | 2 × `GL_FLOAT` |
| Color | 4 × `GL_FLOAT` |
| Secondary color | 4 × `GL_FLOAT` |
| Second texture coordinate | 2 × `GL_FLOAT` |
| Normal | 3 × `GL_FLOAT` |

This does **not** establish that floats are universally the best storage format. They match this particular conversion path; a compact input format may save memory or win through another implementation. Native PVR packed colors and GLdc client-array formats are different layers.

### 13.2 Reduce wrapper work before replacing the wrapper

For an existing OpenGL-style renderer, test:

1. Arrays and fewer submissions instead of per-vertex immediate calls.
2. Offline strips and lower attribute-conversion cost.
3. Material grouping and fewer state changes.
4. Baked lighting or engine-selected lights where appropriate.
5. Avoiding accidental extra texture/effects passes.
6. Avoiding exact software scissoring in hot 3D batches unless required.

GLdc’s `draw.c` includes primitive expansion and exact-scissor handling, and its SH4 backend performs packet conversion and clipping. These are meaningful costs to inspect in a profile.[^16][^18]

### 13.3 Choosing an API boundary

| Approach | Best fit | Main tradeoff |
|---|---|---|
| GLdc throughout | Existing OpenGL 1.x-style engine, broad compatibility needs | Conversion and generic state management |
| Native KOS PVR backend | Engine designed around Dreamcast data and materials | More renderer ownership and validation |
| Portable engine with platform-specific backend | Shared asset/game code across systems | Must avoid forcing every backend into one inefficient representation |

QuakeSpasm-DC exposes both native-PVR and GLdc build options, making it a useful study target. Image output can differ between paths, so timing them is not automatically an equivalent-work comparison.[^19] If mixing APIs within one application, give one component explicit ownership of scene lifecycle and list ordering; do not insert raw TA traffic casually into another library’s queued scene.

## 14. A practical profiling and implementation plan

### 14.1 Build a useful capture first

Create a deterministic camera replay containing a dense world view, several animated objects, near-plane crossings, and the most expensive transparency effect. Record warm runs with the same build, assets, resolution, refresh mode, and output path. Keep console printing out of the measured inner loop.

Track:

| Metric | Purpose |
|---|---|
| Update, visibility, animation, transform, and submission CPU durations | Locates CPU work |
| Wait durations and wall frame interval | Separates idle/synchronization time from work |
| KOS registration and rendering durations | Helps locate pipeline pressure |
| Source/transformed/emitted vertex counts | Detects duplication and expansion |
| Headers, material changes, strips, average strip length | Measures batching quality |
| Clipped primitives and generated vertices | Detects near-plane/scissor cost |
| TA storage high-water mark and free VRAM | Exposes capacity limits |
| Texture upload bytes, CPU staging bytes, and transfer waits | Exposes streaming costs |

The inspected `pvr_stats_t` exposes `frame_last_time`, `reg_last_time`, `rnd_last_time`, `buf_last_time`, vertex-buffer use, and frame rate. Its documented timing units are **nanoseconds**; use `value / 1e6` to obtain milliseconds.[^39]

These are pipeline measurements and can refer to work completing in different stages. Do not add them blindly as independent pieces of one frame. Correlate captures with frame sequencing, especially after enabling DMA.

### 14.2 Use the right clock

KOS’s performance counters can measure operand-cache misses, instruction-cache misses, FPU-related stalls, and other CPU events. Its high-resolution performance-counter timer counts active CPU cycles and stops accumulating while the CPU sleeps. It is suitable for short work regions but may undercount wall time across blocking waits or frame intervals.[^44]

Use an elapsed-time clock that continues across sleep for end-to-end latency and waits. Treat the performance-counter API’s ownership rules carefully: KOS normally reserves the first counter for its timer. The higher-level `perf_monitor` interface is another way to organize named regions and selected events.[^45]

For function-level sampling, `dcprof` is a small MIT-licensed option. Its documented implementation samples saved thread contexts, currently focuses on the main thread, and has a scheduler-dependent sample rate. It can locate broad hot spots but should not be treated as a cycle-accurate microbenchmark.[^46]

### 14.3 Experiments that identify the bottleneck

Each row is a proposed diagnostic. Change one variable and retain an image/correctness check.

| Experiment | What a substantial improvement suggests | Caveat |
|---|---|---|
| Lower resolution with comparable content | Pixel/texture/render-target work matters | Tile/bin work also changes |
| Replace complex textures with small simple textures | Texture sampling/working set matters | Keep geometry and blending unchanged |
| Disable dynamic lighting | CPU light processing matters | A quality change used diagnostically |
| Freeze animation | Skinning/animation matters | Keep actor count and visibility fixed |
| Remove translucent effects | Blending, sorting, or effect geometry matters | Removal changes several costs |
| Cache output for a static diagnostic scene | CPU transformation/submission preparation matters | Added reads may mask savings |
| Switch equivalent mesh between lists and strips | Vertex traffic/processing matters | Include duplicated vertices and headers |
| Compare direct and DMA submission | TA stalls or transfer scheduling matter | Record latency and total buffer use |
| Change spatial/material batch size | Culling granularity versus setup cost matters | Test more than one camera |

If CPU time falls but displayed FPS remains fixed, check the refresh cap and whether another stage is now limiting. If render time stays high after major CPU simplification, further CPU assembly work may have little frame-time value.

### 14.4 Implementation sequence

**Stage A — establish correctness and baselines.** Run the official direct-strip and ordinary strip examples on the target hardware. Add the above metrics to the engine. Verify near clipping, transparency, texture formats, and worst-case storage before changing low-level scheduling.

**Stage B — fix the asset contract.** Produce native textures, material classification, discrete LODs, bounds, and strips offline. Store final runtime data in contiguous blocks with explicit counts and sizes. Keep a versioned file format and validate it during loading.

**Stage C — simplify the common renderer.** Cache headers, remove transient allocations, batch by compatible material within visible regions, and use SH4 matrix routines. Add separate common paths for rigid prelit geometry, lit rigid geometry, and animated geometry when profiles justify them.

**Stage D — optimize the measured limiting stage.** Compare cluster reuse, direct submission, DMA, texture choices, or effect reductions according to the diagnostic results. Retain the simpler implementation whenever the more complex one does not deliver a reproducible benefit.

**Stage E — protect the gain.** Repeat the deterministic replay and include quality screenshots, frame-time distributions, emitted vertex counts, storage high-water marks, and toolchain/repository identifiers with each result. Check several scenes and real hardware; emulator FPS is not a substitute for target timing.

### 14.5 Sample run order

With a configured KOS environment, these repository paths provide a focused progression:

```text
examples/dreamcast/pvr/pvrmark/
examples/dreamcast/pvr/pvrmark_strips/
examples/dreamcast/pvr/pvrmark_strips_direct/
examples/dreamcast/sh4zam/bruces_balls/
examples/dreamcast/gldc/benchmarks/quadmark/
examples/dreamcast/gldc/benchmarks/tristripmark/
examples/dreamcast/pvr/pvr_rtt_sized/
```

Build the relevant example with its supplied Makefile and installed dependencies. For the pinned KOS tree, a command pattern is:

```sh
make -C "$KOS_BASE/examples/dreamcast/pvr/pvrmark_strips_direct"
```

Bruce’s Balls reports a specialized ceiling of approximately 4.5 million polygons per second with suitable build flags. Its controlled geometry, material simplicity, and memory configuration make that useful as a benchmark reference, not a general game budget.[^13]

## 15. Versions, reuse, and limits of the evidence

### 15.1 Source snapshots

Repository state was checked for this report on **15 September 2026**. The following full commit identifiers make the implementation discussions reproducible:

| Project | Inspected commit | Commit date |
|---|---|---|
| KallistiOS | `fcfa7d869471591ca1c777543261a7bfea7cb726` | 14 September 2026 |
| GLdc | `aff72dc35d06a6eaed6d728547d728c897afe3c9` | 13 September 2026 |
| SH4ZAM | `0bacf4b336368c0b47864ce9eeb59e7c07904b51` | 12 September 2026 |
| QuakeSpasm-DC | `8a4eb1fccf62a76193237119b99b94317fe6a1be` | 20 July 2026 |
| DCUE | `c2cd09efd783b16ef520479ee0308e8371f6b37d` | 7 May 2025 |
| Simulant | `4a5f5799d0232c1a509b186262a214da0f138894` | 17 June 2026 |

### 15.2 Reuse status

| Project | Declared status in the inspected material |
|---|---|
| KallistiOS | BSD-like KOS license; distribution also contains other free-software licenses[^1] |
| GLdc | BSD-2-Clause license[^47] |
| SH4ZAM | MIT license[^3] |
| sh4zam_pvr | MIT license[^24] |
| QuakeSpasm-DC | GPLv2; game data supplied separately[^19] |
| Simulant | LGPL license text[^48] |
| dcprof | MIT license[^46] |
| DCUE | No top-level license found and repository license metadata was empty at the inspected snapshot; resolve reuse terms before incorporating its code |

The recommended starting stack can be assembled from independent public development tools. Historical commercial accounts are used to explain engineering decisions; proprietary Sega SDK code is not required to reproduce the proposed experiments.

### 15.3 What remains uncertain

There is no defensible universal triangle budget, Store Queue/DMA crossover size, ideal strip length, or optimum cluster size for a complete Dreamcast engine. The relevant variables include material complexity, lighting, skinning, projected area, transparency, memory traffic, compiler output, and CPU/GPU overlap.

The historical frame-rate results are self-reported. Modern source inspection establishes implementation choices and explicit API requirements, not their speed on an unmeasured game. The repositories and snippets were inspected; the examples were not compiled or timed on Dreamcast hardware for this report.

The first practical deliverable for an engine should therefore be **a reproducible capture that links frame time to processed vertices, submitted bytes, render time, and memory use**. That turns the techniques above into a sequence of measurable changes.

## Sources

Numbered entries also serve as Markdown footnotes. Undated development documentation is cited by the inspected snapshot or access date; historical articles retain their original publication dates.

[^1]: KallistiOS contributors. [KallistiOS README and licensing overview](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/README.md). Snapshot 14 September 2026. Independent SDK, examples, and license scope.

[^2]: KallistiOS contributors. [Scene Submission API](https://kos-docs.dreamcast.wiki/group__pvr__scene__mgmt.html). Accessed 15 September 2026. List grouping, lifecycle, buffering, and resource synchronization. The pinned implementation is [`pvr_scene.c`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/pvr/pvr_scene.c).

[^3]: Falco Girgis and SH4ZAM contributors. [SH4ZAM README](https://github.com/gyrovorbis/sh4zam/blob/0bacf4b336368c0b47864ce9eeb59e7c07904b51/README.md). Snapshot 12 September 2026. Math API, software backend, examples, and MIT license declaration.

[^4]: Marcus Comstedt. [Dreamcast Programming: Hitachi SuperH SH4](https://mc.pp.se/dc/cpu.html). Last modified 27 March 2000. CPU clock, registers, and cache capacities.

[^5]: Marcus Comstedt. [Dreamcast Programming: PowerVR](https://mc.pp.se/dc/pvr.html). Accessed 15 September 2026. Original hardware documentation covering tiles, visibility, buffering, and VRAM organization.

[^6]: Megan Potter and KallistiOS contributors. [`pvr_internal.h`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/pvr/pvr_internal.h). Snapshot 14 September 2026. Internal buffers and direct/DMA pipeline description.

[^7]: KallistiOS contributors. [`pvr.h`, initialization and vertex-DMA interfaces](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr.h). Snapshot 14 September 2026. List bins, autosort, double buffering, and overflow configuration.

[^8]: KallistiOS contributors. [PVR Headers API](https://kos-docs.dreamcast.wiki/group__pvr__primitives__headers.html). Accessed 15 September 2026. List types, depth modes, culling, and sampling constants.

[^9]: Sebastian Wloch, Kalisto Entertainment. [Optimizing Dreamcast Microsoft Direct3D Performance](https://www.gamesurge.com/dreamcast/technical_pages/optimizing.shtml). March 1999; Microsoft article preserved by GameSurge. Firsthand optimization account; historical API-specific guidance.

[^10]: Jamie Fristrom. [Postmortem: Treyarch’s Draconus](https://www.gamedeveloper.com/programming/postmortem-treyarch-s-i-draconus-i-). Game Developer, 14 August 2000. Progressive-mesh performance and renderer-data-structure lessons.

[^11]: Jamie Fristrom. [Postmortem: Treyarch’s Tony Hawk’s Pro Skater, Dreamcast Version](https://www.gamedeveloper.com/design/postmortem-treyarch-s-i-tony-hawk-s-pro-skater-i-dreamcast-version-). Game Developer, 28 June 2000. Worst-case optimization targets and projected shadows.

[^12]: Megan Potter, Falco Girgis, and KallistiOS contributors. [`pvrmark_strips_direct.c`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/pvrmark_strips_direct/pvrmark_strips_direct.c). Snapshot 14 September 2026. Direct strip generation and statistics.

[^13]: KallistiOS contributors. [`bruces_balls.c`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/sh4zam/bruces_balls/bruces_balls.c). Snapshot 14 September 2026. Specialized benchmark, transform scheduling, cache/queue techniques, and documented reciprocal limitations. [Generated documentation](https://kos-docs.dreamcast.wiki/bruces__balls_8c.html).

[^14]: SH4ZAM contributors. [`example/pvr_dma/pvr_dma.c`](https://github.com/gyrovorbis/sh4zam/blob/0bacf4b336368c0b47864ce9eeb59e7c07904b51/example/pvr_dma/pvr_dma.c). Snapshot 12 September 2026. Buffered geometry generation and near clipping.

[^15]: GLdc contributors. [`GL/attributes.c`](https://gitlab.com/simulant/GLdc/-/blob/aff72dc35d06a6eaed6d728547d728c897afe3c9/GL/attributes.c). Snapshot 13 September 2026. `_glIsVertexDataFastPathCompatible()` and input parsing. [Upstream repository](https://gitlab.com/simulant/GLdc).

[^16]: GLdc contributors. [`GL/draw.c`](https://gitlab.com/simulant/GLdc/-/blob/aff72dc35d06a6eaed6d728547d728c897afe3c9/GL/draw.c). Snapshot 13 September 2026. Indexed transforms, primitive expansion, headers, and exact scissoring.

[^17]: GLdc contributors. [`GL/draw_fastpath.inc`](https://gitlab.com/simulant/GLdc/-/blob/aff72dc35d06a6eaed6d728547d728c897afe3c9/GL/draw_fastpath.inc). Snapshot 13 September 2026. Batched attribute processing and 60-vertex batch selection.

[^18]: GLdc contributors. [`GL/platforms/sh4.c`](https://gitlab.com/simulant/GLdc/-/blob/aff72dc35d06a6eaed6d728547d728c897afe3c9/GL/platforms/sh4.c). Snapshot 13 September 2026. Projection, reciprocal-depth handling, clipping, and Store Queue submission.

[^19]: maximqaxd and QuakeSpasm contributors. [QuakeSpasm-DC README](https://github.com/maximqaxd/quakespasm/blob/8a4eb1fccf62a76193237119b99b94317fe6a1be/README.md). Snapshot 20 July 2026. Native renderer, GLdc alternative, dependencies, and license.

[^20]: Stefanos Kornilios Mitsis Poiitidis. [DCUE project description](https://skmp.dev/projects/dcue/). Project dated 2025; accessed 15 September 2026. Unity exporter, engine goals, and DCA3-derived renderer lineage.

[^21]: skmp. [`DCUE tools/repacker.cpp`](https://github.com/skmp/dcue/blob/c2cd09efd783b16ef520479ee0308e8371f6b37d/tools/repacker.cpp). Snapshot 7 May 2025. `process_mesh()`, stripping, cluster construction, index packing, and asset conversion.

[^22]: skmp. [`DCUE main.cpp`](https://github.com/skmp/dcue/blob/c2cd09efd783b16ef520479ee0308e8371f6b37d/main.cpp). Snapshot 7 May 2025. Specialized meshlet transformation and lighting routines.

[^23]: Simulant contributors. [`render_queue.cpp`](https://gitlab.com/simulant/simulant/-/blob/4a5f5799d0232c1a509b186262a214da0f138894/simulant/renderers/batching/render_queue.cpp) and [`gl1x_render_queue_visitor.cpp`](https://gitlab.com/simulant/simulant/-/blob/4a5f5799d0232c1a509b186262a214da0f138894/simulant/renderers/gl1x/gl1x_render_queue_visitor.cpp). Snapshot 17 June 2026. Render grouping and GL backend integration.

[^24]: Daniel Fairchild / dfchil. [sh4zam_pvr](https://github.com/dfchil/sh4zam_pvr). Accessed 15 September 2026. MIT-licensed examples and tutorials, including [sprites](https://github.com/dfchil/sh4zam_pvr/blob/main/docs/Part4_Sprites.md) and [per-vertex specular/OCRAM](https://github.com/dfchil/sh4zam_pvr/blob/main/docs/Part7_PerVertexSpecular.md).

[^25]: maximqaxd and QuakeSpasm contributors. [`Quake/pvr_rsurf.c`](https://github.com/maximqaxd/quakespasm/blob/8a4eb1fccf62a76193237119b99b94317fe6a1be/Quake/pvr_rsurf.c). Snapshot 20 July 2026. PVS/surface-chain integration and single-pass vertex-lighting tradeoffs.

[^26]: SH4ZAM contributors. [SH4 inline implementations](https://github.com/gyrovorbis/sh4zam/tree/0bacf4b336368c0b47864ce9eeb59e7c07904b51/include/sh4zam/inline/sh4), especially [`shz_xmtrx_sh4.inl.h`](https://github.com/gyrovorbis/sh4zam/blob/0bacf4b336368c0b47864ce9eeb59e7c07904b51/include/sh4zam/inline/sh4/shz_xmtrx_sh4.inl.h). Snapshot 12 September 2026. FPU math and hardware-specific implementation interfaces.

[^27]: KallistiOS contributors. [Matrices API](https://kos-docs.dreamcast.wiki/group__math__matrices.html). Accessed 15 September 2026. SH4 matrix and transform interfaces.

[^28]: KallistiOS contributors. [`environ_dreamcast.sh`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/environ_dreamcast.sh). Snapshot 14 September 2026. Toolchain configuration and target compilation options.

[^29]: maximqaxd and QuakeSpasm contributors. [`Quake/pvr_clip.c`](https://github.com/maximqaxd/quakespasm/blob/8a4eb1fccf62a76193237119b99b94317fe6a1be/Quake/pvr_clip.c). Snapshot 20 July 2026. Near-plane masks, signed edge intersection, and native vertex submission.

[^30]: GLdc contributors. [`GL/flush.c`](https://gitlab.com/simulant/GLdc/-/blob/aff72dc35d06a6eaed6d728547d728c897afe3c9/GL/flush.c) and [`include/GL/glkos.h`](https://gitlab.com/simulant/GLdc/-/blob/aff72dc35d06a6eaed6d728547d728c897afe3c9/include/GL/glkos.h). Snapshot 13 September 2026. Default configuration, autosort caveats, and queued scene lifecycle.

[^31]: tvspelsfreak. [Dreamcast texconv documentation](https://github.com/tvspelsfreak/texconv/blob/master/readme.txt). Accessed 15 September 2026. Pixel formats and standard VQ layout. Some older tool restrictions are superseded by pvrtex; use current tool documentation for supported combinations.

[^32]: KallistiOS contributors. [Texturing API](https://kos-docs.dreamcast.wiki/group__pvr__txr__mgmt.html). Accessed 15 September 2026. Texture upload, conversion, and transfer behavior.

[^33]: TapamN and KallistiOS contributors. [`pvrtex/readme_unformatted.txt`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/utils/pvrtex/readme_unformatted.txt). Tool originally credited 2023–2024; snapshot 14 September 2026. Conversion options, mipmaps, small codebooks, stride, and container formats.

[^34]: KallistiOS contributors. [`pvr_pal.h`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr/pvr_pal.h). Snapshot 14 September 2026. Global palette capacity/format and filtered ARGB8888 palette performance warning.

[^35]: KallistiOS contributors. [Store Queues API](https://kos-docs.dreamcast.wiki/group__store__queues.html). Accessed 15 September 2026. Transfer granularity, alignment, locking, and SQ/DMA tradeoffs.

[^36]: KallistiOS contributors. [Direct Rendering API](https://kos-docs.dreamcast.wiki/group__pvr__direct.html). Accessed 15 September 2026. Current `pvr_dr_target()`, `pvr_dr_commit()`, and `pvr_send_to_ta()` contracts.

[^37]: KallistiOS contributors. [`pvr_dma.h`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr/pvr_dma.h). Snapshot 14 September 2026. Alignment, sizes, callbacks, and prohibited overlapping operations.

[^38]: KallistiOS contributors. [`pvr_buffers.c`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/hardware/pvr/pvr_buffers.c). Snapshot 14 September 2026. Word-to-byte conversion, tile count, per-list bins, overflow multiplication, and buffered allocations.

[^39]: KallistiOS contributors. [`pvr_misc.h`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/pvr/pvr_misc.h) and [pvr_stats_t documentation](https://kos-docs.dreamcast.wiki/structpvr__stats__t.html). Snapshot 14 September 2026; documentation accessed 15 September 2026. Timing units and storage counters.

[^40]: KallistiOS contributors. [`cheap_shadow/shadow.c`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/cheap_shadow/shadow.c). Snapshot 14 September 2026. Cheap-shadow modifier configuration and example.

[^41]: KallistiOS contributors. [`modifier_volume/modifier.c`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/modifier_volume/modifier.c). Snapshot 14 September 2026. Modifier lists and volume submission.

[^42]: KallistiOS contributors. [`bumpmap/bump.c`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/bumpmap/bump.c). Snapshot 14 September 2026. Bump-map texture and surface passes.

[^43]: KallistiOS contributors. [`pvr_rtt_sized/pvr_rtt_sized.cc`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/examples/dreamcast/pvr/pvr_rtt_sized/pvr_rtt_sized.cc). Snapshot 14 September 2026. Explicit-size render targets and stride, paired with the Scene Submission API in source 2.

[^44]: KallistiOS contributors. [`perfctr.h`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/perfctr.h). Snapshot 14 September 2026. SH4 counter events, timer ownership, and active-cycle versus wall-time distinction.

[^45]: KallistiOS contributors. [`perf_monitor.h`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/include/dc/perf_monitor.h). Snapshot 14 September 2026. Named profiling regions and event monitoring.

[^46]: Luke Benstead / Simulant contributors. [dcprof README](https://gitlab.com/simulant/dcprof/-/blob/master/README.md). Accessed 15 September 2026. Sampling mechanism, usage, documented limitations, and MIT license declaration.

[^47]: GLdc contributors. [GLdc LICENSE](https://gitlab.com/simulant/GLdc/-/blob/aff72dc35d06a6eaed6d728547d728c897afe3c9/LICENSE). Snapshot 13 September 2026. BSD-2-Clause terms.

[^48]: Simulant contributors. [Simulant LICENSE](https://gitlab.com/simulant/simulant/-/blob/4a5f5799d0232c1a509b186262a214da0f138894/LICENSE). Snapshot 17 June 2026. GNU Lesser General Public License text.

[^49]: Renesas. [SH7750, SH7750S, SH7750R Group User’s Manual: Hardware](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware). Revision 7.02, 24 September 2013, Section 4.1.1 and Table 4.1, printed page 111. Cache mapping, line size, capacity, and on-chip RAM configuration.
