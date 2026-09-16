# Dreamcast Per-Vertex Lighting Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Light authored Dreamcast meshes with the engine's directional and ambient lights so demodisc `cube_test` renders its sun the way GameCube does.

**Architecture:** Lighting is computed per vertex on the SH4 inside the existing generated-header-free frame assembler, just before the geometry pipeline runs, and written into the persistent scratch vertex colors that the PVR already interpolates as Gouraud colors. The geometry pipeline, clipper and raster renderer are untouched. Light gathering (generated `ObjectManager` lists) stays in the render manager; the assembler receives a plain light struct, so all lighting math is host-testable with a fake sink.

**Tech Stack:** C++20 cross-compiled with the pinned KOS/GCC 15.2 SDK image; .NET 9 builder and fixture tool; PowerShell 7; Docker; Flycast 2.7 with Lua probes.

**Spec:** `docs/superpowers/specs/2026-09-16-dreamcast-lighting-design.md`

## Global Constraints

- Work in `C:\dev\helworks\helengine-dc` on `main`. Do not merge or push. Commit after every task.
- Never edit generated C++. Generated Core, the engine checkout and the generator are read-only inputs for this plan.
- Platform ids after this plan: material schema `dc-standard-v1`, graphics profile `dc-standard`, renderer family `dc-standard`. Builder id `helengine.dreamcast.builder`, version `0.2.0`, platform `dc`.
- Lighting model: `color = clamp01(base × (ambient + Σ lightRgb × max(0, dot(n, −lightDirection))))`, alpha 1. No implicit ambient term.
- Maximum 4 enabled directional lights. A fifth is an explicit failure, never silently dropped.
- A lit material on a mesh without normals is an explicit failure, never a silent unlit fallback.
- Native allocations only at startup or explicit release; no per-frame allocation. One PVR scene per frame.
- Budgets unchanged: 1 MiB embedded cooked payload, 8 MiB resident ELF span, at least 4 MiB free heap after startup.
- Diagnostic mode behaviour must not change. Its static-pose capture hash must stay `2c13dc96efabe106aae6beac98426f32eea4d8637e18360a8f7efccbde2ae804`.
- `pwsh` is not on PATH. Use `C:\Users\Helena\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe`.
- Run `git` as `git -c safe.directory=*`. Run `docker` from Bash with `MSYS_NO_PATHCONV=1`.
- Never edit `src/`, `tests/`, `Makefile` or `scripts/build.ps1` while a native build or a graph native stage is running; `build.ps1` hashes those inputs and rejects races.
- End every commit message with:
  `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`

## File Structure

| File | Responsibility |
|---|---|
| `src/platform/dreamcast/DreamcastGeometryPipeline.hpp` | `SourceVertex` gains `normal`; `MeshView` gains `hasNormals`. |
| `src/platform/dreamcast/DreamcastCubeMesh.cpp` | Diagnostic cube literal, unchanged semantics (normals default to zero). |
| `src/platform/dreamcast/DreamcastRuntimeModel.*` | Copies cooked model normals into the owned native mesh. |
| `src/platform/dreamcast/DreamcastAuthoredTransform.*` | Pure matrix/quaternion helpers; gains `makeNormalMatrix` and `directionalLightForward`. |
| `src/platform/dreamcast/DreamcastAuthoredFrame.*` | Light state types, `setLights`, per-vertex lighting, new failure statuses. |
| `src/platform/dreamcast/DreamcastRuntimeMaterial.*` | Accepts `dc-standard`, exposes `lit()`. |
| `src/platform/dreamcast/DreamcastRenderManager3D.*` | Gathers enabled lights each frame, passes `lit` per drawable. |
| `src/platform/dreamcast/DreamcastRuntimeHost.*` | Authored snapshot v2 with `directionalLightCount`. |
| `builder/DreamcastMaterialSchemaIds.cs` | Renamed schema/profile ids and lighting values. |
| `builder/DreamcastMaterialCooker.cs` | `lighting-mode` default `lit`; sets cooked `Lit`. |
| `builder/DreamcastPlatformDefinitionFactory.cs` | Renamed profiles, `lit` default, both lighting choices. |
| `builder/DreamcastCookedContentValidator.cs` | Allows light components and lit materials; `dc-standard` family. |
| `tools/helengine.dreamcast.fixture/DreamcastAuthoredSceneFixtureWriter.cs` | Renamed ids; new positive `lit` variant; `lit-material` no longer a negative. |
| `scripts/verify-authored-scene.ps1` | Lua probe prints `directionalLightCount`. |
| `tests/native/DreamcastAuthoredSubmissionTests.cpp` | Host lighting tests. |
| `tests/native/DreamcastAuthoredTransformTests.cpp` | Host tests for the two new transform helpers. |
| `builder.tests/*` | Builder-side tests for the rename, defaults and validator. |

---

### Task 1: Native mesh contract carries normals

**Files:**
- Modify: `src/platform/dreamcast/DreamcastGeometryPipeline.hpp`
- Modify: `src/platform/dreamcast/DreamcastRuntimeModel.cpp`
- Test: `tests/native/DreamcastAuthoredMeshTests.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `SourceVertex{ Vec3 position; ColorRgba color; Vec3 normal; }` and `MeshView{ const SourceVertex* vertices; std::uint16_t vertexCount; const TriangleIndex* triangles; std::uint16_t triangleCount; bool hasNormals; }`. Both new members are declared **last** so every existing aggregate initializer (`SourceVertex{pos, color}`, `MeshView{v, 24, t, 12}`) still compiles and value-initializes the new member.

- [ ] **Step 1: Write the failing test**

Append to `tests/native/DreamcastAuthoredMeshTests.cpp`, inside the anonymous namespace, before `} // namespace`:

```cpp
void MeshViewCarriesNormalsFlagAndVertexNormals() {
    using namespace helengine::dreamcast;
    const SourceVertex vertices[3] = {
        {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
    };
    const TriangleIndex triangles[1] = {{0, 1, 2}};
    const MeshView lit{vertices, 3, triangles, 1, true};
    EXPECT(lit.hasNormals);
    EXPECT(lit.vertices[2].normal.z == 1.0f);

    // Existing four-field initializers must still compile and mean "no normals".
    const MeshView legacy{vertices, 3, triangles, 1};
    EXPECT(!legacy.hasNormals);
    EXPECT(legacy.vertices[0].normal.x == 0.0f);
    EXPECT(legacy.vertices[0].normal.y == 0.0f);
    EXPECT(legacy.vertices[0].normal.z == 0.0f);
}
```

Register it by adding this line to that file's `main()`, before the failure check:

```cpp
    MeshViewCarriesNormalsFlagAndVertexNormals();
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
cd /c/dev/helworks/helengine-dc
mkdir -p /c/dev/helworks/builds/helengine-dc/v03/host-check
MSYS_NO_PATHCONV=1 docker run --rm --workdir /src \
  --mount "type=bind,source=C:/dev/helworks/helengine-dc,target=/src,readonly" \
  --mount "type=bind,source=C:/dev/helworks/builds/helengine-dc/v03/host-check,target=/out" \
  --entrypoint /bin/bash helengine-dc-sdk:v01 -lc 'make -C /src BUILD_ROOT=/out MODE=Diagnostic host-test 2>&1 | tail -n 25'
```

Expected: FAIL compiling `DreamcastAuthoredMeshTests.cpp` with "too many initializers for 'const helengine::dreamcast::MeshView'" and "no member named 'normal'".

- [ ] **Step 3: Add the members**

In `src/platform/dreamcast/DreamcastGeometryPipeline.hpp`, replace:

```cpp
struct SourceVertex {
    Vec3 position;
    ColorRgba color;
};
```

with:

```cpp
struct SourceVertex {
    Vec3 position;
    ColorRgba color;
    // Model-space normal. Declared last so existing position/color aggregate
    // initializers keep compiling and zero-fill it; only lit draws read it.
    Vec3 normal;
};
```

and replace:

```cpp
struct MeshView {
    const SourceVertex* vertices;
    std::uint16_t vertexCount;
    const TriangleIndex* triangles;
    std::uint16_t triangleCount;
};
```

with:

```cpp
struct MeshView {
    const SourceVertex* vertices;
    std::uint16_t vertexCount;
    const TriangleIndex* triangles;
    std::uint16_t triangleCount;
    // True only when every vertex carries an authored model-space normal.
    bool hasNormals;
};
```

- [ ] **Step 4: Run the test to verify it passes**

Run the Step 2 command again.
Expected: every suite prints "all tests passed" / "tests passed", including `DreamcastAuthoredMeshTests`.

- [ ] **Step 5: Copy cooked normals in the model converter**

In `src/platform/dreamcast/DreamcastRuntimeModel.hpp`, add a private member after `std::uint16_t triangleCount_ = 0;`:

```cpp
    bool hasNormals_ = false;
```

In `src/platform/dreamcast/DreamcastRuntimeModel.cpp`, replace the body of `meshView()`:

```cpp
MeshView DreamcastRuntimeModel::meshView() const noexcept {
    return MeshView{vertices_, vertexCount_, triangles_, triangleCount_, hasNormals_};
}
```

In the same file, inside `validateAsset`, immediately before its closing brace, add:

```cpp
    if (asset.Normals != nullptr && asset.Normals->get_Length() != 0) {
        if (asset.Normals->get_Length() != asset.Positions->get_Length()) {
            throw std::invalid_argument("Dreamcast model normal count must match its position count.");
        }
        for (int32_t index = 0; index < asset.Normals->get_Length(); ++index) {
            const ::float3& normal = (*asset.Normals)[index];
            if (!finite(normal.X) || !finite(normal.Y) || !finite(normal.Z)) {
                throw std::invalid_argument("Dreamcast model normals must be finite.");
            }
        }
    }
```

In `CreateOwned`, replace the vertex copy loop:

```cpp
        runtime->hasNormals_ = assetLease.value->Normals != nullptr &&
                               assetLease.value->Normals->get_Length() == runtime->vertexCount_;
        for (std::uint16_t index = 0; index < runtime->vertexCount_; ++index) {
            const ::float3& position = (*assetLease.value->Positions)[index];
            SourceVertex vertex{{position.X, position.Y, position.Z},
                                {1.0f, 1.0f, 1.0f, 1.0f},
                                {0.0f, 0.0f, 0.0f}};
            if (runtime->hasNormals_) {
                const ::float3& normal = (*assetLease.value->Normals)[index];
                vertex.normal = Vec3{normal.X, normal.Y, normal.Z};
            }
            runtime->vertices_[index] = vertex;
        }
```

In `Dispose`, after `triangleCount_ = 0;`, add:

```cpp
    hasNormals_ = false;
```

- [ ] **Step 6: Verify the converter still cross-compiles**

The model converter needs generated headers, so it is verified by cross-compilation, not by the host suite. Run the Authored cross-build against the last known-good staged inputs:

```bash
PW='C:\Users\Helena\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe'
R='C:\dev\helworks\builds\helengine-dc\v02\cube-test\cache\v2\d87d222cd166d9049967e97c50c8e301\b\dc\debug\release'
"$PW" -NoProfile -File scripts/build.ps1 -BuildId v03-task1 -Mode Authored \
  -OutputRoot 'C:\dev\helworks\builds\helengine-dc\v03' \
  -GeneratedCoreRoot "$R\generated-core" \
  -ContentRoot "$R\native\dreamcast-build-a35a385bf4f04e7d972fc781b6fe1190\content" \
  -CatalogRoot "$R\native\dreamcast-build-a35a385bf4f04e7d972fc781b6fe1190\catalog"
```

If those staged directories no longer exist, list `C:\dev\helworks\builds\helengine-dc\v02\cube-test\cache\v2\*\b\dc\debug\release\native\` and use the newest `dreamcast-build-*` pair instead.
Expected: "Built ...helengine_dc.elf" and a SHA256 line.

- [ ] **Step 7: Commit**

```bash
git -c safe.directory=* add src/platform/dreamcast/DreamcastGeometryPipeline.hpp src/platform/dreamcast/DreamcastRuntimeModel.hpp src/platform/dreamcast/DreamcastRuntimeModel.cpp tests/native/DreamcastAuthoredMeshTests.cpp
git -c safe.directory=* commit -m "Carry model normals through the native mesh contract

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: Lighting transform helpers

**Files:**
- Modify: `src/platform/dreamcast/DreamcastAuthoredTransform.hpp`
- Modify: `src/platform/dreamcast/DreamcastAuthoredTransform.cpp`
- Test: `tests/native/DreamcastAuthoredTransformTests.cpp`

**Interfaces:**
- Consumes: `Matrix4x4`, `Vec3` from `DreamcastGeometryPipeline.hpp`.
- Produces:
  - `struct GeneratedQuaternion { float x; float y; float z; float w; };`
  - `bool makeNormalMatrix(const Matrix4x4& worldFromModel, Matrix4x4& normalMatrix) noexcept` — writes the cofactor matrix of the upper 3×3 into the upper 3×3 of `normalMatrix` (row 3 and column 3 become the identity's). The cofactor matrix equals the inverse-transpose scaled by the determinant; since `validateModelTransform` guarantees a positive determinant and callers renormalize, the scale is irrelevant and no division is performed. Returns false when the input is not a valid model transform.
  - `Vec3 directionalLightForward(const GeneratedQuaternion& orientation) noexcept` — the engine forward axis `(0,0,-1)` rotated by `orientation`, using the engine's `float4.RotateVector` formula verbatim. Not normalized by the helper; the caller normalizes.

- [ ] **Step 1: Write the failing tests**

Append to `tests/native/DreamcastAuthoredTransformTests.cpp`, inside the anonymous namespace, before `} // namespace`:

```cpp
void NormalMatrixKeepsNormalsPerpendicularUnderNonuniformScale() {
    using helengine::dreamcast::makeNormalMatrix;
    // Squash Y to half. A model normal of (0,1,1)/sqrt(2) belongs to a face
    // whose tangent (0,-1,1) becomes (0,-0.5,1) after the squash.
    Matrix4x4 worldFromModel = identity();
    worldFromModel.m[1][1] = 0.5f;
    Matrix4x4 normalMatrix{};
    EXPECT(makeNormalMatrix(worldFromModel, normalMatrix));

    const float inverseRootTwo = 0.70710678f;
    Vec3 worldNormal = helengine::dreamcast::transformDirection(
        normalMatrix, Vec3{0.0f, inverseRootTwo, inverseRootTwo});
    const float length = std::sqrt(worldNormal.x * worldNormal.x +
                                   worldNormal.y * worldNormal.y +
                                   worldNormal.z * worldNormal.z);
    worldNormal = Vec3{worldNormal.x / length, worldNormal.y / length, worldNormal.z / length};

    EXPECT(near(worldNormal.x, 0.0f));
    EXPECT(near(worldNormal.y, 0.89442719f));
    EXPECT(near(worldNormal.z, 0.44721360f));
    // Perpendicular to the transformed tangent.
    const float dot = worldNormal.x * 0.0f + worldNormal.y * -0.5f + worldNormal.z * 1.0f;
    EXPECT(near(dot, 0.0f));
}

void NormalMatrixRejectsInvalidModelTransforms() {
    using helengine::dreamcast::makeNormalMatrix;
    Matrix4x4 mirrored = identity();
    mirrored.m[0][0] = -1.0f;
    Matrix4x4 normalMatrix{};
    EXPECT(!makeNormalMatrix(mirrored, normalMatrix));
}

void DirectionalLightForwardMatchesEngineRotation() {
    using helengine::dreamcast::directionalLightForward;
    using helengine::dreamcast::GeneratedQuaternion;

    Vec3 identityForward = directionalLightForward(GeneratedQuaternion{0.0f, 0.0f, 0.0f, 1.0f});
    EXPECT(near(identityForward.x, 0.0f));
    EXPECT(near(identityForward.y, 0.0f));
    EXPECT(near(identityForward.z, -1.0f));

    // Ninety degrees about +Y takes -Z to -X.
    const float halfRootTwo = 0.70710678f;
    Vec3 yawed = directionalLightForward(GeneratedQuaternion{0.0f, halfRootTwo, 0.0f, halfRootTwo});
    EXPECT(near(yawed.x, -1.0f));
    EXPECT(near(yawed.y, 0.0f));
    EXPECT(near(yawed.z, 0.0f));
}
```

Register them in that file's `main()`, before the failure check:

```cpp
    NormalMatrixKeepsNormalsPerpendicularUnderNonuniformScale();
    NormalMatrixRejectsInvalidModelTransforms();
    DirectionalLightForwardMatchesEngineRotation();
```

Add `#include <cmath>` to the top of the test file if it is not already present (it is; verify before adding).

- [ ] **Step 2: Run the tests to verify they fail**

Run the Task 1 Step 2 docker command.
Expected: FAIL with "'makeNormalMatrix' is not a member of 'helengine::dreamcast'".

- [ ] **Step 3: Declare the helpers**

In `src/platform/dreamcast/DreamcastAuthoredTransform.hpp`, add before the closing `} // namespace helengine::dreamcast`:

```cpp
/// Plain mirror of the generated float4 quaternion fields, so lighting math
/// stays free of generated headers and host-testable.
struct GeneratedQuaternion {
    float x;
    float y;
    float z;
    float w;
};

/// Transforms a direction (w = 0), ignoring translation.
Vec3 transformDirection(const Matrix4x4& matrix, const Vec3& direction) noexcept;

/// Builds the matrix that carries model-space normals to world space for a
/// validated model transform. It is the cofactor matrix of the upper 3x3,
/// which equals the inverse-transpose up to the positive determinant; callers
/// renormalize, so the scale is irrelevant and no division is performed.
bool makeNormalMatrix(const Matrix4x4& worldFromModel, Matrix4x4& normalMatrix) noexcept;

/// Rotates the engine forward axis (0, 0, -1) by an entity world orientation,
/// using the engine's float4.RotateVector formula. The result is not normalized.
Vec3 directionalLightForward(const GeneratedQuaternion& orientation) noexcept;
```

- [ ] **Step 4: Implement the helpers**

In `src/platform/dreamcast/DreamcastAuthoredTransform.cpp`, add before the closing `} // namespace helengine::dreamcast`:

```cpp
Vec3 transformDirection(const Matrix4x4& matrix, const Vec3& direction) noexcept {
    return Vec3{
        matrix.m[0][0] * direction.x + matrix.m[0][1] * direction.y + matrix.m[0][2] * direction.z,
        matrix.m[1][0] * direction.x + matrix.m[1][1] * direction.y + matrix.m[1][2] * direction.z,
        matrix.m[2][0] * direction.x + matrix.m[2][1] * direction.y + matrix.m[2][2] * direction.z,
    };
}

bool makeNormalMatrix(const Matrix4x4& worldFromModel, Matrix4x4& normalMatrix) noexcept {
    if (!validateModelTransform(worldFromModel)) {
        return false;
    }
    const auto& a = worldFromModel.m;
    normalMatrix = Matrix4x4{{
        {a[1][1] * a[2][2] - a[1][2] * a[2][1],
         a[1][2] * a[2][0] - a[1][0] * a[2][2],
         a[1][0] * a[2][1] - a[1][1] * a[2][0],
         0.0f},
        {a[0][2] * a[2][1] - a[0][1] * a[2][2],
         a[0][0] * a[2][2] - a[0][2] * a[2][0],
         a[0][1] * a[2][0] - a[0][0] * a[2][1],
         0.0f},
        {a[0][1] * a[1][2] - a[0][2] * a[1][1],
         a[0][2] * a[1][0] - a[0][0] * a[1][2],
         a[0][0] * a[1][1] - a[0][1] * a[1][0],
         0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    }};
    return finite(normalMatrix);
}

Vec3 directionalLightForward(const GeneratedQuaternion& orientation) noexcept {
    // Engine float4.RotateVector with value = (0, 0, -1), kept in the same
    // double-precision shape as the engine implementation.
    const double qx = orientation.x;
    const double qy = orientation.y;
    const double qz = orientation.z;
    const double qw = orientation.w;
    const double vx = 0.0;
    const double vy = 0.0;
    const double vz = -1.0;

    const double tx = 2.0 * (qy * vz - qz * vy);
    const double ty = 2.0 * (qz * vx - qx * vz);
    const double tz = 2.0 * (qx * vy - qy * vx);

    const double cx = (qy * tz) - (qz * ty);
    const double cy = (qz * tx) - (qx * tz);
    const double cz = (qx * ty) - (qy * tx);

    return Vec3{
        static_cast<float>(vx + (tx * qw) + cx),
        static_cast<float>(vy + (ty * qw) + cy),
        static_cast<float>(vz + (tz * qw) + cz),
    };
}
```

The file's anonymous namespace already defines `finite(const Matrix4x4&)`; `makeNormalMatrix` uses it.

- [ ] **Step 5: Run the tests to verify they pass**

Run the Task 1 Step 2 docker command.
Expected: `DreamcastAuthoredTransformTests: all tests passed`, and every other suite still passes.

- [ ] **Step 6: Commit**

```bash
git -c safe.directory=* add src/platform/dreamcast/DreamcastAuthoredTransform.hpp src/platform/dreamcast/DreamcastAuthoredTransform.cpp tests/native/DreamcastAuthoredTransformTests.cpp
git -c safe.directory=* commit -m "Add normal-matrix and light-forward transform helpers

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: Per-vertex lighting in the frame assembler

**Files:**
- Modify: `src/platform/dreamcast/DreamcastAuthoredFrame.hpp`
- Modify: `src/platform/dreamcast/DreamcastAuthoredFrame.cpp`
- Test: `tests/native/DreamcastAuthoredSubmissionTests.cpp`

**Interfaces:**
- Consumes: `makeNormalMatrix`, `transformDirection` (Task 2); `MeshView::hasNormals`, `SourceVertex::normal` (Task 1).
- Produces:
  - `constexpr std::uint8_t kMaxDirectionalLights = 4;`
  - `struct AuthoredDirectionalLight { Vec3 direction; ColorRgba color; };` — `direction` is the normalized world direction the light travels; `color` is RGB premultiplied by intensity, alpha unused.
  - `struct AuthoredLightState { ColorRgba ambient; std::uint8_t directionalCount; AuthoredDirectionalLight directional[kMaxDirectionalLights]; };`
  - `AuthoredFrameStatus AuthoredFrameAssembler::setLights(const AuthoredLightState& lights) noexcept`
  - `AuthoredDrawState` gains `bool lit;` (declared last).
  - `AuthoredFrameSummary` gains `std::uint8_t directionalLightCount;` and `bool lit;`.
  - New statuses `TooManyDirectionalLights`, `InvalidLight`, `LitMeshWithoutNormals` with names `too-many-directional-lights`, `invalid-light`, `lit-mesh-without-normals`.

- [ ] **Step 1: Write the failing tests**

In `tests/native/DreamcastAuthoredSubmissionTests.cpp`, add these helpers inside the anonymous namespace, after the existing `validDrawable` helper:

```cpp
AuthoredLightState whiteLightFromPositiveZ(float ambientLevel = 0.0f) {
    // Travels along -Z, so a +Z facing normal is fully lit.
    AuthoredLightState lights{};
    lights.ambient = ColorRgba{ambientLevel, ambientLevel, ambientLevel, 1.0f};
    lights.directionalCount = 1;
    lights.directional[0] = AuthoredDirectionalLight{{0.0f, 0.0f, -1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}};
    return lights;
}

/// One quad facing +Z and one facing -Z, both with explicit normals.
AuthoredDrawState twoFacingDrawable(const SourceVertex* vertices, const TriangleIndex* triangles, bool lit) {
    AuthoredDrawState drawable{};
    drawable.worldFromModel = translation(0.0f, 0.0f, -6.0f);
    drawable.mesh = MeshView{vertices, 6, triangles, 2, true};
    drawable.hasMaterial = true;
    drawable.materialColor = ColorRgba{1.0f, 0.5f, 0.25f, 1.0f};
    drawable.lit = lit;
    return drawable;
}
```

Then add these tests, and register each one in `main()` before the failure check:

```cpp
void LitVertexFacingLightGetsFullLightColor() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    const SourceVertex vertices[3] = {
        {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
    };
    const TriangleIndex triangles[1] = {{0, 1, 2}};
    AuthoredDrawState drawable{};
    drawable.worldFromModel = translation(0.0f, 0.0f, -6.0f);
    drawable.mesh = MeshView{vertices, 3, triangles, 1, true};
    drawable.hasMaterial = true;
    drawable.materialColor = ColorRgba{1.0f, 0.5f, 0.25f, 1.0f};
    drawable.lit = true;

    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(whiteLightFromPositiveZ()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);

    const ProjectedVertex& vertex = assembler.geometry().triangles[0].vertices[0];
    EXPECT(near(vertex.color.r, 1.0f));
    EXPECT(near(vertex.color.g, 0.5f));
    EXPECT(near(vertex.color.b, 0.25f));
    EXPECT(assembler.summary().directionalLightCount == 1);
    EXPECT(assembler.summary().lit);
}

void LitVertexFacingAwayGetsAmbientOnly() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    // Normals point away from the light (-Z), so only ambient 0.25 applies.
    const SourceVertex vertices[3] = {
        {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
    };
    const TriangleIndex triangles[1] = {{0, 1, 2}};
    AuthoredDrawState drawable{};
    drawable.worldFromModel = translation(0.0f, 0.0f, -6.0f);
    drawable.mesh = MeshView{vertices, 3, triangles, 1, true};
    drawable.hasMaterial = true;
    drawable.materialColor = ColorRgba{1.0f, 0.5f, 0.25f, 1.0f};
    drawable.lit = true;

    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(whiteLightFromPositiveZ(0.25f)) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);

    const ProjectedVertex& vertex = assembler.geometry().triangles[0].vertices[0];
    EXPECT(near(vertex.color.r, 0.25f));
    EXPECT(near(vertex.color.g, 0.125f));
    EXPECT(near(vertex.color.b, 0.0625f));
}

void LitVertexFacingAwayWithoutAmbientIsBlack() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    const SourceVertex vertices[3] = {
        {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
    };
    const TriangleIndex triangles[1] = {{0, 1, 2}};
    AuthoredDrawState drawable{};
    drawable.worldFromModel = translation(0.0f, 0.0f, -6.0f);
    drawable.mesh = MeshView{vertices, 3, triangles, 1, true};
    drawable.hasMaterial = true;
    drawable.materialColor = ColorRgba{1.0f, 0.5f, 0.25f, 1.0f};
    drawable.lit = true;

    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(whiteLightFromPositiveZ()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);

    const ProjectedVertex& vertex = assembler.geometry().triangles[0].vertices[0];
    EXPECT(near(vertex.color.r, 0.0f));
    EXPECT(near(vertex.color.g, 0.0f));
    EXPECT(near(vertex.color.b, 0.0f));
}

void LitColorIsClampedToOne() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    const SourceVertex vertices[3] = {
        {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
    };
    const TriangleIndex triangles[1] = {{0, 1, 2}};
    AuthoredDrawState drawable{};
    drawable.worldFromModel = translation(0.0f, 0.0f, -6.0f);
    drawable.mesh = MeshView{vertices, 3, triangles, 1, true};
    drawable.hasMaterial = true;
    drawable.materialColor = ColorRgba{1.0f, 1.0f, 1.0f, 1.0f};
    drawable.lit = true;

    assembler.begin(640.0f, 480.0f);
    // Ambient 0.5 plus a full-strength light would reach 1.5 without clamping.
    EXPECT(assembler.setLights(whiteLightFromPositiveZ(0.5f)) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);

    const ProjectedVertex& vertex = assembler.geometry().triangles[0].vertices[0];
    EXPECT(near(vertex.color.r, 1.0f));
    EXPECT(near(vertex.color.a, 1.0f));
}

void UnlitMaterialIgnoresLights() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(whiteLightFromPositiveZ(0.25f)) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    // validDrawable() has no normals and lit defaults to false.
    EXPECT(assembler.addDrawable(validDrawable()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);

    EXPECT(near(sink.lastFirstColor.r, 0.8f));
    EXPECT(near(sink.lastFirstColor.g, 0.1f));
    EXPECT(near(sink.lastFirstColor.b, 0.1f));
    EXPECT(!assembler.summary().lit);
}

void LitMeshWithoutNormalsFailsBeforePresentation() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    AuthoredDrawState drawable = validDrawable();
    drawable.lit = true;

    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(whiteLightFromPositiveZ()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::LitMeshWithoutNormals);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::LitMeshWithoutNormals);
    EXPECT(sink.presentCount == 0);
}

void FiveDirectionalLightsFail() {
    AuthoredFrameAssembler assembler;
    AuthoredLightState lights = whiteLightFromPositiveZ();
    lights.directionalCount = 5;

    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(lights) == AuthoredFrameStatus::TooManyDirectionalLights);

    RecordingSink sink;
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::TooManyDirectionalLights);
    EXPECT(sink.presentCount == 0);
}

void NonFiniteLightStateFails() {
    AuthoredFrameAssembler assembler;
    AuthoredLightState lights = whiteLightFromPositiveZ();
    lights.directional[0].color.g = std::nanf("");
    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(lights) == AuthoredFrameStatus::InvalidLight);

    AuthoredFrameAssembler second;
    AuthoredLightState zeroDirection = whiteLightFromPositiveZ();
    zeroDirection.directional[0].direction = Vec3{0.0f, 0.0f, 0.0f};
    second.begin(640.0f, 480.0f);
    EXPECT(second.setLights(zeroDirection) == AuthoredFrameStatus::InvalidLight);
}

void LitShadingSurvivesNonuniformScale() {
    AuthoredFrameAssembler assembler;
    RecordingSink sink;
    // Two quads: the first faces +Z (lit), the second faces -Z (unlit).
    const SourceVertex vertices[6] = {
        {{-1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{1.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{0.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 1.0f}},
        {{-1.0f, -1.0f, -0.5f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{1.0f, -1.0f, -0.5f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
        {{0.0f, 1.0f, -0.5f}, {1.0f, 1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, -1.0f}},
    };
    const TriangleIndex triangles[2] = {{0, 1, 2}, {3, 4, 5}};
    AuthoredDrawState drawable = twoFacingDrawable(vertices, triangles, true);
    // Nonuniform scale must not tilt the face normals.
    drawable.worldFromModel.m[1][1] = 1.25f;

    assembler.begin(640.0f, 480.0f);
    EXPECT(assembler.setLights(whiteLightFromPositiveZ()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.setCamera(validCamera()) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.addDrawable(drawable) == AuthoredFrameStatus::Ok);
    EXPECT(assembler.present(sink) == AuthoredFrameStatus::Ok);

    bool sawLit = false;
    bool sawDark = false;
    const GeometryFrame& frame = assembler.geometry();
    for (std::uint16_t i = 0; i < frame.triangleCount; ++i) {
        const float red = frame.triangles[i].vertices[0].color.r;
        sawLit = sawLit || near(red, 1.0f);
        sawDark = sawDark || near(red, 0.0f);
    }
    EXPECT(sawLit);
    EXPECT(sawDark);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run the Task 1 Step 2 docker command.
Expected: FAIL with "'AuthoredLightState' was not declared in this scope".

- [ ] **Step 3: Extend the assembler header**

In `src/platform/dreamcast/DreamcastAuthoredFrame.hpp`:

Add after the `AuthoredViewport` struct:

```cpp
/// Maximum simultaneously enabled directional lights for Dreamcast v0.3.
constexpr std::uint8_t kMaxDirectionalLights = 4;

/// One enabled directional light, resolved to world space by the caller.
struct AuthoredDirectionalLight {
    Vec3 direction;   // normalized world direction the light travels
    ColorRgba color;  // rgb premultiplied by intensity; alpha unused
};

/// Scene lighting for one frame. Carries no generated headers.
struct AuthoredLightState {
    ColorRgba ambient;
    std::uint8_t directionalCount;
    AuthoredDirectionalLight directional[kMaxDirectionalLights];
};
```

Add `bool lit;` as the last member of `AuthoredDrawState`, with this comment:

```cpp
    // True when the bound material asks for lighting; requires mesh.hasNormals.
    bool lit;
```

Add to `AuthoredFrameStatus`, before `GeometryFailed`:

```cpp
    TooManyDirectionalLights,
    InvalidLight,
    LitMeshWithoutNormals,
```

Add to `AuthoredFrameSummary`, after `drawableCount`:

```cpp
    std::uint8_t directionalLightCount;
    bool lit;
```

Add to the public section of `AuthoredFrameAssembler`, after `setCamera`:

```cpp
    AuthoredFrameStatus setLights(const AuthoredLightState& lights) noexcept;
```

Add to its private section, after `AuthoredCameraState camera_{};`:

```cpp
    AuthoredLightState lights_{};
```

- [ ] **Step 4: Implement lighting**

In `src/platform/dreamcast/DreamcastAuthoredFrame.cpp`:

Add to the anonymous namespace:

```cpp
bool finiteVec3(const Vec3& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

float clampUnit(float value) noexcept {
    if (!std::isfinite(value) || value <= 0.0f) {
        return 0.0f;
    }
    return value >= 1.0f ? 1.0f : value;
}
```

Add the three new status names to `authoredFrameStatusName`, before the `GeometryFailed` case:

```cpp
    case AuthoredFrameStatus::TooManyDirectionalLights:
        return "too-many-directional-lights";
    case AuthoredFrameStatus::InvalidLight:
        return "invalid-light";
    case AuthoredFrameStatus::LitMeshWithoutNormals:
        return "lit-mesh-without-normals";
```

In `begin`, after `summary_ = AuthoredFrameSummary{};`, add:

```cpp
    lights_ = AuthoredLightState{};
```

Add `setLights` after `setCamera`:

```cpp
AuthoredFrameStatus AuthoredFrameAssembler::setLights(const AuthoredLightState& lights) noexcept {
    const auto fail = [this](AuthoredFrameStatus status) noexcept {
        if (firstFailure_ == AuthoredFrameStatus::Ok) {
            firstFailure_ = status;
        }
        return status;
    };
    if (!began_) {
        return fail(AuthoredFrameStatus::NotStarted);
    }
    if (lights.directionalCount > kMaxDirectionalLights) {
        return fail(AuthoredFrameStatus::TooManyDirectionalLights);
    }
    if (!finiteColor(lights.ambient)) {
        return fail(AuthoredFrameStatus::InvalidLight);
    }
    for (std::uint8_t index = 0; index < lights.directionalCount; ++index) {
        const AuthoredDirectionalLight& light = lights.directional[index];
        const float lengthSquared = light.direction.x * light.direction.x +
                                    light.direction.y * light.direction.y +
                                    light.direction.z * light.direction.z;
        if (!finiteColor(light.color) || !finiteVec3(light.direction) || lengthSquared <= 1.0e-8f) {
            return fail(AuthoredFrameStatus::InvalidLight);
        }
    }
    lights_ = lights;
    summary_.directionalLightCount = lights.directionalCount;
    return AuthoredFrameStatus::Ok;
}
```

In `addDrawable`, replace the vertex copy loop and the surrounding validation so a lit mesh is rejected early and normals are carried into the scratch buffer:

```cpp
    if (drawable.lit && !drawable.mesh.hasNormals) {
        return fail(AuthoredFrameStatus::LitMeshWithoutNormals);
    }
    for (std::uint16_t index = 0; index < drawable.mesh.vertexCount; ++index) {
        coloredVertices_[index] = SourceVertex{drawable.mesh.vertices[index].position,
                                               drawable.materialColor,
                                               drawable.mesh.vertices[index].normal};
    }
    summary_.lit = drawable.lit;
```

In `present`, immediately after the `makeCameraFromModel` block and before building `GeometryConfig`, add the lighting pass:

```cpp
    if (drawable_.lit) {
        Matrix4x4 normalMatrix{};
        if (!makeNormalMatrix(drawable_.worldFromModel, normalMatrix)) {
            return AuthoredFrameStatus::InvalidModelTransform;
        }
        for (std::uint16_t index = 0; index < drawable_.mesh.vertexCount; ++index) {
            const Vec3 worldNormal = transformDirection(normalMatrix, coloredVertices_[index].normal);
            const float lengthSquared = worldNormal.x * worldNormal.x +
                                        worldNormal.y * worldNormal.y +
                                        worldNormal.z * worldNormal.z;
            float diffuseRed = lights_.ambient.r;
            float diffuseGreen = lights_.ambient.g;
            float diffuseBlue = lights_.ambient.b;
            if (lengthSquared > 1.0e-12f) {
                const float inverseLength = 1.0f / std::sqrt(lengthSquared);
                const Vec3 unitNormal{worldNormal.x * inverseLength,
                                      worldNormal.y * inverseLength,
                                      worldNormal.z * inverseLength};
                for (std::uint8_t light = 0; light < lights_.directionalCount; ++light) {
                    const AuthoredDirectionalLight& source = lights_.directional[light];
                    const float lambert = -(unitNormal.x * source.direction.x +
                                            unitNormal.y * source.direction.y +
                                            unitNormal.z * source.direction.z);
                    if (lambert <= 0.0f) {
                        continue;
                    }
                    diffuseRed += source.color.r * lambert;
                    diffuseGreen += source.color.g * lambert;
                    diffuseBlue += source.color.b * lambert;
                }
            }
            ColorRgba& color = coloredVertices_[index].color;
            color = ColorRgba{clampUnit(drawable_.materialColor.r * diffuseRed),
                              clampUnit(drawable_.materialColor.g * diffuseGreen),
                              clampUnit(drawable_.materialColor.b * diffuseBlue),
                              1.0f};
        }
    }
```

Note the light directions are already normalized by the caller (`setLights` rejects zero-length directions), so no renormalization of `source.direction` is performed.

- [ ] **Step 5: Run the tests to verify they pass**

Run the Task 1 Step 2 docker command.
Expected: `DreamcastAuthoredSubmissionTests: all tests passed` plus every other suite.

- [ ] **Step 6: Run the sanitizer build**

```bash
MSYS_NO_PATHCONV=1 docker run --rm --workdir /src \
  --mount "type=bind,source=C:/dev/helworks/helengine-dc,target=/src,readonly" \
  --mount "type=bind,source=C:/dev/helworks/builds/helengine-dc/v03/host-check,target=/out" \
  --entrypoint /bin/bash helengine-dc-sdk:v01 -lc \
  'c++ -std=c++17 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -Wall -Wextra -Werror -Isrc \
   tests/native/DreamcastAuthoredSubmissionTests.cpp \
   src/platform/dreamcast/DreamcastAuthoredFrame.cpp \
   src/platform/dreamcast/DreamcastAuthoredTransform.cpp \
   src/platform/dreamcast/DreamcastGeometryPipeline.cpp \
   src/platform/dreamcast/DreamcastCubeMesh.cpp \
   -o /out/submission-asan && ASAN_OPTIONS=detect_leaks=1 UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 /out/submission-asan; echo "asan-exit=$?"'
```

Expected: `all tests passed` followed by `asan-exit=0`.

- [ ] **Step 7: Commit**

```bash
git -c safe.directory=* add src/platform/dreamcast/DreamcastAuthoredFrame.hpp src/platform/dreamcast/DreamcastAuthoredFrame.cpp tests/native/DreamcastAuthoredSubmissionTests.cpp
git -c safe.directory=* commit -m "Light authored vertices in the frame assembler

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: Material lit flag and the dc-standard rename

**Files:**
- Modify: `builder/DreamcastMaterialSchemaIds.cs`
- Modify: `builder/DreamcastMaterialCooker.cs`
- Modify: `builder/DreamcastPlatformDefinitionFactory.cs`
- Modify: `builder/DreamcastCookedContentValidator.cs`
- Modify: `src/platform/dreamcast/DreamcastRuntimeMaterial.hpp`, `.cpp`
- Test: `builder.tests/DreamcastMaterialCookerTests.cs`, `builder.tests/DreamcastPlatformDefinitionTests.cs`, `builder.tests/DreamcastCookedContentValidatorTests.cs`, `builder.tests/DreamcastCookedTestContent.cs`, `builder.tests/DreamcastBuildFailureTests.cs`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces: `DreamcastMaterialSchemaIds.StandardSchemaId = "dc-standard-v1"`, `.StandardGraphicsProfileId = "dc-standard"`, `.LightingLitValue = "lit"`, `.LightingUnlitValue = "unlit"`; cooked `PlatformMaterialAsset.RendererFamilyId == "dc-standard"` with `Lit` following the authored choice; `DreamcastRuntimeMaterial::lit()`.

- [ ] **Step 1: Write the failing builder tests**

In `builder.tests/DreamcastPlatformDefinitionTests.cs`, replace the two profile assertions:

```csharp
        Assert.Equal("dc-standard", Assert.Single(builder.Definition.GraphicsProfiles).ProfileId);
        Assert.Equal("dc-standard-v1", Assert.Single(builder.Definition.MaterialSchemas).SchemaId);
```

In `builder.tests/DreamcastMaterialCookerTests.cs`:
- Replace `Assert.Equal("dc-unlit", cooked.RendererFamilyId);` with `Assert.Equal("dc-standard", cooked.RendererFamilyId);`.
- Replace both `"dc-unlit"` graphics-profile arguments in `CreateRequest` with `"dc-standard"`.
- Replace `DreamcastMaterialSchemaIds.UnlitSchemaId` with `DreamcastMaterialSchemaIds.StandardSchemaId`.
- Delete the `[InlineData("lit", "unlit lighting")]` row and its `mutation == "lit"` branch from `CookMaterial_RejectsUnsupportedState`.
- Change `CreateFields` to author lit by default: `[DreamcastMaterialSchemaIds.LightingModeFieldId] = "lit",`.
- In `CookMaterial_EmitsAuthoredOpaqueRed`, change `Assert.False(cooked.Lit);` to `Assert.True(cooked.Lit);`.
- Add these two tests:

```csharp
    /// <summary>Confirms an explicit unlit material still cooks with the flag cleared.</summary>
    [Fact]
    public void CookMaterial_EmitsExplicitUnlit() {
        Dictionary<string, string> fields = CreateFields("#C02010FF");
        fields[DreamcastMaterialSchemaIds.LightingModeFieldId] = "unlit";

        PlatformMaterialCookResult result = new DreamcastPlatformAssetBuilder().CookMaterial(CreateRequest(fields));

        helengine.PlatformMaterialAsset cooked = Assert.IsType<helengine.PlatformMaterialAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(result.CookedMaterialBytes));
        Assert.False(cooked.Lit);
    }

    /// <summary>Confirms a material with only the engine's common fields defaults to lit.</summary>
    [Fact]
    public void CookMaterial_DefaultsToLit() {
        Dictionary<string, string> fields = new(StringComparer.OrdinalIgnoreCase) {
            [DreamcastMaterialSchemaIds.BaseColorFieldId] = "#FFFFFFFF",
            ["casts-shadow"] = "false"
        };

        PlatformMaterialCookResult result = new DreamcastPlatformAssetBuilder().CookMaterial(CreateRequest(fields));

        helengine.PlatformMaterialAsset cooked = Assert.IsType<helengine.PlatformMaterialAsset>(helengine.files.AssetSerializer.DeserializeFromBytes(result.CookedMaterialBytes));
        Assert.True(cooked.Lit);
    }
```

In `builder.tests/DreamcastCookedContentValidatorTests.cs`:
- Delete the `[InlineData("lit", "unlit")]` row and the `mutation == "lit"` branch from `RejectsUnsupportedMaterial`.
- Add:

```csharp
    /// <summary>Lit materials are supported and the light components that drive them are allowed.</summary>
    [Fact]
    public void AcceptsLitMaterialAndLightComponents() {
        using TemporaryDirectory temporary = new();
        PlatformMaterialAsset material = DreamcastCookedTestContent.CreateMaterial();
        material.Lit = true;
        Stage(temporary.Path, material: material, extraEntities: [
            DreamcastCookedTestContent.CreateEntity(3, true, [DreamcastCookedTestContent.CreateComponent("helengine.DirectionalLightComponent")]),
            DreamcastCookedTestContent.CreateEntity(4, true, [DreamcastCookedTestContent.CreateComponent("helengine.AmbientLightComponent")])
        ]);

        Validate(temporary.Path);
    }
```

In `builder.tests/DreamcastCookedTestContent.cs`, change the material factory's family to `"dc-standard"` and its summary to "Creates the opaque red dc-standard material."

In `builder.tests/DreamcastBuildFailureTests.cs`, replace the three `"dc-unlit"` literals with `"dc-standard"`.

- [ ] **Step 2: Run the tests to verify they fail**

```powershell
cd C:\dev\helworks\helengine-dc
dotnet test builder.tests/helengine.dreamcast.builder.tests.csproj --artifacts-path C:/dev/helworks/builds/helengine-dc/v02/dotnet-main -p:HelengineRoot=C:/dev/helworks/helengine -v:minimal
```

Expected: compile errors for `StandardSchemaId` and assertion failures on the renderer family.

- [ ] **Step 3: Rename the schema ids**

Replace the contents of `builder/DreamcastMaterialSchemaIds.cs`'s constants block so it reads:

```csharp
    /// <summary>Stable Dreamcast material schema id.</summary>
    public const string StandardSchemaId = "dc-standard-v1";

    /// <summary>Stable Dreamcast graphics profile and cooked renderer family id.</summary>
    public const string StandardGraphicsProfileId = "dc-standard";

    /// <summary>Authored lighting choice that enables per-vertex lighting.</summary>
    public const string LightingLitValue = "lit";

    /// <summary>Authored lighting choice that keeps the flat base color.</summary>
    public const string LightingUnlitValue = "unlit";
```

Keep the existing `BaseColorFieldId`, `TextureRelativePathFieldId`, `LightingModeFieldId`, `TransparencyFieldId`, `DoubleSidedFieldId` and `VertexColorModeFieldId` constants unchanged.

- [ ] **Step 4: Update the cooker**

In `builder/DreamcastMaterialCooker.cs`:
- `request.SchemaId` check uses `DreamcastMaterialSchemaIds.StandardSchemaId`.
- Graphics profile check compares against `DreamcastMaterialSchemaIds.StandardGraphicsProfileId` and its message names `'dc-standard'`.
- `lightingMode` default becomes `DreamcastMaterialSchemaIds.LightingLitValue`.
- Replace the unlit rejection with a parse:

```csharp
        bool lit;
        if (string.Equals(lightingMode, DreamcastMaterialSchemaIds.LightingLitValue, StringComparison.OrdinalIgnoreCase)) {
            lit = true;
        } else if (string.Equals(lightingMode, DreamcastMaterialSchemaIds.LightingUnlitValue, StringComparison.OrdinalIgnoreCase)) {
            lit = false;
        } else {
            throw new InvalidOperationException($"Dreamcast dc-standard-v1 lighting mode must be 'lit' or 'unlit', received '{lightingMode}'.");
        }
```

- The cooked asset becomes:

```csharp
            RendererFamilyId = DreamcastMaterialSchemaIds.StandardGraphicsProfileId,
            ...
            Lit = lit,
```

- Every remaining `dc-unlit-v1` in a message becomes `dc-standard-v1`.

- [ ] **Step 5: Update the definition factory**

In `builder/DreamcastPlatformDefinitionFactory.cs`:
- schema id argument becomes `DreamcastMaterialSchemaIds.StandardSchemaId`;
- the renderer family list becomes `[DreamcastMaterialSchemaIds.StandardGraphicsProfileId]`;
- the lighting field becomes:

```csharp
                new(DreamcastMaterialSchemaIds.LightingModeFieldId, "Lighting", PlatformMaterialFieldKind.Choice, DreamcastMaterialSchemaIds.LightingLitValue, true, [DreamcastMaterialSchemaIds.LightingLitValue, DreamcastMaterialSchemaIds.LightingUnlitValue]),
```

- both build profiles pass `DreamcastMaterialSchemaIds.StandardGraphicsProfileId`;
- the graphics profile becomes:

```csharp
            [new PlatformGraphicsProfileDefinition(DreamcastMaterialSchemaIds.StandardGraphicsProfileId, "Dreamcast Standard", "PVR opaque renderer with per-vertex lighting", [])],
```

- [ ] **Step 6: Update the cooked-content validator**

In `builder/DreamcastCookedContentValidator.cs`:
- `RendererFamilyId` constant becomes `"dc-standard"`;
- delete the `material.Lit` rejection branch;
- the "no material" message becomes `"Staged package contains no dc-standard material; the startup mesh has no material."`;
- add light component ids next to the camera and mesh ids:

```csharp
    const string DirectionalLightTypeId = "helengine.DirectionalLightComponent";
    const string AmbientLightTypeId = "helengine.AmbientLightComponent";
```

and in `CountComponents`, allow them before the catch-all rejection:

```cpp
                } else if (string.Equals(component.ComponentTypeId, DirectionalLightTypeId, StringComparison.Ordinal) ||
                           string.Equals(component.ComponentTypeId, AmbientLightTypeId, StringComparison.Ordinal)) {
                    continue;
```

(The surrounding method is C#; the fenced language above is incidental.)

- [ ] **Step 7: Run the builder tests to verify they pass**

Run the Step 2 command.
Expected: `Passed!` with 0 failures.

- [ ] **Step 8: Update the native material**

In `src/platform/dreamcast/DreamcastRuntimeMaterial.hpp`, add to the public section:

```cpp
    /// True when the authored material asks for per-vertex lighting.
    bool lit() const noexcept;
```

and to the private section:

```cpp
    bool lit_ = false;
```

In `src/platform/dreamcast/DreamcastRuntimeMaterial.cpp`, replace the validation head of `Create`:

```cpp
    if (asset.RendererFamilyId != "dc-standard" || !asset.TextureRelativePath.empty() ||
        asset.DoubleSided || asset.UseVertexColor || asset.BaseColorA != 255) {
        throw std::invalid_argument("Dreamcast material does not match the opaque dc-standard contract.");
    }
```

and inside the `try` block, after `runtime->set_Id(asset.Id);`, add:

```cpp
        runtime->lit_ = asset.Lit;
```

Change the constructor's lighting model to follow the flag by replacing `set_LightingModel(::RuntimeMaterialLightingModel::Unlit);` in the constructor with nothing, and add after `runtime->lit_ = asset.Lit;`:

```cpp
        runtime->set_LightingModel(asset.Lit ? ::RuntimeMaterialLightingModel::Lit
                                             : ::RuntimeMaterialLightingModel::Unlit);
```

Add the accessor at the end of the file, before the namespace close:

```cpp
bool DreamcastRuntimeMaterial::lit() const noexcept {
    return lit_;
}
```

If `::RuntimeMaterialLightingModel::Lit` is not a member of the generated enum, inspect `RuntimeMaterialLightingModel.hpp` in the current generated root and use the enumerator that means lit; do not invent one.

- [ ] **Step 9: Commit**

```bash
git -c safe.directory=* add builder src/platform/dreamcast/DreamcastRuntimeMaterial.hpp src/platform/dreamcast/DreamcastRuntimeMaterial.cpp builder.tests
git -c safe.directory=* commit -m "Support lit materials and rename the platform ids to dc-standard

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: Gather lights in the render manager and publish them

**Files:**
- Modify: `src/platform/dreamcast/DreamcastRenderManager3D.cpp`
- Modify: `src/platform/dreamcast/DreamcastRuntimeHost.hpp`, `.cpp`
- Modify: `scripts/verify-authored-scene.ps1`

**Interfaces:**
- Consumes: `AuthoredLightState`, `setLights`, `AuthoredDrawState::lit` (Task 3); `DreamcastRuntimeMaterial::lit()` (Task 4); `directionalLightForward`, `GeneratedQuaternion` (Task 2).
- Produces: authored snapshot version 2 whose sixteenth 32-bit word is `directionalLightCount`.

- [ ] **Step 1: Gather lights in Draw**

In `src/platform/dreamcast/DreamcastRenderManager3D.cpp`, add these includes:

```cpp
#include "AmbientLightComponent.hpp"
#include "DirectionalLightComponent.hpp"
#include "LightComponent.hpp"
```

Add to the anonymous namespace:

```cpp
/// Reads the enabled scene lights into the generated-header-free light state.
/// Ambient contributions sum; directional lights keep authored order and are
/// capped by the assembler, which fails rather than dropping a fifth light.
AuthoredLightState gatherLights(::ObjectManager& objectManager) {
    AuthoredLightState lights{};
    lights.ambient = ColorRgba{0.0f, 0.0f, 0.0f, 1.0f};
    ::List<::AmbientLightComponent*>* ambientLights = objectManager.get_AmbientLights();
    if (ambientLights != nullptr) {
        for (int32_t index = 0; index < ambientLights->get_Count(); ++index) {
            ::AmbientLightComponent* light = ambientLights->get_Item(index);
            if (light == nullptr) {
                continue;
            }
            ::Entity* owner = light->get_Parent();
            if (owner == nullptr || !owner->get_IsHierarchyEnabled()) {
                continue;
            }
            const ::float4 color = light->get_Color();
            const float intensity = light->get_Intensity();
            lights.ambient.r += color.X * intensity;
            lights.ambient.g += color.Y * intensity;
            lights.ambient.b += color.Z * intensity;
        }
    }

    ::List<::DirectionalLightComponent*>* directionalLights = objectManager.get_DirectionalLights();
    if (directionalLights != nullptr) {
        for (int32_t index = 0; index < directionalLights->get_Count(); ++index) {
            ::DirectionalLightComponent* light = directionalLights->get_Item(index);
            if (light == nullptr) {
                continue;
            }
            ::Entity* owner = light->get_Parent();
            if (owner == nullptr || !owner->get_IsHierarchyEnabled()) {
                continue;
            }
            if (lights.directionalCount >= kMaxDirectionalLights) {
                // Report the real count so the assembler fails explicitly.
                lights.directionalCount = static_cast<std::uint8_t>(kMaxDirectionalLights + 1);
                return lights;
            }
            const ::float4 orientation = owner->get_Orientation();
            const Vec3 forward = directionalLightForward(
                GeneratedQuaternion{orientation.X, orientation.Y, orientation.Z, orientation.W});
            const float lengthSquared =
                forward.x * forward.x + forward.y * forward.y + forward.z * forward.z;
            if (!(lengthSquared > 1.0e-12f)) {
                continue;
            }
            const float inverseLength = 1.0f / std::sqrt(lengthSquared);
            const ::float4 color = light->get_Color();
            const float intensity = light->get_Intensity();
            lights.directional[lights.directionalCount] = AuthoredDirectionalLight{
                Vec3{forward.x * inverseLength, forward.y * inverseLength, forward.z * inverseLength},
                ColorRgba{color.X * intensity, color.Y * intensity, color.Z * intensity, 1.0f}};
            ++lights.directionalCount;
        }
    }
    return lights;
}
```

Add `#include <cmath>` to that file's include block.

In `DreamcastRenderManager3D::Draw`, immediately after `assembler_.begin(...)`, add:

```cpp
    AuthoredFrameStatus status = assembler_.setLights(gatherLights(*objectManager));
```

and change the existing declaration `AuthoredFrameStatus status = AuthoredFrameStatus::Ok;` to an assignment-free form so the variable is declared once. The camera loop's condition `status == AuthoredFrameStatus::Ok` is unchanged, so a light failure skips the loop and surfaces from `present`.

In `DrawableCollector::Visit`, set the lit flag from the bound material:

```cpp
        state.hasMaterial = material != nullptr;
        if (material != nullptr) {
            state.materialColor = material->color();
            state.lit = material->lit();
        }
```

- [ ] **Step 2: Publish the light count in the authored snapshot**

In `src/platform/dreamcast/DreamcastRuntimeHost.hpp`:
- rename the snapshot's `reserved` member to `directionalLightCount`;
- change `kDreamcastAuthoredSnapshotVersion` to `2u`;
- keep the `static_assert(sizeof(DreamcastAuthoredSnapshot) == 64, ...)`.

In `src/platform/dreamcast/DreamcastRuntimeHost.cpp`:
- in `resetAuthoredSnapshot`, replace `snapshot.reserved = 0u;` with `snapshot.directionalLightCount = 0u;`;
- in `publishAuthoredFrame`, add:

```cpp
    snapshot.directionalLightCount = summary.directionalLightCount;
```

- in the first-frame `dbglog` call, add `lights=%u` to the format string and `static_cast<unsigned>(summary.directionalLightCount)` to the arguments.

- [ ] **Step 3: Print the light count in the verifier probe**

In `scripts/verify-authored-scene.ps1`, inside the Lua `authoredText` function, append ` lights=%u` to the format string and `a[16]` to its argument list.

- [ ] **Step 4: Cross-compile to verify**

Run the Task 1 Step 6 command with `-BuildId v03-task5`.
Expected: "Built ...helengine_dc.elf" with a SHA256 line and no compiler errors.

- [ ] **Step 5: Confirm the symbol layout is still 64 bytes**

```bash
MSYS_NO_PATHCONV=1 docker run --rm \
  --mount "type=bind,source=C:/dev/helworks/builds/helengine-dc/v03/v03-task5,target=/out,readonly" \
  --entrypoint /bin/bash helengine-dc-sdk:v01 -lc \
  '/opt/toolchains/dc/sh-elf/bin/sh-elf-nm --print-size /out/helengine_dc.elf | grep helengine_dreamcast_authored_snapshot'
```

Expected: a `B` symbol with size `40` (hexadecimal 0x40 = 64 bytes).

- [ ] **Step 6: Commit**

```bash
git -c safe.directory=* add src/platform/dreamcast/DreamcastRenderManager3D.cpp src/platform/dreamcast/DreamcastRuntimeHost.hpp src/platform/dreamcast/DreamcastRuntimeHost.cpp scripts/verify-authored-scene.ps1
git -c safe.directory=* commit -m "Gather scene lights and publish the directional light count

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: Fixture lit variant and demodisc project changes

**Files:**
- Modify: `tools/helengine.dreamcast.fixture/DreamcastAuthoredSceneFixtureWriter.cs`
- Modify: `tools/helengine.dreamcast.fixture/Program.cs`
- Modify (outside this repo, left uncommitted): `C:\dev\helprojs\demodisc\assets\codebase\rendering.tools\CubeTestSceneFactory.cs`, `C:\dev\helprojs\demodisc\user_settings\build_config.json`

**Interfaces:**
- Consumes: the renamed schema ids (Task 4).
- Produces: fixture variant `lit`; `lit-material` removed from `NegativeVariants`.

- [ ] **Step 1: Rename ids and retarget the variants in the fixture writer**

In `tools/helengine.dreamcast.fixture/DreamcastAuthoredSceneFixtureWriter.cs`:
- `platform.SelectedGraphicsProfileId = "dc-standard";`
- both `SchemaId = "dc-standard-v1"` assignments;
- replace `lit-material` with `lit` in `SupportedVariants`, and delete `lit-material` from `NegativeVariants` (it is now a supported configuration, so it can no longer be a negative);
- the lighting field line becomes:

```csharp
        document.Processor.FieldValues["lighting-mode"] = variant == "lit" ? "lit" : "unlit";
```

so every pre-existing variant keeps its unlit meaning and only `lit` opts in.

- [ ] **Step 2: Author a directional light in the lit variant**

In the same file, inside `CreateSceneBytes`, after the `two-cameras` block, add:

```csharp
        if (variant == "lit") {
            float4 sunOrientation;
            float4.CreateFromYawPitchRoll(-0.65f, -0.85f, 0f, out sunOrientation);
            DirectionalLightComponent sun = new() {
                Color = new float4(1f, 1f, 1f, 1f),
                Intensity = 1f,
                ShadowsEnabled = false,
                ShadowMapMode = ShadowMapMode.Forced,
                ShadowStrength = 1f,
                ShadowDistance = 24f
            };
            SceneEntityAsset sunEntity = CreateEntity("sun", "Fixture Sun", new float3(0f, 4f, 0f), float3.One, [CreateComponent("helengine.DirectionalLightComponent", authoring.CreateLightPayload(sun))]);
            sunEntity.LocalOrientation = sunOrientation;
            rootEntities.Add(sunEntity);
        }
```

The angles match demodisc `cube_test`, so the fixture and the real project light the cube the same way.

- [ ] **Step 3: Update the usage string**

In `tools/helengine.dreamcast.fixture/Program.cs`, replace `lit-material` with `lit` in the usage message.

- [ ] **Step 4: Rebuild the fixture tool**

```powershell
cd C:\dev\helworks\helengine-dc
dotnet build tools/helengine.dreamcast.fixture/helengine.dreamcast.fixture.csproj --artifacts-path C:/dev/helworks/builds/helengine-dc/v02/dotnet-fixture -p:HelengineRoot=C:/dev/helworks/helengine -v:minimal
```

Expected: `0 Error(s)`.

- [ ] **Step 5: Verify the lit variant writes a light**

```powershell
& C:/dev/helworks/builds/helengine-dc/v02/dotnet-fixture/bin/helengine.dreamcast.fixture/debug/helengine.dreamcast.fixture.exe --output C:\dev\helworks\builds\helengine-dc\v03\fixtures\lit --variant lit
Select-String -Path C:\dev\helworks\builds\helengine-dc\v03\fixtures\lit\assets\scenes\startup.helen -Pattern 'DirectionalLightComponent' -Encoding Byte | Measure-Object | Select-Object Count
```

Expected: exit code 0 and a nonzero match count.

- [ ] **Step 6: Commit the repo-side changes**

```bash
git -c safe.directory=* add tools/helengine.dreamcast.fixture
git -c safe.directory=* commit -m "Add a lit fixture variant and adopt the dc-standard ids

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

- [ ] **Step 7: Split the demodisc exclusion sets**

In `C:\dev\helprojs\demodisc\assets\codebase\rendering.tools\CubeTestSceneFactory.cs`, replace the single exclusion array with two, keeping the existing `Nintendo64PlatformId` and `DreamcastPlatformId` constants:

```csharp
        /// <summary>
        /// Targets whose runtimes support neither the instruction overlay, the UI root nor the orbit camera.
        /// </summary>
        static readonly string[] MinimalRuntimeExcludedPlatformIds = [Nintendo64PlatformId, DreamcastPlatformId];

        /// <summary>
        /// Targets without any lighting support. Dreamcast lights the cube, so only N64 drops the sun.
        /// </summary>
        static readonly string[] UnlitRuntimeExcludedPlatformIds = [Nintendo64PlatformId];
```

Change `ExcludeN64Root` and `ExcludeN64OrbitComponent` to take the platform array as a parameter, then call them as:

```csharp
            ExcludeN64Root(projectRootPath, instructionOverlayEntity, MinimalRuntimeExcludedPlatformIds);
            ExcludeN64Root(projectRootPath, consoleInstructionBlueprintEntity, MinimalRuntimeExcludedPlatformIds);
            ExcludeN64Root(projectRootPath, uiEntity, MinimalRuntimeExcludedPlatformIds);
            ExcludeN64Root(projectRootPath, directionalLightEntity, UnlitRuntimeExcludedPlatformIds);
            ExcludeN64OrbitComponent(projectRootPath, cameraEntity, MinimalRuntimeExcludedPlatformIds);
```

Note the directional light entity is now excluded explicitly (it was not excluded at all before this plan; it was simply absent from the `dc` cook because the whole light root was excluded on `n64` only — verify the current call list before editing and keep N64's behaviour identical).

- [ ] **Step 8: Rebuild project code and regenerate the scene**

```powershell
& 'C:\Program Files\dotnet\dotnet.exe' build 'C:\dev\helprojs\demodisc\user_settings\generated_code\projects\menu.tools\menu.tools.csproj' -v:minimal
& 'C:\Program Files\dotnet\dotnet.exe' 'C:\dev\helworks\helengine\helengine.ui\helengine.editor.app\bin\Debug\net9.0-windows\helengine.editor.app.dll' --project 'C:\dev\helprojs\demodisc\project.heproj' --editor-command menu.generate-cube-test-scene
(Get-FileHash 'C:\dev\helprojs\demodisc\assets\scenes\rendering\cube_test.helen' -Algorithm SHA256).Hash.ToLowerInvariant()
```

Expected: "executed successfully" and a new hash. Record it.

- [ ] **Step 9: Point the dc build entry at the regenerated scene**

In `C:\dev\helprojs\demodisc\user_settings\build_config.json`, in the `dc` platform entry, set `selectedGraphicsProfileId` to `"dc-standard"` and `selectedSceneReferences[0].contentHash` to `"sha256:<hash from step 8>"`.

These demodisc edits stay uncommitted; that repository carries other unrelated uncommitted work.

---

### Task 7: End-to-end evidence and documentation

**Files:**
- Modify: `docs/AuthoredSceneBuild.md`, `docs/Validation.md`, `docs/GeneratedRuntimeContract.md`

**Interfaces:**
- Consumes: everything above.
- Produces: no code.

- [ ] **Step 1: Build cube_test through the graph**

```powershell
$pwsh = 'C:\Users\Helena\.cache\codex-runtimes\codex-primary-runtime\dependencies\native\powershell\pwsh.exe'
cd C:\dev\helworks\helengine-dc
& $pwsh -NoProfile -File .\scripts\prepare-authored-fixture.ps1 `
  -FixtureRoot C:\dev\helprojs\demodisc `
  -OutputRoot C:\dev\helworks\builds\helengine-dc\v03\cube-test\graph-1 `
  -CacheRoot C:\dev\helworks\builds\helengine-dc\v03\cube-test\cache `
  -EngineRoot C:\dev\helworks\helengine `
  -EngineVersion 1.0.0+ec2d7ff8a5b2624bbc51784bd9eb0c9babed1843 `
  -BuilderAssemblyPath C:\dev\helworks\builds\helengine-dc\v02\dotnet-main\bin\helengine.dreamcast.builder\debug\helengine.dreamcast.builder.dll `
  -CodegenToolPath C:\dev\helworks\builds\helengine-dc\v02\csharpcodegen-source\codegen\bin\Release\net9.0\codegen.exe `
  -Build
```

Rebuild the builder into `v02\dotnet-main` first if Task 4 changed it after the last build.
Expected: `BuildExitCode: 0` and an `authored-<guid>` directory under `...\graph-1\build`.

- [ ] **Step 2: Verify in a hidden bounded emulator session**

```powershell
& $pwsh -NoProfile -File .\scripts\verify-authored-scene.ps1 `
  -BuildRoot C:\dev\helworks\builds\helengine-dc\v03\cube-test\graph-1\build\authored-<guid> `
  -FlycastPath C:\dev\helworks\emus\dreamcast\flycast.exe `
  -EvidenceRoot C:\dev\helworks\builds\helengine-dc\v03\cube-test\verify-1 `
  -CaptureFrames "120,180"
```

Expected: `Result=success`, `lights=1` in the running lines, and two `Frame` rows whose sha256 values differ.

- [ ] **Step 3: Prove the shading is visible**

```bash
python - <<'EOF'
import numpy as np
for frame in (120, 180):
    path = f'C:/dev/helworks/builds/helengine-dc/v03/cube-test/verify-1/frame-{frame:04d}.raw'
    data = np.fromfile(path, dtype=np.uint16)[:640*480]
    values, counts = np.unique(data, return_counts=True)
    order = np.argsort(counts)[::-1][:6]
    print(frame, [(hex(int(values[i])), int(counts[i])) for i in order])
EOF
```

Expected: at least two distinct nonbackground gray levels with meaningful pixel counts, proving per-face shading rather than one flat color. Record the values.

- [ ] **Step 4: Verify the lit fixture variant**

Build the `lit` fixture through the graph the same way (its own `-OutputRoot`, shared `-CacheRoot`) and verify it. Expected: `Result=success` and `lights=1`.

- [ ] **Step 5: Confirm Diagnostic is unchanged**

Build Diagnostic with `-StaticPose`, run the diagnostic Lua probe from `C:\dev\helworks\builds\helengine-dc\v02\execution\task5-diagnostic-probe-01` against the new ELF (copy the profile to a fresh directory and update the symbol address with pinned `sh-elf-nm` first), and hash the captured frame.
Expected: `2c13dc96efabe106aae6beac98426f32eea4d8637e18360a8f7efccbde2ae804`.

- [ ] **Step 6: Launch visibly for the user**

```powershell
& $pwsh -NoProfile -File .\scripts\launch_in_emulator.ps1 `
  -BuildId authored-<guid> `
  -OutputRoot C:\dev\helworks\builds\helengine-dc\v03\cube-test\graph-1\build `
  -FlycastPath C:\dev\helworks\emus\dreamcast\flycast.exe -Visible
```

Confirm the process owner is `DESKTOP-HELENA\Helena` and the main window handle is nonzero.

- [ ] **Step 7: Update the documentation**

- `docs/AuthoredSceneBuild.md`: the supported subset gains directional and ambient lights with the formula, the four-light cap, the lit-without-normals failure, and the renamed ids; the cube_test section notes that the sun is now included on `dc`.
- `docs/Validation.md`: a v0.3 section with the build identity, the ELF hash, the verifier result, the two frame hashes, the observed shading levels, the lit fixture row and the Diagnostic regression row.
- `docs/GeneratedRuntimeContract.md`: the material section records `dc-standard`, the `Lit` flag, the light gathering ABI (`ObjectManager::get_DirectionalLights`, `get_AmbientLights`, `Entity::get_Orientation`, `LightComponent::get_Color`, `get_Intensity`) and authored snapshot version 2.

- [ ] **Step 8: Commit**

```bash
git -c safe.directory=* add docs
git -c safe.directory=* commit -m "Record Dreamcast lighting evidence and contract

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>"
```

---

## Self-review notes

- Spec coverage: data/material contract (Tasks 1, 4), light gathering (Task 5), lighting math (Tasks 2, 3), diagnostics (Task 5), builder and validation (Task 4), project and fixture changes (Task 6), testing and evidence (Tasks 1-3, 7).
- Two deliberate refinements of the spec, both narrowing toward the engine's own behaviour:
  1. Light direction is derived from the entity's world orientation quaternion through the engine's `float4.RotateVector` formula rather than the world matrix, because that is exactly what the GameCube renderer does and the spec's stated intent is GameCube parity. The formula lives in a pure host-testable helper.
  2. `SourceVertex::normal` and `MeshView::hasNormals` are declared last rather than mid-struct, so existing aggregate initializers keep compiling; the spec's ordering was incidental.
- `InvalidLight` is added beyond the spec's two named failures so non-finite or zero-length light data fails explicitly instead of silently producing black.
