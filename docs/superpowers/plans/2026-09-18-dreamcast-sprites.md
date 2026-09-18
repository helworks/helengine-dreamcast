# Dreamcast Textures and Sprites Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the Dreamcast a cooked PVR texture pipeline and a sprite drawing path, proven by rendering the console instruction Blueprint the N64 runtime already draws.

**Architecture:** The builder converts each engine-cooked `TextureAsset` into a `dc-texture-v1` blob holding pre-twiddled ARGB4444 pixels, so the runtime only copies bytes into video memory. The existing authored frame assembler gains a sprite list alongside its geometry, and presents both through one sink call: the opaque 3D list first, then the PVR translucent list with depth writes off.

**Tech Stack:** C# 12 / .NET 9 for the builder and its xUnit tests; C++17 for host-testable platform code and C++20 for the SH4 build; KallistiOS PVR API; Docker SDK image `helengine-dc-sdk:v01`.

**Spec:** `docs/superpowers/specs/2026-09-17-dreamcast-sprites-design.md`

## Global Constraints

- Never hand-edit generated C++. Fixes are generic, in configuration or generator source.
- Platform sources under `src/` compile with `-Wall -Wextra -Werror`. Only generated translation units may demote warnings, via `GENERATED_WARNING_FLAGS` in the `Makefile`.
- Host-testable types carry no generated headers. Anything including a generated header goes behind `#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)`.
- `DreamcastAuthoredSnapshot` must remain exactly 64 bytes; its `static_assert` stays.
- Texture format is ARGB4444, twiddled, single format. No other format is emitted.
- Sprites use `PVR_LIST_TR_POLY` with depth writes disabled, drawn in submission order.
- Maximum 64 sprites per frame. Maximum padded texture axis 1024.
- The 3D material schema is unchanged. `dc-standard-v1` keeps rejecting texture bindings.
- Run builder tests with: `dotnet test builder.tests/helengine.dreamcast.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-dc/v02/dotnet-main -p:HelengineRoot=C:/dev/helworks/helengine -v:minimal`
- Record the branch of the shared `C:/dev/helworks/helengine` checkout alongside any test result, because it is shared with other sessions.

## Plan-level decision: square padding

The spec says pad up to the next power of two on each axis. This plan pads to the
next power of two **square**. Reason: PVR twiddling of a non-square texture
processes square sub-blocks of side `min(w, h)`, which is materially harder to
verify by hand, and the spec requires the twiddle test to assert against a
hand-computed Morton order rather than a round trip. Square padding reduces
twiddling to a plain bit interleave. The cost is wasted video memory on very
oblong art; the target icons are roughly square. This is recorded in Task 2's
cooker documentation so the trade is visible at the call site.

## File Structure

**Builder, C#**

- Create `builder/DreamcastTextureTwiddle.cs` — pure Morton interleave. One public static method, no I/O.
- Create `builder/DreamcastTextureSchemaIds.cs` — format id, magic, version, limits.
- Create `builder/DreamcastTextureCooker.cs` — `TextureAsset` to `dc-texture-v1` bytes; conversion, square padding, validation.
- Modify `builder/DreamcastContentStager.cs` — accept `TextureAsset`, route it through the cooker, count converted bytes against the payload budget.
- Modify `builder/DreamcastPlatformDefinitionFactory.cs` — component support rules for sprite, text and rounded rectangle.
- Create `builder.tests/DreamcastTextureTwiddleTests.cs`, `builder.tests/DreamcastTextureCookerTests.cs`.
- Modify `builder.tests/DreamcastContentStagerTests.cs`, `builder.tests/DreamcastPlatformDefinitionTests.cs`.

**Runtime, C++**

- Create `src/platform/dreamcast/DreamcastSpriteFrame.hpp` — `AuthoredSpriteState`, `SpriteFrame`, `kMaxSprites`. No generated headers, host-testable.
- Create `src/platform/dreamcast/DreamcastTextureFormat.hpp/.cpp` — parse the `dc-texture-v1` header. No generated headers, host-testable.
- Create `src/platform/dreamcast/DreamcastRuntimeTexture.hpp/.cpp` — `RuntimeTexture` subclass holding the video memory pointer. Generated headers, guarded.
- Modify `src/platform/dreamcast/DreamcastAuthoredFrame.hpp/.cpp` — `addSprite`, new statuses, sink carries both lists.
- Modify `src/platform/dreamcast/DreamcastRasterRenderer.hpp/.cpp` — textured translucent pass.
- Modify `src/platform/dreamcast/DreamcastRenderManager2D.hpp/.cpp` — `BuildTextureFromCooked`, `DrawSprite`, `ReleaseTexture`.
- Modify `src/platform/dreamcast/DreamcastRenderManager3D.hpp/.cpp` — assembler injected rather than owned.
- Modify `src/platform/dreamcast/DreamcastRuntimeHost.hpp/.cpp` — own the assembler, inject into both managers, snapshot version 3.
- Create `tests/native/DreamcastSpriteSubmissionTests.cpp`, `tests/native/DreamcastTextureFormatTests.cpp`.
- Modify `Makefile`, `scripts/verify-authored-scene.ps1`.

**Shared project**

- Modify `C:/dev/helprojs/demodisc/assets/codebase/rendering.tools/CubeTestSceneFactory.cs` — remove `dc` from `SpritelessRuntimeExcludedPlatformIds`. That repository is shared; leave its other pending changes alone and commit only this file.

## The `dc-texture-v1` binary layout

Little endian, 16-byte header then pixels. Every task that reads or writes this
format uses exactly these offsets.

| offset | size | field |
|---|---|---|
| 0 | 4 | magic `D` `C` `T` `X` |
| 4 | 2 | version, currently 1 |
| 6 | 2 | flags, must be 0 |
| 8 | 2 | padded side, power of two, square |
| 10 | 2 | used width |
| 12 | 2 | used height |
| 14 | 2 | reserved, written as zero, ignored by readers |
| 16 | side × side × 2 | ARGB4444, twiddled |

---

### Task 1: Twiddle function

**Files:**
- Create: `builder/DreamcastTextureTwiddle.cs`
- Test: `builder.tests/DreamcastTextureTwiddleTests.cs`

**Interfaces:**
- Consumes: nothing.
- Produces: `public static class DreamcastTextureTwiddle` with `public static int TwiddledIndex(int x, int y)` and `public static ushort[] Twiddle(ushort[] linear, int side)`.

- [ ] **Step 1: Write the failing test**

Create `builder.tests/DreamcastTextureTwiddleTests.cs`:

```csharp
using helengine.dreamcast.builder;

namespace helengine.dreamcast.builder.tests;

/// <summary>Verifies the PVR Morton interleave against hand-computed values.</summary>
public sealed class DreamcastTextureTwiddleTests {
    /// <summary>Confirms the interleave puts y in even bits and x in odd bits.</summary>
    [Theory]
    [InlineData(0, 0, 0)]
    [InlineData(0, 1, 1)]
    [InlineData(1, 0, 2)]
    [InlineData(1, 1, 3)]
    [InlineData(0, 2, 4)]
    [InlineData(2, 0, 8)]
    [InlineData(3, 3, 15)]
    [InlineData(0, 3, 5)]
    [InlineData(3, 0, 10)]
    public void TwiddledIndex_MatchesHandComputedMortonOrder(int x, int y, int expected) {
        Assert.Equal(expected, DreamcastTextureTwiddle.TwiddledIndex(x, y));
    }

    /// <summary>Confirms a 4x4 image is reordered exactly, not merely permuted.</summary>
    [Fact]
    public void Twiddle_ReordersAFourByFourImageExactly() {
        ushort[] linear = new ushort[16];
        for (int i = 0; i < 16; i++) {
            linear[i] = (ushort)(i + 1);
        }

        ushort[] twiddled = DreamcastTextureTwiddle.Twiddle(linear, 4);

        // linear[y * 4 + x] must land at TwiddledIndex(x, y).
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                Assert.Equal(linear[y * 4 + x], twiddled[DreamcastTextureTwiddle.TwiddledIndex(x, y)]);
            }
        }
        Assert.Equal(16, twiddled.Length);
    }

    /// <summary>Confirms a non-square or non-power-of-two side is rejected rather than silently mishandled.</summary>
    [Theory]
    [InlineData(3)]
    [InlineData(0)]
    [InlineData(6)]
    public void Twiddle_RejectsASideThatIsNotAPositivePowerOfTwo(int side) {
        ushort[] linear = new ushort[16];

        ArgumentOutOfRangeException exception =
            Assert.Throws<ArgumentOutOfRangeException>(() => DreamcastTextureTwiddle.Twiddle(linear, side));

        Assert.Equal("side", exception.ParamName);
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `dotnet test builder.tests/helengine.dreamcast.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-dc/v02/dotnet-main -p:HelengineRoot=C:/dev/helworks/helengine -v:minimal`

Expected: FAIL to compile, `DreamcastTextureTwiddle` does not exist.

- [ ] **Step 3: Write the implementation**

Create `builder/DreamcastTextureTwiddle.cs`:

```csharp
namespace helengine.dreamcast.builder;

/// <summary>
/// Reorders a square power-of-two image into the PVR's twiddled (Morton) order.
/// The convention is y in the even bit positions and x in the odd ones, which is
/// what the PVR expects for a texture flagged PVR_TXRFMT_TWIDDLED.
/// </summary>
public static class DreamcastTextureTwiddle {
    /// <summary>Returns the twiddled element index for a linear coordinate pair.</summary>
    /// <param name="x">Column, zero based.</param>
    /// <param name="y">Row, zero based.</param>
    /// <returns>Index into the twiddled array.</returns>
    public static int TwiddledIndex(int x, int y) {
        if (x < 0) {
            throw new ArgumentOutOfRangeException(nameof(x));
        } else if (y < 0) {
            throw new ArgumentOutOfRangeException(nameof(y));
        }

        int result = 0;
        for (int bit = 0; bit < 16; bit++) {
            result |= ((y >> bit) & 1) << (2 * bit);
            result |= ((x >> bit) & 1) << ((2 * bit) + 1);
        }
        return result;
    }

    /// <summary>Reorders a square power-of-two image into twiddled order.</summary>
    /// <param name="linear">Row-major pixels, length side * side.</param>
    /// <param name="side">Square side, a positive power of two.</param>
    /// <returns>A new array in twiddled order.</returns>
    public static ushort[] Twiddle(ushort[] linear, int side) {
        if (linear == null) {
            throw new ArgumentNullException(nameof(linear));
        } else if (side <= 0 || (side & (side - 1)) != 0) {
            throw new ArgumentOutOfRangeException(nameof(side), side, "Side must be a positive power of two.");
        } else if (linear.Length != side * side) {
            throw new ArgumentException($"Expected {side * side} pixels, received {linear.Length}.", nameof(linear));
        }

        ushort[] twiddled = new ushort[linear.Length];
        for (int y = 0; y < side; y++) {
            for (int x = 0; x < side; x++) {
                twiddled[TwiddledIndex(x, y)] = linear[(y * side) + x];
            }
        }
        return twiddled;
    }
}
```

- [ ] **Step 4: Run the test to verify it passes**

Run the same command. Expected: PASS.

- [ ] **Step 5: Mutation-verify the test**

Temporarily swap the two shift expressions in `TwiddledIndex` so x goes to even bits and y to odd. Re-run the tests and confirm `TwiddledIndex_MatchesHandComputedMortonOrder` FAILS on the asymmetric rows, specifically `(0,1)` and `(1,0)`. Then restore the original. A test that only used symmetric pairs would pass against the transposed function, which is the bug this guards.

- [ ] **Step 6: Commit**

```bash
git add builder/DreamcastTextureTwiddle.cs builder.tests/DreamcastTextureTwiddleTests.cs
git commit -m "feat(dc): add the PVR twiddle reorder for square textures"
```

---

### Task 2: Texture cooker

**Files:**
- Create: `builder/DreamcastTextureSchemaIds.cs`, `builder/DreamcastTextureCooker.cs`
- Test: `builder.tests/DreamcastTextureCookerTests.cs`

**Interfaces:**
- Consumes: `DreamcastTextureTwiddle.Twiddle(ushort[], int)` from Task 1.
- Produces: `public sealed class DreamcastTextureCooker` with `public byte[] Cook(helengine.TextureAsset asset)`. Constants on `DreamcastTextureSchemaIds`: `FormatId = "dc-texture-v1"`, `Magic = 0x58544344u`, `Version = (ushort)1`, `MaximumSide = 1024`, `HeaderBytes = 16`.

- [ ] **Step 1: Write the failing test**

Create `builder.tests/DreamcastTextureCookerTests.cs`:

```csharp
using helengine.dreamcast.builder;

namespace helengine.dreamcast.builder.tests;

/// <summary>Verifies conversion, square padding and rejection of unsupported texture input.</summary>
public sealed class DreamcastTextureCookerTests {
    /// <summary>Confirms the header records the padded side and the original used extent.</summary>
    [Fact]
    public void Cook_PadsToSquarePowerOfTwoAndRecordsTheUsedExtent() {
        byte[] cooked = new DreamcastTextureCooker().Cook(CreateRgba32(3, 2));

        Assert.Equal((uint)DreamcastTextureSchemaIds.Magic, BitConverter.ToUInt32(cooked, 0));
        Assert.Equal((ushort)1, BitConverter.ToUInt16(cooked, 4));
        Assert.Equal((ushort)0, BitConverter.ToUInt16(cooked, 6));
        Assert.Equal((ushort)4, BitConverter.ToUInt16(cooked, 8));
        Assert.Equal((ushort)3, BitConverter.ToUInt16(cooked, 10));
        Assert.Equal((ushort)2, BitConverter.ToUInt16(cooked, 12));
        Assert.Equal(DreamcastTextureSchemaIds.HeaderBytes + (4 * 4 * 2), cooked.Length);
    }

    /// <summary>Confirms an opaque white source pixel becomes ARGB4444 0xFFFF at its twiddled position.</summary>
    [Fact]
    public void Cook_ConvertsOpaqueWhiteToArgb4444() {
        helengine.TextureAsset asset = CreateRgba32(1, 1);
        asset.Colors = [0xFF, 0xFF, 0xFF, 0xFF];

        byte[] cooked = new DreamcastTextureCooker().Cook(asset);

        int offset = DreamcastTextureSchemaIds.HeaderBytes + (DreamcastTextureTwiddle.TwiddledIndex(0, 0) * 2);
        Assert.Equal((ushort)0xFFFF, BitConverter.ToUInt16(cooked, offset));
    }

    /// <summary>Confirms padding is transparent black so no colour can bleed at the sampled edge.</summary>
    [Fact]
    public void Cook_FillsPaddingWithTransparentBlack() {
        // 3x2 pads to a 4x4 square, so (3,3) is padding while (2,1) is real.
        // Every source pixel is opaque white, so a zero can only come from padding.
        helengine.TextureAsset asset = CreateRgba32(3, 2);
        Array.Fill(asset.Colors, (byte)0xFF);

        byte[] cooked = new DreamcastTextureCooker().Cook(asset);

        int padding = DreamcastTextureSchemaIds.HeaderBytes + (DreamcastTextureTwiddle.TwiddledIndex(3, 3) * 2);
        int real = DreamcastTextureSchemaIds.HeaderBytes + (DreamcastTextureTwiddle.TwiddledIndex(2, 1) * 2);
        Assert.Equal((ushort)0x0000, BitConverter.ToUInt16(cooked, padding));
        Assert.Equal((ushort)0xFFFF, BitConverter.ToUInt16(cooked, real));
    }

    /// <summary>Confirms a source larger than the PVR limit is rejected by name.</summary>
    [Fact]
    public void Cook_RejectsASourceAboveTheHardwareLimit() {
        helengine.TextureAsset asset = CreateRgba32(1025, 4);
        asset.Id = "textures/huge";

        InvalidOperationException exception =
            Assert.Throws<InvalidOperationException>(() => new DreamcastTextureCooker().Cook(asset));

        Assert.Contains("textures/huge", exception.Message, StringComparison.Ordinal);
        Assert.Contains("1024", exception.Message, StringComparison.Ordinal);
    }

    /// <summary>Confirms a zero-sized source is rejected rather than producing an empty texture.</summary>
    [Theory]
    [InlineData(0, 4)]
    [InlineData(4, 0)]
    public void Cook_RejectsAZeroSizedSource(int width, int height) {
        helengine.TextureAsset asset = CreateRgba32(width, height);

        Assert.Throws<InvalidOperationException>(() => new DreamcastTextureCooker().Cook(asset));
    }

    /// <summary>Confirms colour formats this platform cannot consume are rejected by name.</summary>
    [Theory]
    [InlineData(helengine.TextureAssetColorFormat.Indexed4)]
    [InlineData(helengine.TextureAssetColorFormat.Indexed8)]
    [InlineData(helengine.TextureAssetColorFormat.GxRgb5A3)]
    public void Cook_RejectsUnsupportedColourFormats(helengine.TextureAssetColorFormat format) {
        helengine.TextureAsset asset = CreateRgba32(2, 2);
        asset.ColorFormat = format;

        InvalidOperationException exception =
            Assert.Throws<InvalidOperationException>(() => new DreamcastTextureCooker().Cook(asset));

        Assert.Contains(format.ToString(), exception.Message, StringComparison.Ordinal);
    }

    static helengine.TextureAsset CreateRgba32(int width, int height) {
        return new helengine.TextureAsset {
            Id = "textures/test",
            Width = (ushort)width,
            Height = (ushort)height,
            ColorFormat = helengine.TextureAssetColorFormat.Rgba32,
            AlphaPrecision = helengine.TextureAssetAlphaPrecision.A8,
            Colors = new byte[Math.Max(0, width * height * 4)]
        };
    }
}
```

- [ ] **Step 2: Run the test to verify it fails**

Run the builder test command. Expected: FAIL to compile, `DreamcastTextureCooker` does not exist.

- [ ] **Step 3: Write the schema ids**

Create `builder/DreamcastTextureSchemaIds.cs`:

```csharp
namespace helengine.dreamcast.builder;

/// <summary>Stable identifiers and limits for the Dreamcast cooked texture format.</summary>
public static class DreamcastTextureSchemaIds {
    /// <summary>Format identifier recorded in build reports and diagnostics.</summary>
    public const string FormatId = "dc-texture-v1";

    /// <summary>Header magic, the ASCII bytes D C T X in little-endian order.</summary>
    public const uint Magic = 0x58544344u;

    /// <summary>Current format version.</summary>
    public const ushort Version = 1;

    /// <summary>Header size in bytes, before the pixel payload.</summary>
    public const int HeaderBytes = 16;

    /// <summary>Largest padded square side the PVR can sample.</summary>
    public const int MaximumSide = 1024;
}
```

- [ ] **Step 4: Write the cooker**

Create `builder/DreamcastTextureCooker.cs`:

```csharp
namespace helengine.dreamcast.builder;

/// <summary>
/// Converts an engine-cooked texture into the Dreamcast's twiddled ARGB4444 form.
/// Padding is to a square power of two rather than per axis: the PVR twiddles a
/// non-square texture in square sub-blocks, which is materially harder to verify,
/// and square padding reduces the reorder to a plain bit interleave.
/// </summary>
public sealed class DreamcastTextureCooker {
    /// <summary>Cooks one texture asset into dc-texture-v1 bytes.</summary>
    /// <param name="asset">Engine-cooked source texture.</param>
    /// <returns>Header followed by twiddled ARGB4444 pixels.</returns>
    public byte[] Cook(helengine.TextureAsset asset) {
        if (asset == null) {
            throw new ArgumentNullException(nameof(asset));
        }

        string name = string.IsNullOrWhiteSpace(asset.Id) ? "<unnamed>" : asset.Id;
        if (asset.ColorFormat != helengine.TextureAssetColorFormat.Rgba32 &&
            asset.ColorFormat != helengine.TextureAssetColorFormat.Rgba4444) {
            throw new InvalidOperationException(
                $"Dreamcast {DreamcastTextureSchemaIds.FormatId} cannot consume texture '{name}' in format {asset.ColorFormat}; expected Rgba32 or Rgba4444.");
        }
        if (asset.Width == 0 || asset.Height == 0) {
            throw new InvalidOperationException(
                $"Dreamcast {DreamcastTextureSchemaIds.FormatId} rejects zero-sized texture '{name}' ({asset.Width}x{asset.Height}).");
        }

        int side = NextPowerOfTwo(Math.Max(asset.Width, asset.Height));
        if (side > DreamcastTextureSchemaIds.MaximumSide) {
            throw new InvalidOperationException(
                $"Dreamcast {DreamcastTextureSchemaIds.FormatId} rejects texture '{name}': padded side {side} exceeds the hardware limit of {DreamcastTextureSchemaIds.MaximumSide}.");
        }

        ushort[] linear = new ushort[side * side];
        for (int y = 0; y < asset.Height; y++) {
            for (int x = 0; x < asset.Width; x++) {
                linear[(y * side) + x] = ReadPixel(asset, x, y);
            }
        }

        ushort[] twiddled = DreamcastTextureTwiddle.Twiddle(linear, side);
        byte[] cooked = new byte[DreamcastTextureSchemaIds.HeaderBytes + (twiddled.Length * 2)];
        BitConverter.TryWriteBytes(cooked.AsSpan(0), DreamcastTextureSchemaIds.Magic);
        BitConverter.TryWriteBytes(cooked.AsSpan(4), DreamcastTextureSchemaIds.Version);
        BitConverter.TryWriteBytes(cooked.AsSpan(6), (ushort)0);
        BitConverter.TryWriteBytes(cooked.AsSpan(8), (ushort)side);
        BitConverter.TryWriteBytes(cooked.AsSpan(10), asset.Width);
        BitConverter.TryWriteBytes(cooked.AsSpan(12), asset.Height);
        BitConverter.TryWriteBytes(cooked.AsSpan(14), (ushort)0);
        for (int i = 0; i < twiddled.Length; i++) {
            BitConverter.TryWriteBytes(cooked.AsSpan(DreamcastTextureSchemaIds.HeaderBytes + (i * 2)), twiddled[i]);
        }
        return cooked;
    }

    /// <summary>Reads one source pixel and returns it as ARGB4444.</summary>
    static ushort ReadPixel(helengine.TextureAsset asset, int x, int y) {
        if (asset.ColorFormat == helengine.TextureAssetColorFormat.Rgba4444) {
            int offset = ((y * asset.Width) + x) * 2;
            ushort rgba = BitConverter.ToUInt16(asset.Colors, offset);
            // Source is RGBA4444; the PVR wants ARGB4444, so rotate the alpha nibble to the top.
            return (ushort)(((rgba & 0x000F) << 12) | ((rgba & 0xFFF0) >> 4));
        }

        int index = ((y * asset.Width) + x) * 4;
        int red = asset.Colors[index] >> 4;
        int green = asset.Colors[index + 1] >> 4;
        int blue = asset.Colors[index + 2] >> 4;
        int alpha = asset.Colors[index + 3] >> 4;
        return (ushort)((alpha << 12) | (red << 8) | (green << 4) | blue);
    }

    /// <summary>Returns the smallest power of two greater than or equal to the value.</summary>
    static int NextPowerOfTwo(int value) {
        int result = 1;
        while (result < value) {
            result <<= 1;
        }
        return result;
    }
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run the builder test command. Expected: PASS, all suites.

- [ ] **Step 6: Commit**

```bash
git add builder/DreamcastTextureSchemaIds.cs builder/DreamcastTextureCooker.cs builder.tests/DreamcastTextureCookerTests.cs
git commit -m "feat(dc): cook engine textures into twiddled ARGB4444"
```

---

### Task 3: Stage cooked textures

**Files:**
- Modify: `builder/DreamcastContentStager.cs`
- Test: `builder.tests/DreamcastContentStagerTests.cs`

**Interfaces:**
- Consumes: `DreamcastTextureCooker.Cook(helengine.TextureAsset)` from Task 2.
- Produces: staged `dc-texture-v1` files at the artifact's relative path.

**Context the implementer needs.** `IsRuntimeLoadable` at `builder/DreamcastContentStager.cs:111` currently returns true only for `SceneAsset`, `ModelAsset` and `PlatformMaterialAsset`, which is why the demodisc instruction textures are skipped today. The staging loop copies bytes verbatim and then calls `VerifyHash(artifact, destinationPath)`. **A converted texture will not match the declared artifact hash**, because the staged bytes are no longer the source bytes. Verify the hash against the source file before conversion, and do not verify the converted output.

- [ ] **Step 1: Write the failing test**

Add to `builder.tests/DreamcastContentStagerTests.cs`:

```csharp
    /// <summary>Confirms a cooked texture is converted into the Dreamcast format rather than skipped.</summary>
    [Fact]
    public void Stage_ConvertsCookedTexturesIntoTheDreamcastFormat() {
        using DreamcastCookedTestContent content = DreamcastCookedTestContent.CreateWithTexture();

        IReadOnlyList<string> stagedPaths = new DreamcastContentStager().Stage(
            content.Manifest, content.SourceRoot, content.DestinationRoot, CancellationToken.None);

        string staged = Path.Combine(content.DestinationRoot, "textures", "icon.hasset");
        Assert.Contains("textures/icon.hasset", stagedPaths);
        Assert.True(File.Exists(staged));
        byte[] bytes = File.ReadAllBytes(staged);
        Assert.Equal(DreamcastTextureSchemaIds.Magic, BitConverter.ToUInt32(bytes, 0));
    }
```

Add a `CreateWithTexture` factory to `builder.tests/DreamcastCookedTestContent.cs` following the existing factories in that file, declaring one cooked artifact at `textures/icon.hasset` whose bytes are a serialized `helengine.TextureAsset` of 3 by 2 pixels in `Rgba32`, with the manifest hash computed over those source bytes.

- [ ] **Step 2: Run the test to verify it fails**

Run the builder test command. Expected: FAIL, the staged file does not exist because the texture was skipped.

- [ ] **Step 3: Implement the conversion**

In `builder/DreamcastContentStager.cs`, extend the loadable set and branch the copy. Replace the body of `IsRuntimeLoadable` so it also accepts `TextureAsset`:

```csharp
            return asset is SceneAsset || asset is ModelAsset || asset is PlatformMaterialAsset || asset is TextureAsset;
```

In the staging loop, before the verbatim copy, add the texture branch. Verify the source hash first, then write converted bytes and skip the output hash check:

```csharp
            if (TryReadTexture(sourcePath, out helengine.TextureAsset? texture)) {
                VerifyHash(artifact, sourcePath);
                byte[] converted = new DreamcastTextureCooker().Cook(texture!);
                if (converted.Length > MaximumPayloadBytes || converted.Length > MaximumPayloadBytes - totalBytes) {
                    throw new InvalidOperationException("Dreamcast ROMFS payload exceeds the one MiB limit.");
                }
                Directory.CreateDirectory(Path.GetDirectoryName(destinationPath)!);
                File.WriteAllBytes(destinationPath, converted);
                totalBytes += converted.Length;
                File.SetLastWriteTimeUtc(destinationPath, BuildEpochUtc);
                stagedPaths.Add(relativePath);
                continue;
            }
```

Add the helper beside `IsRuntimeLoadable`:

```csharp
    /// <summary>Returns the deserialized texture when the file is one, otherwise false.</summary>
    static bool TryReadTexture(string sourcePath, out helengine.TextureAsset? texture) {
        texture = null;
        byte[] bytes = File.ReadAllBytes(sourcePath);
        if (bytes.Length < 4 || bytes[0] != (byte)'H' || bytes[1] != (byte)'E' || bytes[2] != (byte)'L' || bytes[3] != (byte)'E') {
            return false;
        }
        try {
            texture = AssetSerializer.DeserializeFromBytes(bytes) as helengine.TextureAsset;
            return texture != null;
        } catch (Exception exception) when (exception is not OutOfMemoryException) {
            return false;
        }
    }
```

- [ ] **Step 4: Run the tests to verify they pass**

Run the builder test command. Expected: PASS. The existing stager tests must still pass, including the skip behaviour for genuinely unloadable artifacts.

- [ ] **Step 5: Commit**

```bash
git add builder/DreamcastContentStager.cs builder.tests/DreamcastContentStagerTests.cs builder.tests/DreamcastCookedTestContent.cs
git commit -m "feat(dc): stage cooked textures through the Dreamcast converter"
```

---

### Task 4: Component support rules

**Files:**
- Modify: `builder/DreamcastPlatformDefinitionFactory.cs`
- Test: `builder.tests/DreamcastPlatformDefinitionTests.cs`

**Interfaces:**
- Consumes: nothing.
- Produces: rules readable through `builder.Definition.ComponentSupportRules`.

**Context.** The existing rules are at `builder/DreamcastPlatformDefinitionFactory.cs:68-73` and use `PlatformComponentSupportKind.Transform` for supported components and `Unsupported` for rejected ones. Follow that shape exactly.

- [ ] **Step 1: Write the failing test**

Add to `builder.tests/DreamcastPlatformDefinitionTests.cs`:

```csharp
    /// <summary>Confirms sprites are transformed through while text and rounded rectangles are rejected.</summary>
    [Fact]
    public void DefinitionSupportsSpritesAndRejectsTextAndRoundedRects() {
        DreamcastPlatformAssetBuilder builder = new();

        PlatformComponentSupportRule sprite = Assert.Single(builder.Definition.ComponentSupportRules, rule => rule.ComponentTypeId == "helengine.SpriteComponent");
        PlatformComponentSupportRule text = Assert.Single(builder.Definition.ComponentSupportRules, rule => rule.ComponentTypeId == "helengine.TextComponent");
        PlatformComponentSupportRule rounded = Assert.Single(builder.Definition.ComponentSupportRules, rule => rule.ComponentTypeId == "helengine.RoundedRectComponent");
        Assert.Equal(PlatformComponentSupportKind.Transform, sprite.SupportKind);
        Assert.Equal(PlatformComponentSupportKind.Unsupported, text.SupportKind);
        Assert.Equal(PlatformComponentSupportKind.Unsupported, rounded.SupportKind);
    }
```

**Before writing the implementation, confirm the three component type id strings.** Grep the engine for the concrete component classes implementing `ISpriteDrawable2D`, `ITextDrawable2D` and `IRoundedRectDrawable2D`, and use the ids the build graph actually emits. The N64 platform definition is a working reference for sprite support. If an id differs from the guesses above, use the real one and update the test to match. Do not invent ids.

- [ ] **Step 2: Run the test to verify it fails**

Run the builder test command. Expected: FAIL, `Assert.Single` finds no matching rule.

- [ ] **Step 3: Add the rules**

In `builder/DreamcastPlatformDefinitionFactory.cs`, beside the existing light rules:

```csharp
                new PlatformComponentSupportRule("helengine.SpriteComponent", PlatformComponentSupportKind.Transform, "Authored sprites draw in the translucent overlay list.", "Keep sprite components; they render as textured quads."),
                new PlatformComponentSupportRule("helengine.TextComponent", PlatformComponentSupportKind.Unsupported, "Dreamcast has no font atlas or glyph layout yet.", "Remove text components or exclude them for dc."),
                new PlatformComponentSupportRule("helengine.RoundedRectComponent", PlatformComponentSupportKind.Unsupported, "Dreamcast draws no rounded-rectangle primitive.", "Remove rounded rectangle components or exclude them for dc."),
```

- [ ] **Step 4: Run the tests to verify they pass**

Run the builder test command. Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add builder/DreamcastPlatformDefinitionFactory.cs builder.tests/DreamcastPlatformDefinitionTests.cs
git commit -m "feat(dc): declare sprite support and reject text and rounded rects"
```

---

### Task 5: Cooked texture parsing on the runtime side

**Files:**
- Create: `src/platform/dreamcast/DreamcastTextureFormat.hpp`, `src/platform/dreamcast/DreamcastTextureFormat.cpp`
- Create: `tests/native/DreamcastTextureFormatTests.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: the `dc-texture-v1` layout table above.
- Produces:

```cpp
namespace helengine::dreamcast {
struct CookedTextureHeader {
    std::uint16_t paddedSide;
    std::uint16_t usedWidth;
    std::uint16_t usedHeight;
};
enum class CookedTextureStatus : std::uint8_t { Ok, TooSmall, BadMagic, BadVersion, BadFlags, BadSide, BadExtent, SizeMismatch };
CookedTextureStatus parseCookedTexture(const std::uint8_t* bytes, std::size_t length, CookedTextureHeader& header) noexcept;
constexpr std::size_t kCookedTextureHeaderBytes = 16;
}
```

This file carries no generated headers so it stays host-testable.

- [ ] **Step 1: Write the failing test**

Create `tests/native/DreamcastTextureFormatTests.cpp`, mirroring the harness style of `tests/native/DreamcastContentPathTests.cpp` (a plain `main`, an `expect` helper, a nonzero exit on failure):

```cpp
#include "platform/dreamcast/DreamcastTextureFormat.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

using namespace helengine::dreamcast;

int failures = 0;

void expect(bool condition, const char* expression) {
    if (!condition) {
        std::cerr << "expected " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(value) expect((value), #value)

std::vector<std::uint8_t> makeTexture(std::uint16_t side, std::uint16_t usedWidth, std::uint16_t usedHeight) {
    std::vector<std::uint8_t> bytes(kCookedTextureHeaderBytes + static_cast<std::size_t>(side) * side * 2, 0);
    const std::uint32_t magic = 0x58544344u;
    std::memcpy(bytes.data(), &magic, 4);
    const std::uint16_t version = 1;
    std::memcpy(bytes.data() + 4, &version, 2);
    std::memcpy(bytes.data() + 8, &side, 2);
    std::memcpy(bytes.data() + 10, &usedWidth, 2);
    std::memcpy(bytes.data() + 12, &usedHeight, 2);
    return bytes;
}

void ValidTextureParses() {
    std::vector<std::uint8_t> bytes = makeTexture(4, 3, 2);
    CookedTextureHeader header{};
    EXPECT(parseCookedTexture(bytes.data(), bytes.size(), header) == CookedTextureStatus::Ok);
    EXPECT(header.paddedSide == 4);
    EXPECT(header.usedWidth == 3);
    EXPECT(header.usedHeight == 2);
}

void CorruptTexturesAreRejectedSpecifically() {
    CookedTextureHeader header{};
    {
        std::vector<std::uint8_t> bytes = makeTexture(4, 3, 2);
        bytes[0] = 'X';
        EXPECT(parseCookedTexture(bytes.data(), bytes.size(), header) == CookedTextureStatus::BadMagic);
    }
    {
        std::vector<std::uint8_t> bytes = makeTexture(4, 3, 2);
        bytes[4] = 2;
        EXPECT(parseCookedTexture(bytes.data(), bytes.size(), header) == CookedTextureStatus::BadVersion);
    }
    {
        std::vector<std::uint8_t> bytes = makeTexture(4, 3, 2);
        bytes[6] = 1;
        EXPECT(parseCookedTexture(bytes.data(), bytes.size(), header) == CookedTextureStatus::BadFlags);
    }
    {
        // Side 6 is not a power of two.
        std::vector<std::uint8_t> bytes = makeTexture(4, 3, 2);
        const std::uint16_t side = 6;
        std::memcpy(bytes.data() + 8, &side, 2);
        EXPECT(parseCookedTexture(bytes.data(), bytes.size(), header) == CookedTextureStatus::BadSide);
    }
    {
        // Used extent cannot exceed the padded side.
        std::vector<std::uint8_t> bytes = makeTexture(4, 5, 2);
        EXPECT(parseCookedTexture(bytes.data(), bytes.size(), header) == CookedTextureStatus::BadExtent);
    }
    {
        std::vector<std::uint8_t> bytes = makeTexture(4, 3, 2);
        bytes.resize(bytes.size() - 2);
        EXPECT(parseCookedTexture(bytes.data(), bytes.size(), header) == CookedTextureStatus::SizeMismatch);
    }
    {
        std::vector<std::uint8_t> bytes = makeTexture(4, 3, 2);
        EXPECT(parseCookedTexture(bytes.data(), 4, header) == CookedTextureStatus::TooSmall);
    }
    EXPECT(parseCookedTexture(nullptr, 64, header) == CookedTextureStatus::TooSmall);
}

} // namespace

int main() {
    ValidTextureParses();
    CorruptTexturesAreRejectedSpecifically();
    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return 1;
    }
    std::cout << "DreamcastTextureFormatTests: all tests passed\n";
    return 0;
}
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
g++ -std=c++17 -Wall -Wextra -Werror -Isrc \
  tests/native/DreamcastTextureFormatTests.cpp \
  src/platform/dreamcast/DreamcastTextureFormat.cpp \
  -o /tmp/dc-texfmt
```

Expected: FAIL, the header does not exist.

- [ ] **Step 3: Write the header and implementation**

`src/platform/dreamcast/DreamcastTextureFormat.hpp` declares exactly the interface block above, with a doc comment on each entity. `src/platform/dreamcast/DreamcastTextureFormat.cpp` implements the checks **in the order the status enum lists them**, so a file with several faults reports the first: length below the header, magic, version, flags, side positive and a power of two and at most 1024, used extent nonzero and no greater than the side, then total length exactly `16 + side * side * 2`. Read every multi-byte field with `std::memcpy` into a local rather than casting the pointer, because the SH4 faults on unaligned loads.

- [ ] **Step 4: Run the test to verify it passes**

Re-run the compile command, then `/tmp/dc-texfmt`. Expected: `DreamcastTextureFormatTests: all tests passed`.

- [ ] **Step 5: Add to the Makefile**

In `Makefile`, add `src/platform/dreamcast/DreamcastTextureFormat.cpp` to `AUTHORED_SOURCES`, and add a `host-test` block for the new suite following the existing blocks, compiling the test with its one source and running the produced binary.

- [ ] **Step 6: Commit**

```bash
git add src/platform/dreamcast/DreamcastTextureFormat.hpp src/platform/dreamcast/DreamcastTextureFormat.cpp tests/native/DreamcastTextureFormatTests.cpp Makefile
git commit -m "feat(dc): parse and validate the cooked texture header"
```

---

### Task 6: Sprite contract and assembler submission

**Files:**
- Create: `src/platform/dreamcast/DreamcastSpriteFrame.hpp`
- Modify: `src/platform/dreamcast/DreamcastAuthoredFrame.hpp`, `src/platform/dreamcast/DreamcastAuthoredFrame.cpp`
- Create: `tests/native/DreamcastSpriteSubmissionTests.cpp`
- Modify: `Makefile`

**Interfaces:**
- Consumes: `ColorRgba` from `DreamcastGeometryPipeline.hpp`.
- Produces:

```cpp
namespace helengine::dreamcast {
constexpr std::uint16_t kMaxSprites = 64;

struct AuthoredSpriteState {
    float destinationX;      // screen pixels, left
    float destinationY;      // screen pixels, top
    float destinationWidth;  // screen pixels
    float destinationHeight; // screen pixels
    float sourceU0;          // normalized, within the used sub-rectangle
    float sourceV0;
    float sourceU1;
    float sourceV1;
    const void* texture;     // opaque handle; the raster stage knows its real type
    ColorRgba tint;
};

struct SpriteFrame {
    AuthoredSpriteState sprites[kMaxSprites];
    std::uint16_t spriteCount;
};

// On AuthoredFrameAssembler:
AuthoredFrameStatus addSprite(const AuthoredSpriteState& sprite) noexcept;
const SpriteFrame& sprites() const noexcept;
```

`AuthoredFrameStatus` gains `TooManySprites` and `InvalidSprite`, appended **after** `PresentFailed` so existing numeric values do not shift. `AuthoredFrameSummary` gains `std::uint16_t spriteCount`. `authoredFrameStatusName` gains the two names.

- [ ] **Step 1: Write the failing test**

Create `tests/native/DreamcastSpriteSubmissionTests.cpp` using the same harness shape as `tests/native/DreamcastAuthoredSubmissionTests.cpp`: an `expect` helper, an `EXPECT` macro, a `RecordingSink`, and `validCamera()` / `validDrawable()` helpers copied from that file. Include these cases:

```cpp
void OneSpriteIsAcceptedAndCounted() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addSprite(validSprite()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.sprites().spriteCount == 1);
    EXPECT(assembler.summary().spriteCount == 1);
    EXPECT(sink.presentCount == 1);
    EXPECT(sink.lastSpriteCount == 1);
}

void SixtyFifthSpriteFails() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    for (std::uint16_t i = 0; i < kMaxSprites; ++i) {
        EXPECT(assembler.addSprite(validSprite()) == AuthoredFrameStatus::Ok);
    }
    EXPECT(assembler.addSprite(validSprite()) == AuthoredFrameStatus::TooManySprites);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::TooManySprites);
    EXPECT(sink.presentCount == 0);
    EXPECT(assembler.summary().spriteCount == kMaxSprites + 1);
}

void InvalidSpritesFailBeforeAnyPresentation() {
    RecordingSink sink;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    AuthoredSpriteState cases[6] = {validSprite(), validSprite(), validSprite(),
                                    validSprite(), validSprite(), validSprite()};
    cases[0].destinationX = nan;
    cases[1].destinationWidth = 0.0f;   // zero extent draws nothing; reject rather than emit
    cases[2].texture = nullptr;
    cases[3].sourceU1 = 1.5f;           // outside the used sub-rectangle
    cases[4].sourceU1 = cases[4].sourceU0; // degenerate source
    cases[5].tint = ColorRgba{nan, 0.0f, 0.0f, 1.0f};
    for (const AuthoredSpriteState& sprite : cases) {
        AuthoredFrameAssembler assembler;
        assembler.begin(640.0f, 480.0f);
        EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
        EXPECT(assembler.addSprite(sprite) == AuthoredFrameStatus::InvalidSprite);
        EXPECT(assembler.present(sink) == AuthoredFrameStatus::InvalidSprite);
    }
    EXPECT(sink.presentCount == 0);
}

void ASpritelessFrameStillPresents() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);
    EXPECT(sink.presentCount == 1);
    EXPECT(sink.lastSpriteCount == 0);
}

void SpritesKeepSubmissionOrder() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    AuthoredSpriteState first = validSprite();
    first.destinationX = 10.0f;
    AuthoredSpriteState second = validSprite();
    second.destinationX = 200.0f;
    EXPECT(assembler.addSprite(first) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addSprite(second) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);
    EXPECT(near(assembler.sprites().sprites[0].destinationX, 10.0f));
    EXPECT(near(assembler.sprites().sprites[1].destinationX, 200.0f));
}
```

with the helper:

```cpp
int dummyTexture = 0;

AuthoredSpriteState validSprite() {
    return AuthoredSpriteState{32.0f, 48.0f, 64.0f, 64.0f,
                               0.0f, 0.0f, 0.75f, 0.5f,
                               &dummyTexture,
                               ColorRgba{1.0f, 1.0f, 1.0f, 1.0f}};
}
```

`RecordingSink` gains `std::uint16_t lastSpriteCount = 0;` and records it in `present`.

- [ ] **Step 2: Run the test to verify it fails**

```bash
g++ -std=c++17 -Wall -Wextra -Werror -Isrc \
  tests/native/DreamcastSpriteSubmissionTests.cpp \
  src/platform/dreamcast/DreamcastAuthoredFrame.cpp \
  src/platform/dreamcast/DreamcastAuthoredTransform.cpp \
  src/platform/dreamcast/DreamcastGeometryPipeline.cpp \
  src/platform/dreamcast/DreamcastCubeMesh.cpp \
  -o /tmp/dc-sprite
```

Expected: FAIL, `addSprite` does not exist.

- [ ] **Step 3: Add the sprite contract and change the sink**

Create `src/platform/dreamcast/DreamcastSpriteFrame.hpp` with the interface block above and a doc comment on each entity.

In `src/platform/dreamcast/DreamcastAuthoredFrame.hpp`: include the new header; append `TooManySprites` and `InvalidSprite` to `AuthoredFrameStatus`; add `std::uint16_t spriteCount;` to `AuthoredFrameSummary`; declare `addSprite` and `sprites()`; add a `SpriteFrame sprites_{};` member. Change the sink to carry both lists:

```cpp
class IAuthoredFrameSink {
public:
    virtual ~IAuthoredFrameSink() = default;
    virtual bool present(const GeometryFrame& geometry,
                         const SpriteFrame& sprites,
                         const ColorRgba& clearColor) noexcept = 0;
};
```

This is one call rather than two on purpose: it makes presenting the 3D list without its overlay impossible, and preserves the exactly-once contract the existing tests enforce.

In `src/platform/dreamcast/DreamcastAuthoredFrame.cpp`: reset `sprites_` and `summary_.spriteCount` in `begin`; implement `addSprite` with the bounds check **before** the write, signalling `kMaxSprites + 1` on overflow exactly as `setLights` signals a fifth light; validate finiteness of all eight floats and the tint, a nonzero destination extent, a non-null texture, source coordinates within `[0, 1]`, and `sourceU1 > sourceU0` and `sourceV1 > sourceV0`; pass `sprites_` to the sink in `present`; add both names to `authoredFrameStatusName`.

- [ ] **Step 4: Fix the existing callers**

`tests/native/DreamcastAuthoredSubmissionTests.cpp` and `src/platform/dreamcast/DreamcastRuntimeHost.cpp` both implement or call the sink. Update the `RecordingSink` in the existing test file and `RasterFrameSink` at `DreamcastRuntimeHost.cpp:239` to the three-argument signature. The renderer call inside `RasterFrameSink::present` stays as it is until Task 7.

- [ ] **Step 5: Run every host suite**

Rebuild and run all suites, including the pre-existing ones, to confirm the sink change broke nothing:

```bash
for t in DreamcastGeometryPipelineTests DreamcastAuthoredTransformTests DreamcastAuthoredMeshTests DreamcastRasterSeamTests DreamcastContentPathTests DreamcastAuthoredSubmissionTests DreamcastTextureFormatTests DreamcastSpriteSubmissionTests; do echo "== $t"; done
```

Expected: every suite prints its own pass line.

- [ ] **Step 6: Add the new suite to the Makefile and commit**

```bash
git add src/platform/dreamcast/DreamcastSpriteFrame.hpp src/platform/dreamcast/DreamcastAuthoredFrame.hpp src/platform/dreamcast/DreamcastAuthoredFrame.cpp tests/native/DreamcastSpriteSubmissionTests.cpp tests/native/DreamcastAuthoredSubmissionTests.cpp src/platform/dreamcast/DreamcastRuntimeHost.cpp Makefile
git commit -m "feat(dc): accept validated sprites into the authored frame"
```

---

### Task 7: Textured translucent pass

**Files:**
- Modify: `src/platform/dreamcast/DreamcastRasterRenderer.hpp`, `src/platform/dreamcast/DreamcastRasterRenderer.cpp`, `src/platform/dreamcast/DreamcastRuntimeHost.cpp`

**Interfaces:**
- Consumes: `SpriteFrame` from Task 6, `CookedTextureHeader` from Task 5.
- Produces: `bool renderFrame(const GeometryFrame& geometry, const SpriteFrame& sprites, const ColorRgba& clearColor) noexcept;`

**Context the implementer needs.** The renderer today opens one list. `DreamcastRasterRenderer.cpp:181` calls `pvr_poly_cxt_col(&context, PVR_LIST_OP_POLY)` and compiles a cached header once at init, because the opaque pass uses a single untextured context. Sprites cannot reuse that: each sprite binds its own texture, so its header must be compiled per sprite, per frame. The relevant KallistiOS entry points are:

```c
void pvr_poly_cxt_txr(pvr_poly_cxt_t *dst, pvr_list_t list, int textureformat,
                      int tw, int th, pvr_ptr_t textureaddr, pvr_filter_mode_t filtering);
void pvr_poly_compile(pvr_poly_hdr_t *dst, const pvr_poly_cxt_t *src);
```

with `PVR_LIST_TR_POLY` for the translucent list and the format word `PVR_TXRFMT_ARGB4444 | PVR_TXRFMT_TWIDDLED`. Depth writes are disabled through the context's depth sub-struct before compiling. The vertex type is `pvr_vertex_t` with `x`, `y`, `z`, `u`, `v`, `argb`.

- [ ] **Step 1: Extend the renderer signature**

Add the three-argument `renderFrame` overload to the header, keeping the existing two-argument one delegating to it with an empty `SpriteFrame`, so the diagnostic path is untouched.

- [ ] **Step 2: Implement the translucent pass**

After the opaque list is finished and before the scene ends, when `sprites.spriteCount != 0`, open `PVR_LIST_TR_POLY`. For each sprite in submission order: build a textured context for its texture, disable depth writes, compile a header into a 32-byte aligned local, submit it, then submit four vertices as two triangles in a strip. Give every vertex a constant `z` so overlay sprites never depth-fight each other, and pack the tint into `argb`. Convert the sprite's normalized source coordinates into `u` and `v` directly; the cooker already expressed them relative to the padded texture.

- [ ] **Step 3: Update the sink**

In `src/platform/dreamcast/DreamcastRuntimeHost.cpp`, `RasterFrameSink::present` now forwards all three arguments to the renderer.

- [ ] **Step 4: Verify the host suites still pass**

The renderer is not host-compiled, so this step confirms nothing regressed in the suites that are. Run all host suites. Expected: all pass.

- [ ] **Step 5: Verify the SH4 build compiles**

Run the full graph as documented in `docs/AuthoredSceneBuild.md`, and confirm exit code 0. This is the first task whose code only compiles under the cross toolchain, so do not skip it.

- [ ] **Step 6: Commit**

```bash
git add src/platform/dreamcast/DreamcastRasterRenderer.hpp src/platform/dreamcast/DreamcastRasterRenderer.cpp src/platform/dreamcast/DreamcastRuntimeHost.cpp
git commit -m "feat(dc): draw sprites in the PVR translucent list"
```

---

### Task 8: Runtime texture resource

**Files:**
- Create: `src/platform/dreamcast/DreamcastRuntimeTexture.hpp`, `src/platform/dreamcast/DreamcastRuntimeTexture.cpp`
- Modify: `src/platform/dreamcast/DreamcastRenderManager2D.hpp`, `src/platform/dreamcast/DreamcastRenderManager2D.cpp`, `Makefile`

**Interfaces:**
- Consumes: `parseCookedTexture` from Task 5.
- Produces: `class DreamcastRuntimeTexture final : public ::RuntimeTexture` holding `pvr_ptr_t vram`, `std::uint16_t paddedSide`, `usedWidth`, `usedHeight`, and the override

```cpp
::RuntimeTexture* BuildTextureFromCooked(std::string cookedAssetPath,
                                         ::IContentStreamSource* contentStreamSource) override;
```

**Context the implementer needs.** The generated `RuntimeTexture` base carries only `Width`, `Height`, `IsEngineOwned` and `IsDisposed`; it has **no** native handle, which is why a subclass is required. `BuildTextureFromCooked` is virtual with a body that throws in the base class, and the generated `RuntimeSceneAssetReferenceResolver::ResolveTexture` calls it whenever a scene references a texture by path. That is the entry point; `BuildTextureFromRaw` is not involved and keeps throwing. Upload with `pvr_txr_load(src, dst, count)`, a plain byte copy. Do **not** use `pvr_txr_load_ex`, whose documentation states it always twiddles regardless of flags and would therefore twiddle already-twiddled data.

- [ ] **Step 1: Implement the resource**

Read the whole cooked file through the content stream source, call `parseCookedTexture`, and on anything other than `Ok` throw with the path and the status name, so a corrupt texture names itself. On success allocate `paddedSide * paddedSide * 2` bytes with `pvr_mem_malloc`, fail the load if the allocation returns null, naming the path and the byte count, then `pvr_txr_load` the pixels and populate the base class `Width` and `Height` with the **used** extent, because that is what engine code reasons about.

- [ ] **Step 2: Implement release**

Override `ReleaseTexture(::RuntimeTexture* texture)` to `pvr_mem_free` the allocation and delete the object, tolerating a null argument. Leave `BuildTextureFromRaw` and `UpdateTextureRegionCore` throwing, with their messages updated to say textures must be cooked offline.

- [ ] **Step 3: Verify the SH4 build compiles**

Run the full graph. Expected: exit code 0.

- [ ] **Step 4: Commit**

```bash
git add src/platform/dreamcast/DreamcastRuntimeTexture.hpp src/platform/dreamcast/DreamcastRuntimeTexture.cpp src/platform/dreamcast/DreamcastRenderManager2D.hpp src/platform/dreamcast/DreamcastRenderManager2D.cpp Makefile
git commit -m "feat(dc): load cooked textures into video memory"
```

---

### Task 9: Gather sprites from the scene

**Files:**
- Modify: `src/platform/dreamcast/DreamcastRenderManager3D.hpp`, `src/platform/dreamcast/DreamcastRenderManager3D.cpp`, `src/platform/dreamcast/DreamcastRenderManager2D.hpp`, `src/platform/dreamcast/DreamcastRenderManager2D.cpp`, `src/platform/dreamcast/DreamcastRuntimeHost.cpp`

**Interfaces:**
- Consumes: `AuthoredFrameAssembler` from Task 6, `DreamcastRuntimeTexture` from Task 8.
- Produces: both render managers holding `AuthoredFrameAssembler*` rather than the 3D manager owning one.

**Context the implementer needs.** `DreamcastRenderManager3D` currently owns `AuthoredFrameAssembler assembler_;` as a member. Sprites arrive through the 2D manager and must land in the same frame, so ownership moves to `DreamcastRuntimeHost`, which constructs the assembler and injects a pointer into both managers. `ISpriteDrawable2D` exposes `get_Texture()`, `get_Color()` returning `byte4`, `get_SourceRect()` returning `float4`, and `get_Size()` returning `int2`. It has **no screen position**: that comes from `get_Parent()` and the entity's world transform, as `gatherCamera` already does for the camera at `DreamcastRenderManager3D.cpp:207`.

- [ ] **Step 1: Move assembler ownership**

Replace the 3D manager's member with `AuthoredFrameAssembler* assembler_ = nullptr;` and an `installAssembler` setter, and give the 2D manager the same. In `DreamcastRuntimeHost`, construct one assembler and install it in both before the frame loop. Every existing `assembler_.` use becomes `assembler_->` guarded by a null check that fails the draw rather than dereferencing.

- [ ] **Step 2: Implement DrawSprite**

Convert the drawable into an `AuthoredSpriteState`: destination from the parent entity's world transform translation plus `get_Size()`, source from `get_SourceRect()`, tint from `get_Color()` scaled from bytes to unit floats, and the texture pointer from `get_Texture()` downcast to `DreamcastRuntimeTexture`. A null parent, a null texture or a texture that is not ours fails the frame through the assembler's existing `InvalidSprite` path rather than throwing.

- [ ] **Step 3: Verify the SH4 build compiles and the host suites pass**

Run all host suites and the full graph. Expected: all suites pass, graph exit code 0.

- [ ] **Step 4: Commit**

```bash
git add src/platform/dreamcast/DreamcastRenderManager3D.hpp src/platform/dreamcast/DreamcastRenderManager3D.cpp src/platform/dreamcast/DreamcastRenderManager2D.hpp src/platform/dreamcast/DreamcastRenderManager2D.cpp src/platform/dreamcast/DreamcastRuntimeHost.cpp
git commit -m "feat(dc): gather authored sprites into the shared frame"
```

---

### Task 10: Snapshot version 3 and the verifier gate

**Files:**
- Modify: `src/platform/dreamcast/DreamcastRuntimeHost.hpp`, `src/platform/dreamcast/DreamcastRuntimeHost.cpp`, `scripts/verify-authored-scene.ps1`

**Interfaces:**
- Consumes: `AuthoredFrameSummary::spriteCount` from Task 6.
- Produces: snapshot version 3 with a packed count word; verifier parameter `-MinimumSprites`.

**Context the implementer needs.** `DreamcastAuthoredSnapshot` is exactly 64 bytes across sixteen `std::uint32_t` fields with none spare, and its `static_assert` must keep holding. The last field is `directionalLightCount`. Pack both counts into it: light count in the low sixteen bits, sprite count in the high sixteen. The verifier reads sixteen words and formats `a[16]` as `lights`; it must now report both. `-MinimumLights` was added on 2026-09-17 and its gate lives beside the success predicate; `-MinimumSprites` follows exactly that shape, including registering the new terminal kind in the result regex, which was missed the first time and cost a debugging round.

- [ ] **Step 1: Pack the counts and bump the version**

Rename the field to `packedCounts`, write `lights | (sprites << 16)`, and set `kDreamcastAuthoredSnapshotVersion = 3u`.

- [ ] **Step 2: Update the verifier**

In the Lua probe, change the validity gate to require `a[2] == 3`, unpack both counts, and print `lights=%u sprites=%u`. Add `$MinimumSprites` beside `$MinimumLights` with the same default of 0 and the same `@@MINIMUM_SPRITES@@` substitution, add the sprite check beside the light check in the success predicate emitting `sprites-below-gate`, and **add `sprites-below-gate` to the terminal-result regex** alongside the kinds already listed.

- [ ] **Step 3: Prove the gate both ways**

Build, then run the verifier twice against the same build: once with `-MinimumSprites` at the real sprite count, expecting PASS, and once one higher, expecting `Result=sprites-below-gate` and a nonzero exit. A gate that has only been seen to pass has not been tested.

- [ ] **Step 4: Commit**

```bash
git add src/platform/dreamcast/DreamcastRuntimeHost.hpp src/platform/dreamcast/DreamcastRuntimeHost.cpp scripts/verify-authored-scene.ps1
git commit -m "feat(dc): publish the sprite count and gate the verifier on it"
```

---

### Task 11: Enable the overlay and record the evidence

**Files:**
- Modify: `C:/dev/helprojs/demodisc/assets/codebase/rendering.tools/CubeTestSceneFactory.cs`
- Modify: `docs/Validation.md`, `docs/AuthoredSceneBuild.md`

**Interfaces:**
- Consumes: everything above.
- Produces: the recorded evidence rows.

**Context the implementer needs.** `SpritelessRuntimeExcludedPlatformIds` at `CubeTestSceneFactory.cs:29` contains Dreamcast alone and causes the console instruction Blueprint root to be dropped. The demodisc repository is shared with other sessions and has unrelated pending work; commit only this one file. Regenerating the scene rewrites `user_settings/build_config.json` and drops the `dc` entry, so regenerate first and restore that entry afterwards, per `docs/AuthoredSceneBuild.md`.

- [ ] **Step 1: Remove the exclusion**

Change `SpritelessRuntimeExcludedPlatformIds` to an empty array and update its documentation comment to say both runtimes now render the authored sprite icons. Commit that file alone in the demodisc repository.

- [ ] **Step 2: Build and verify**

Run the full graph, then the verifier with `-MinimumSprites` set to the number of icons the Blueprint declares, capturing frames.

- [ ] **Step 3: Confirm the 3D path did not regress**

Confirm the lit cube still shows two distinct face shades in the captured frame, as recorded for ELF `5c88499c7829b9d4854f755043a47a18a73620ae85b5023b1ae8ec9bcf04bc1c`. A sprite overlay must not disturb the opaque pass.

- [ ] **Step 4: Record the evidence**

Add a section to `docs/Validation.md` with the build identity and ELF hash, the verifier result with the sprite gate armed, the measured free video memory after tile accelerator initialisation, and the derived build-time texture limit.

**The evidence must be captured pixels showing the icons, not a non-zero sprite count.** A count proves gathering, not drawing. Identify specific pixel coordinates inside an icon and give their values, and state plainly anything the capture does not demonstrate. The lighting evidence row had to be corrected for exactly this class of overclaim.

Update `docs/AuthoredSceneBuild.md` to describe sprites in the authored contract list, and to stop saying textures are left out of the ROMFS.

- [ ] **Step 5: Commit**

```bash
git add docs/Validation.md docs/AuthoredSceneBuild.md
git commit -m "docs(dc): record the on-screen sprite evidence"
```

---

## Self-Review

**Spec coverage.** Cooked texture asset, Task 2. Twiddling, Task 1. Padding and used sub-rectangle, Tasks 1 and 2. The two cook rejections, Task 2. Video memory without reservation, Task 8. Build-time total check, Task 3's payload budget. Measured free memory, Task 11. `BuildTextureFromRaw` and `UpdateTextureRegionCore` still throwing, Task 8. `BuildTextureFromCooked` override, Task 8. Sprite state and validation, Task 6. The 64 sprite cap, Task 6. One sink call carrying both lists, Task 6. Translucent list with depth off in submission order, Task 7. Screen position from the entity transform, Task 9. Assembler ownership moving to the host, Task 9. Component support rules, Task 4. Packed snapshot at version 3, Task 10. New statuses, Task 6. Host and cooker tests, Tasks 1, 2, 3, 5, 6. Hand-computed Morton test with mutation, Task 1. Verifier sprite gate, Task 10. Exclusion removal and pixel evidence, Task 11.

**One spec requirement deliberately changed.** Per-axis power-of-two padding became square padding, argued at the top of this plan.

**One item needs confirming during execution, not guessing.** The three component type id strings in Task 4 are written as the expected names, and the task explicitly instructs the implementer to confirm them against the engine and the N64 platform definition before use.

**Type consistency.** `AuthoredSpriteState` field names are identical in Tasks 6, 7 and 9. `CookedTextureHeader` field names are identical in Tasks 5 and 8. `DreamcastTextureSchemaIds.HeaderBytes` is 16 in Task 2 and `kCookedTextureHeaderBytes` is 16 in Task 5, and both match the layout table. `Twiddle(ushort[], int)` is called with the same signature in Tasks 1 and 2.
