#include "platform/dreamcast/DreamcastGeometryPipeline.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using helengine::dreamcast::ColorRgba;
using helengine::dreamcast::GeometryConfig;
using helengine::dreamcast::GeometryFrame;
using helengine::dreamcast::GeometryStatus;
using helengine::dreamcast::Matrix4x4;
using helengine::dreamcast::MeshView;
using helengine::dreamcast::SourceVertex;
using helengine::dreamcast::TriangleIndex;

int failures = 0;

void expect(bool condition, const char* expression) {
    if (!condition) {
        std::cerr << "expected " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(value) expect((value), #value)

constexpr ColorRgba white{1.0f, 1.0f, 1.0f, 1.0f};
constexpr Matrix4x4 identity{{
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
}};

GeometryConfig config() {
    return GeometryConfig{640.0f, 480.0f, 1.10f, 0.5f, 10.0f};
}

void AllBeyondFarPlaneIsRejected() {
    const SourceVertex vertices[] = {
        {{-0.5f, -0.5f, 11.0f}, white},
        {{0.5f, -0.5f, 11.0f}, white},
        {{0.0f, 0.5f, 11.0f}, white},
    };
    const TriangleIndex indices{0, 1, 2};
    GeometryFrame frame{};
    EXPECT(helengine::dreamcast::processGeometry(
                MeshView{vertices, 3, &indices, 1}, identity, config(), frame) ==
            GeometryStatus::Ok);
    EXPECT(frame.triangleCount == 0);
    EXPECT(frame.rejectedTriangleCount == 1);
    EXPECT(frame.clippedTriangleCount == 0);
}

void FarPlaneCrossingProducesFiniteTriangles() {
    const SourceVertex vertices[] = {
        {{-0.5f, -0.5f, 9.0f}, white},
        {{0.5f, -0.5f, 11.0f}, white},
        {{0.0f, 0.5f, 9.0f}, white},
    };
    const TriangleIndex indices{0, 1, 2};
    GeometryFrame frame{};
    EXPECT(helengine::dreamcast::processGeometry(
                MeshView{vertices, 3, &indices, 1}, identity, config(), frame) ==
            GeometryStatus::Ok);
    EXPECT(frame.triangleCount == 2);
    EXPECT(frame.clippedTriangleCount == 1);
    for (std::uint16_t triangle = 0; triangle < frame.triangleCount; ++triangle) {
        for (const auto& vertex : frame.triangles[triangle].vertices) {
            EXPECT(std::isfinite(vertex.x));
            EXPECT(std::isfinite(vertex.y));
            EXPECT(vertex.z > 0.0f);
        }
    }
}

void NearAndFarSlabCrossingIsClippedSequentially() {
    const SourceVertex vertices[] = {
        {{-0.5f, -0.5f, 0.1f}, white},
        {{0.5f, -0.5f, 5.0f}, white},
        {{0.0f, 0.5f, 20.0f}, white},
    };
    const TriangleIndex indices{0, 1, 2};
    GeometryFrame frame{};
    EXPECT(helengine::dreamcast::processGeometry(
                MeshView{vertices, 3, &indices, 1}, identity, config(), frame) ==
            GeometryStatus::Ok);
    EXPECT(frame.triangleCount == 3);
    EXPECT(frame.clippedTriangleCount == 1);
    for (std::uint16_t triangle = 0; triangle < frame.triangleCount; ++triangle) {
        for (const auto& vertex : frame.triangles[triangle].vertices) {
            EXPECT(vertex.z >= 1.0f / 10.0f);
            EXPECT(vertex.z <= 1.0f / 0.5f);
        }
    }
}

void LegalInputLimitsAreAcceptedAndPositionLimitIsEnforced() {
    std::vector<SourceVertex> vertices = {
        SourceVertex{{-1.0f, -1.0f, 0.1f}, white},
        SourceVertex{{1.0f, -1.0f, 12.0f}, white},
        SourceVertex{{0.0f, 1.0f, 5.0f}, white},
    };
    std::vector<TriangleIndex> triangles(128, TriangleIndex{0, 1, 2});
    GeometryFrame frame{};
    EXPECT(helengine::dreamcast::processGeometry(
                MeshView{vertices.data(), static_cast<std::uint16_t>(vertices.size()),
                         triangles.data(), static_cast<std::uint16_t>(triangles.size())},
                identity, config(), frame) == GeometryStatus::Ok);
    EXPECT(frame.triangleCount == 384);
    EXPECT(frame.rejectedTriangleCount == 0);
    EXPECT(!frame.overflow);

    vertices.resize(65, SourceVertex{{0.0f, 0.0f, 3.0f}, white});
    EXPECT(helengine::dreamcast::processGeometry(
                MeshView{vertices.data(), static_cast<std::uint16_t>(vertices.size()),
                         triangles.data(), static_cast<std::uint16_t>(triangles.size())},
                identity, config(), frame) == GeometryStatus::InvalidMesh);
}

void InvalidTriangleLimitAndFarConfigAreRejected() {
    std::vector<SourceVertex> vertices(3, SourceVertex{{0.0f, 0.0f, 3.0f}, white});
    std::vector<TriangleIndex> triangles(129, TriangleIndex{0, 1, 2});
    GeometryFrame frame{};
    EXPECT(helengine::dreamcast::processGeometry(
                MeshView{vertices.data(), 3, triangles.data(), 129}, identity, config(), frame) ==
            GeometryStatus::InvalidMesh);
    GeometryConfig invalid = config();
    invalid.farPlane = invalid.nearPlane;
    EXPECT(helengine::dreamcast::processGeometry(
                MeshView{vertices.data(), 3, triangles.data(), 1}, identity, invalid, frame) ==
            GeometryStatus::InvalidConfig);
}

void InvalidIndexIsRejectedBeforeOutput() {
    const SourceVertex vertices[] = {
        {{-0.5f, -0.5f, 2.0f}, white},
        {{0.5f, -0.5f, 2.0f}, white},
        {{0.0f, 0.5f, 2.0f}, white},
    };
    const TriangleIndex indices{0, 1, 3};
    GeometryFrame frame{};
    frame.triangleCount = 12;
    EXPECT(helengine::dreamcast::processGeometry(
                MeshView{vertices, 3, &indices, 1}, identity, config(), frame) ==
            GeometryStatus::InvalidMesh);
    EXPECT(frame.triangleCount == 0);
}

} // namespace

int main() {
    AllBeyondFarPlaneIsRejected();
    FarPlaneCrossingProducesFiniteTriangles();
    NearAndFarSlabCrossingIsClippedSequentially();
    LegalInputLimitsAreAcceptedAndPositionLimitIsEnforced();
    InvalidTriangleLimitAndFarConfigAreRejected();
    InvalidIndexIsRejectedBeforeOutput();
    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return 1;
    }
    std::cout << "DreamcastAuthoredMeshTests: all tests passed\n";
    return 0;
}
