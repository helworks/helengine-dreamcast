#include "platform/dreamcast/DreamcastGeometryPipeline.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>

namespace {

using helengine::dreamcast::ColorRgba;
using helengine::dreamcast::MeshView;
using helengine::dreamcast::GeometryConfig;
using helengine::dreamcast::GeometryFrame;
using helengine::dreamcast::GeometryStatus;
using helengine::dreamcast::Matrix4x4;
using helengine::dreamcast::SourceVertex;
using helengine::dreamcast::TriangleIndex;
using helengine::dreamcast::Vec3;

int g_failures = 0;

void expect(bool condition, const char* expression, const char* testName) {
    if (!condition) {
        std::cerr << testName << ": expected " << expression << '\n';
        ++g_failures;
    }
}

#define EXPECT(condition) expect((condition), #condition, testName)

constexpr ColorRgba kWhite{1.0f, 1.0f, 1.0f, 1.0f};

Matrix4x4 identityMatrix() {
    return Matrix4x4{{
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    }};
}

GeometryConfig defaultConfig() {
    return GeometryConfig{640.0f, 480.0f, 1.57079632679f, 0.5f, 100.0f};
}

MeshView oneTriangle(const SourceVertex (&vertices)[3]) {
    static const TriangleIndex indices{0, 1, 2};
    return MeshView{vertices, 3, &indices, 1};
}

bool finiteTriangle(const helengine::dreamcast::ProjectedTriangle& triangle) {
    for (const auto& vertex : triangle.vertices) {
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
            !std::isfinite(vertex.z) || vertex.z <= 0.0f ||
            !std::isfinite(vertex.color.r) || !std::isfinite(vertex.color.g) ||
            !std::isfinite(vertex.color.b) || !std::isfinite(vertex.color.a)) {
            return false;
        }
    }
    return true;
}

void CenteredTriangleProjectsToViewportCenter() {
    constexpr const char* testName = "CenteredTriangleProjectsToViewportCenter";
    const SourceVertex vertices[] = {
        {{-1.0f, -1.0f, 4.0f}, kWhite},
        {{1.0f, -1.0f, 4.0f}, kWhite},
        {{0.0f, 0.0f, 4.0f}, kWhite},
    };
    GeometryFrame frame{};
    const GeometryStatus status = helengine::dreamcast::processGeometry(
        oneTriangle(vertices), identityMatrix(), defaultConfig(), frame);

    EXPECT(status == GeometryStatus::Ok);
    EXPECT(frame.triangleCount == 1);
    EXPECT(frame.triangles[0].vertices[2].x == 320.0f);
    EXPECT(frame.triangles[0].vertices[2].y == 240.0f);
    EXPECT(frame.triangles[0].vertices[2].z == 0.25f);
}

void FartherDepthProducesSmallerQ() {
    constexpr const char* testName = "FartherDepthProducesSmallerQ";
    const SourceVertex nearVertices[] = {
        {{-1.0f, -1.0f, 4.0f}, kWhite},
        {{1.0f, -1.0f, 4.0f}, kWhite},
        {{0.0f, 1.0f, 4.0f}, kWhite},
    };
    const SourceVertex farVertices[] = {
        {{-1.0f, -1.0f, 8.0f}, kWhite},
        {{1.0f, -1.0f, 8.0f}, kWhite},
        {{0.0f, 1.0f, 8.0f}, kWhite},
    };
    GeometryFrame nearFrame{};
    GeometryFrame farFrame{};
    EXPECT(helengine::dreamcast::processGeometry(
                oneTriangle(nearVertices), identityMatrix(), defaultConfig(), nearFrame) ==
            GeometryStatus::Ok);
    EXPECT(helengine::dreamcast::processGeometry(
                oneTriangle(farVertices), identityMatrix(), defaultConfig(), farFrame) ==
            GeometryStatus::Ok);
    EXPECT(nearFrame.triangleCount == 1);
    EXPECT(farFrame.triangleCount == 1);
    EXPECT(farFrame.triangles[0].vertices[0].z == nearFrame.triangles[0].vertices[0].z * 0.5f);
}

void OneOutsideNearPlaneEmitsTwoTriangles() {
    constexpr const char* testName = "OneOutsideNearPlaneEmitsTwoTriangles";
    const SourceVertex vertices[] = {
        {{-0.5f, -0.5f, 0.25f}, kWhite},
        {{0.5f, -0.5f, 2.0f}, kWhite},
        {{0.0f, 0.5f, 2.0f}, kWhite},
    };
    GeometryFrame frame{};
    const GeometryStatus status = helengine::dreamcast::processGeometry(
        oneTriangle(vertices), identityMatrix(), defaultConfig(), frame);

    EXPECT(status == GeometryStatus::Ok);
    EXPECT(frame.triangleCount == 2);
    EXPECT(frame.clippedTriangleCount == 1);
    EXPECT(frame.emittedVertexCount == 6);
    for (std::uint16_t i = 0; i < frame.triangleCount; ++i) {
        EXPECT(finiteTriangle(frame.triangles[i]));
    }
}

void TwoOutsideNearPlaneEmitsOneTriangle() {
    constexpr const char* testName = "TwoOutsideNearPlaneEmitsOneTriangle";
    const SourceVertex vertices[] = {
        {{-0.5f, -0.5f, 0.25f}, kWhite},
        {{0.5f, -0.5f, 0.25f}, kWhite},
        {{0.0f, 0.5f, 2.0f}, kWhite},
    };
    GeometryFrame frame{};
    const GeometryStatus status = helengine::dreamcast::processGeometry(
        oneTriangle(vertices), identityMatrix(), defaultConfig(), frame);

    EXPECT(status == GeometryStatus::Ok);
    EXPECT(frame.triangleCount == 1);
    EXPECT(frame.clippedTriangleCount == 1);
    EXPECT(frame.emittedVertexCount == 3);
    EXPECT(finiteTriangle(frame.triangles[0]));
}

void FullyBehindNearPlaneIsRejected() {
    constexpr const char* testName = "FullyBehindNearPlaneIsRejected";
    const SourceVertex vertices[] = {
        {{-0.5f, -0.5f, 0.25f}, kWhite},
        {{0.5f, -0.5f, 0.25f}, kWhite},
        {{0.0f, 0.5f, 0.25f}, kWhite},
    };
    GeometryFrame frame{};
    const GeometryStatus status = helengine::dreamcast::processGeometry(
        oneTriangle(vertices), identityMatrix(), defaultConfig(), frame);

    EXPECT(status == GeometryStatus::Ok);
    EXPECT(frame.triangleCount == 0);
    EXPECT(frame.rejectedTriangleCount == 1);
    EXPECT(frame.emittedVertexCount == 0);
}

void OnPlaneVertexDoesNotDuplicate() {
    constexpr const char* testName = "OnPlaneVertexDoesNotDuplicate";
    const SourceVertex vertices[] = {
        {{-0.5f, -0.5f, 0.5f}, kWhite},
        {{0.5f, -0.5f, 2.0f}, kWhite},
        {{0.0f, 0.5f, 2.0f}, kWhite},
    };
    GeometryFrame frame{};
    const GeometryStatus status = helengine::dreamcast::processGeometry(
        oneTriangle(vertices), identityMatrix(), defaultConfig(), frame);

    EXPECT(status == GeometryStatus::Ok);
    EXPECT(frame.triangleCount == 1);
    EXPECT(frame.emittedVertexCount == 3);
    EXPECT(finiteTriangle(frame.triangles[0]));
}

void DegenerateProjectedTriangleIsRejected() {
    constexpr const char* testName = "DegenerateProjectedTriangleIsRejected";
    const SourceVertex vertices[] = {
        {{0.0f, 0.0f, 2.0f}, kWhite},
        {{1.0f, 0.0f, 2.0f}, kWhite},
        {{2.0f, 0.0f, 2.0f}, kWhite},
    };
    GeometryFrame frame{};
    const GeometryStatus status = helengine::dreamcast::processGeometry(
        oneTriangle(vertices), identityMatrix(), defaultConfig(), frame);

    EXPECT(status == GeometryStatus::Ok);
    EXPECT(frame.triangleCount == 0);
    EXPECT(frame.rejectedTriangleCount == 1);
}

void NonFiniteInputIsReported() {
    constexpr const char* testName = "NonFiniteInputIsReported";
    const SourceVertex vertices[] = {
        {{-0.5f, -0.5f, 2.0f}, kWhite},
        {{0.5f, -0.5f, 2.0f}, kWhite},
        {{0.0f, 0.5f, 2.0f}, {NAN, 1.0f, 1.0f, 1.0f}},
    };
    GeometryFrame frame{};
    const GeometryStatus status = helengine::dreamcast::processGeometry(
        oneTriangle(vertices), identityMatrix(), defaultConfig(), frame);

    EXPECT(status == GeometryStatus::NonFiniteInput);
    EXPECT(frame.triangleCount == 0);
    EXPECT(frame.rejectedTriangleCount == 0);
    EXPECT(frame.emittedVertexCount == 0);
}

void OffscreenProjectionRemainsFinite() {
    constexpr const char* testName = "OffscreenProjectionRemainsFinite";
    const SourceVertex vertices[] = {
        {{-1000.0f, -1000.0f, 2.0f}, kWhite},
        {{1000.0f, -1000.0f, 2.0f}, kWhite},
        {{0.0f, 1000.0f, 2.0f}, kWhite},
    };
    GeometryFrame frame{};
    const GeometryStatus status = helengine::dreamcast::processGeometry(
        oneTriangle(vertices), identityMatrix(), defaultConfig(), frame);

    EXPECT(status == GeometryStatus::Ok);
    EXPECT(frame.triangleCount == 1);
    EXPECT(finiteTriangle(frame.triangles[0]));
    EXPECT(frame.triangles[0].vertices[0].x < 0.0f);
    EXPECT(frame.triangles[0].vertices[1].x > 640.0f);
}

void InputTriangleCapacityIsRejected() {
    constexpr const char* testName = "InputTriangleCapacityIsRejected";
    const SourceVertex vertices[] = {
        {{-0.5f, -0.5f, 2.0f}, kWhite},
        {{0.5f, -0.5f, 2.0f}, kWhite},
        {{0.0f, 0.5f, 2.0f}, kWhite},
    };
    TriangleIndex triangles[129]{};
    for (auto& triangle : triangles) {
        triangle = TriangleIndex{0, 1, 2};
    }
    const MeshView mesh{vertices, 3, triangles, 129};
    GeometryFrame frame{};
    frame.triangleCount = 99;
    frame.overflow = false;
    const GeometryStatus status = helengine::dreamcast::processGeometry(
        mesh, identityMatrix(), defaultConfig(), frame);

    EXPECT(status == GeometryStatus::InvalidMesh);
    EXPECT(frame.triangleCount == 0);
    EXPECT(frame.rejectedTriangleCount == 0);
    EXPECT(frame.clippedTriangleCount == 0);
    EXPECT(frame.emittedVertexCount == 0);
    EXPECT(!frame.overflow);
}

void CubeMeshReportsExpectedCountsAndValidIndices() {
    constexpr const char* testName = "CubeMeshReportsExpectedCountsAndValidIndices";
    const MeshView mesh = helengine::dreamcast::cubeMesh();
    EXPECT(mesh.vertexCount == 24);
    EXPECT(mesh.triangleCount == 12);
    EXPECT(mesh.vertices != nullptr);
    EXPECT(mesh.triangles != nullptr);
    for (std::uint16_t i = 0; i < mesh.triangleCount; ++i) {
        EXPECT(mesh.triangles[i].a < mesh.vertexCount);
        EXPECT(mesh.triangles[i].b < mesh.vertexCount);
        EXPECT(mesh.triangles[i].c < mesh.vertexCount);
    }
}

void NullMeshPointerIsReported() {
    constexpr const char* testName = "NullMeshPointerIsReported";
    const MeshView mesh{nullptr, 3, nullptr, 1};
    GeometryFrame frame{};
    EXPECT(helengine::dreamcast::processGeometry(
                mesh, identityMatrix(), defaultConfig(), frame) ==
            GeometryStatus::InvalidMesh);
    EXPECT(frame.triangleCount == 0);
}

void NullVertexPointerIsReported() {
    constexpr const char* testName = "NullVertexPointerIsReported";
    const TriangleIndex triangle{0, 0, 0};
    const MeshView mesh{nullptr, 3, &triangle, 1};
    GeometryFrame frame{};
    EXPECT(helengine::dreamcast::processGeometry(
                mesh, identityMatrix(), defaultConfig(), frame) ==
            GeometryStatus::InvalidMesh);
    EXPECT(frame.triangleCount == 0);
}

void NullTrianglePointerIsReported() {
    constexpr const char* testName = "NullTrianglePointerIsReported";
    const SourceVertex vertex{{0.0f, 0.0f, 2.0f}, kWhite};
    const MeshView mesh{&vertex, 1, nullptr, 1};
    GeometryFrame frame{};
    EXPECT(helengine::dreamcast::processGeometry(
                mesh, identityMatrix(), defaultConfig(), frame) ==
            GeometryStatus::InvalidMesh);
    EXPECT(frame.triangleCount == 0);
}

void OutOfRangeIndexIsReported() {
    constexpr const char* testName = "OutOfRangeIndexIsReported";
    const SourceVertex vertices[] = {
        {{-0.5f, -0.5f, 2.0f}, kWhite},
        {{0.5f, -0.5f, 2.0f}, kWhite},
        {{0.0f, 0.5f, 2.0f}, kWhite},
    };
    const TriangleIndex triangle{0, 1, 3};
    const MeshView mesh{vertices, 3, &triangle, 1};
    GeometryFrame frame{};
    EXPECT(helengine::dreamcast::processGeometry(
                mesh, identityMatrix(), defaultConfig(), frame) ==
            GeometryStatus::InvalidMesh);
    EXPECT(frame.triangleCount == 0);
}

void NearPlaneTopologyHoldsForVertexPermutations() {
    constexpr const char* testName = "NearPlaneTopologyHoldsForVertexPermutations";
    const Vec3 positions[] = {
        {-0.5f, -0.5f, 0.0f},
        {0.5f, -0.5f, 0.0f},
        {0.0f, 0.5f, 0.0f},
    };
    const std::uint8_t permutations[][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2},
        {1, 2, 0}, {2, 0, 1}, {2, 1, 0},
    };
    for (const auto& permutation : permutations) {
        SourceVertex oneOutside[3] = {
            {positions[0], kWhite}, {positions[1], kWhite}, {positions[2], kWhite},
        };
        SourceVertex twoOutside[3] = {
            {positions[0], kWhite}, {positions[1], kWhite}, {positions[2], kWhite},
        };
        for (std::uint8_t i = 0; i < 3; ++i) {
            oneOutside[i].position.z = permutation[i] == 0 ? 0.25f : 2.0f;
            twoOutside[i].position.z = permutation[i] == 2 ? 2.0f : 0.25f;
        }
        GeometryFrame oneFrame{};
        GeometryFrame twoFrame{};
        EXPECT(helengine::dreamcast::processGeometry(
                    oneTriangle(oneOutside), identityMatrix(), defaultConfig(), oneFrame) ==
                GeometryStatus::Ok);
        EXPECT(helengine::dreamcast::processGeometry(
                    oneTriangle(twoOutside), identityMatrix(), defaultConfig(), twoFrame) ==
                GeometryStatus::Ok);
        EXPECT(oneFrame.triangleCount == 2);
        EXPECT(twoFrame.triangleCount == 1);
        for (std::uint16_t i = 0; i < oneFrame.triangleCount; ++i) {
            EXPECT(finiteTriangle(oneFrame.triangles[i]));
        }
        EXPECT(finiteTriangle(twoFrame.triangles[0]));
    }
}

void ClippingPreservesWindingForVertexPermutations() {
    constexpr const char* testName = "ClippingPreservesWindingForVertexPermutations";
    const Vec3 positions[] = {
        {-0.5f, -0.5f, 0.0f},
        {0.5f, -0.5f, 0.0f},
        {0.0f, 0.5f, 0.0f},
    };
    const std::uint8_t permutations[][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2},
        {1, 2, 0}, {2, 0, 1}, {2, 1, 0},
    };
    float referenceSign = 0.0f;
    for (const auto& permutation : permutations) {
        SourceVertex vertices[3] = {
            {positions[0], kWhite}, {positions[1], kWhite}, {positions[2], kWhite},
        };
        for (std::uint8_t i = 0; i < 3; ++i) {
            vertices[i].position.z = permutation[i] == 0 ? 0.25f : 2.0f;
        }
        GeometryFrame frame{};
        EXPECT(helengine::dreamcast::processGeometry(
                    oneTriangle(vertices), identityMatrix(), defaultConfig(), frame) ==
                GeometryStatus::Ok);
        EXPECT(frame.triangleCount == 2);
        for (std::uint16_t i = 0; i < frame.triangleCount; ++i) {
            const auto& triangle = frame.triangles[i];
            const float area =
                (triangle.vertices[1].x - triangle.vertices[0].x) *
                    (triangle.vertices[2].y - triangle.vertices[0].y) -
                (triangle.vertices[1].y - triangle.vertices[0].y) *
                    (triangle.vertices[2].x - triangle.vertices[0].x);
            EXPECT(std::fabs(area) > 1.0e-5f);
            if (referenceSign == 0.0f) {
                referenceSign = area > 0.0f ? 1.0f : -1.0f;
            }
            EXPECT((area > 0.0f ? 1.0f : -1.0f) == referenceSign);
        }
    }
}

void CubeIndicesHaveOutwardWinding() {
    constexpr const char* testName = "CubeIndicesHaveOutwardWinding";
    const MeshView mesh = helengine::dreamcast::cubeMesh();
    const Vec3 expectedNormals[] = {
        {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, -1.0f},
        {1.0f, 0.0f, 0.0f},  {1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},  {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f}, {0.0f, -1.0f, 0.0f},
    };
    for (std::uint16_t i = 0; i < mesh.triangleCount; ++i) {
        const TriangleIndex index = mesh.triangles[i];
        const Vec3 a = mesh.vertices[index.a].position;
        const Vec3 b = mesh.vertices[index.b].position;
        const Vec3 c = mesh.vertices[index.c].position;
        const Vec3 ab{b.x - a.x, b.y - a.y, b.z - a.z};
        const Vec3 ac{c.x - a.x, c.y - a.y, c.z - a.z};
        const Vec3 normal{
            ab.y * ac.z - ab.z * ac.y,
            ab.z * ac.x - ab.x * ac.z,
            ab.x * ac.y - ab.y * ac.x,
        };
        const Vec3 expected = expectedNormals[i];
        EXPECT(normal.x * expected.x + normal.y * expected.y + normal.z * expected.z > 0.0f);
    }
}

void InvalidConfigIsReported() {
    constexpr const char* testName = "InvalidConfigIsReported";
    const SourceVertex vertices[] = {
        {{-0.5f, -0.5f, 2.0f}, kWhite},
        {{0.5f, -0.5f, 2.0f}, kWhite},
        {{0.0f, 0.5f, 2.0f}, kWhite},
    };
    GeometryConfig config = defaultConfig();
    config.nearPlane = 0.0f;
    GeometryFrame frame{};
    EXPECT(helengine::dreamcast::processGeometry(
                oneTriangle(vertices), identityMatrix(), config, frame) ==
            GeometryStatus::InvalidConfig);
    EXPECT(frame.triangleCount == 0);
}

void NonFiniteMatrixIsReported() {
    constexpr const char* testName = "NonFiniteMatrixIsReported";
    const SourceVertex vertices[] = {
        {{-0.5f, -0.5f, 2.0f}, kWhite},
        {{0.5f, -0.5f, 2.0f}, kWhite},
        {{0.0f, 0.5f, 2.0f}, kWhite},
    };
    Matrix4x4 matrix = identityMatrix();
    matrix.m[0][0] = INFINITY;
    GeometryFrame frame{};
    EXPECT(helengine::dreamcast::processGeometry(
                oneTriangle(vertices), matrix, defaultConfig(), frame) ==
            GeometryStatus::NonFiniteInput);
    EXPECT(frame.triangleCount == 0);
}

} // namespace

int main() {
    CenteredTriangleProjectsToViewportCenter();
    FartherDepthProducesSmallerQ();
    OneOutsideNearPlaneEmitsTwoTriangles();
    TwoOutsideNearPlaneEmitsOneTriangle();
    FullyBehindNearPlaneIsRejected();
    OnPlaneVertexDoesNotDuplicate();
    DegenerateProjectedTriangleIsRejected();
    NonFiniteInputIsReported();
    OffscreenProjectionRemainsFinite();
    InputTriangleCapacityIsRejected();
    CubeMeshReportsExpectedCountsAndValidIndices();
    NullMeshPointerIsReported();
    NullVertexPointerIsReported();
    NullTrianglePointerIsReported();
    OutOfRangeIndexIsReported();
    NearPlaneTopologyHoldsForVertexPermutations();
    ClippingPreservesWindingForVertexPermutations();
    CubeIndicesHaveOutwardWinding();
    InvalidConfigIsReported();
    NonFiniteMatrixIsReported();

    if (g_failures != 0) {
        std::cerr << g_failures << " assertion(s) failed\n";
        return 1;
    }
    std::cout << "DreamcastGeometryPipelineTests: all tests passed\n";
    return 0;
}
