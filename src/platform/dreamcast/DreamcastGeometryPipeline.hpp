#pragma once

#include <cstdint>

namespace helengine::dreamcast {

struct Vec3 {
    float x;
    float y;
    float z;
};

struct ColorRgba {
    float r;
    float g;
    float b;
    float a;
};

struct SourceVertex {
    Vec3 position;
    ColorRgba color;
    // Model-space normal. Declared last so existing position/color aggregate
    // initializers keep compiling and zero-fill it; only lit draws read it.
    Vec3 normal;
};

struct TriangleIndex {
    std::uint16_t a;
    std::uint16_t b;
    std::uint16_t c;
};

struct MeshView {
    const SourceVertex* vertices;
    std::uint16_t vertexCount;
    const TriangleIndex* triangles;
    std::uint16_t triangleCount;
    // True only when every vertex carries an authored model-space normal.
    bool hasNormals;
};

// Row-major storage. Transform with cameraPoint = cameraFromModel * [x,y,z,1].
struct Matrix4x4 {
    float m[4][4];
};

struct GeometryConfig {
    float viewportWidth;
    float viewportHeight;
    float verticalFovRadians;
    float nearPlane;
    float farPlane;
};

struct ProjectedVertex {
    float x;
    float y;
    float z; // q = 1 / positive camera-space depth
    ColorRgba color;
};

struct ProjectedTriangle {
    ProjectedVertex vertices[3];
};

struct GeometryFrame {
    static constexpr std::uint16_t kMaxTriangles = 512;
    ProjectedTriangle triangles[kMaxTriangles];
    std::uint16_t triangleCount;
    std::uint16_t rejectedTriangleCount;
    std::uint16_t clippedTriangleCount;
    std::uint16_t emittedVertexCount;
    bool overflow;
};

enum class GeometryStatus : std::uint8_t {
    Ok,
    InvalidConfig,
    NonFiniteInput,
    OutputCapacityExceeded,
    InvalidMesh,
};

void resetGeometryFrame(GeometryFrame& frame) noexcept;

GeometryStatus processGeometry(const MeshView& mesh,
                               const Matrix4x4& cameraFromModel,
                               const GeometryConfig& config,
                               GeometryFrame& output) noexcept;

MeshView cubeMesh() noexcept;

} // namespace helengine::dreamcast
