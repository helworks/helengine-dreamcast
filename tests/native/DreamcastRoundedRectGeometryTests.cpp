#include "platform/dreamcast/DreamcastRoundedRectGeometry.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {
using namespace helengine::dreamcast;
int failures = 0;
void expect(bool condition, const char* expression) {
    if (!condition) { std::cerr << "expected " << expression << '\n'; ++failures; }
}
#define EXPECT(x) expect((x), #x)

AuthoredRoundedRectState rect() {
    return {10.0f, 20.0f, 100.0f, 60.0f, 0.0f, 0.0f, kRoundedRectAllCorners,
            {1, 0, 0, 1}, {0, 0, 0, 1}};
}

void SquareEmitsTwoTrianglesAndAllFourCorners() {
    RoundedRectGeometry mesh{};
    EXPECT(tessellateRoundedRect(rect(), mesh) == RoundedRectGeometryStatus::Ok);
    EXPECT(mesh.triangleCount == 2);
    EXPECT(mesh.fillTriangleCount == 2);
    EXPECT(mesh.borderTriangleCount == 0);
    EXPECT(mesh.outerPointCount == 4);
    EXPECT(mesh.outerBoundary[0].x == 110.0f && mesh.outerBoundary[0].y == 20.0f);
    EXPECT(mesh.outerBoundary[1].x == 110.0f && mesh.outerBoundary[1].y == 80.0f);
    EXPECT(mesh.outerBoundary[2].x == 10.0f && mesh.outerBoundary[2].y == 80.0f);
    EXPECT(mesh.outerBoundary[3].x == 10.0f && mesh.outerBoundary[3].y == 20.0f);
}

void RoundedCornersAndBorderProduceBoundedNondegenerateTriangles() {
    auto input = rect();
    input.radius = 12.0f;
    input.borderThickness = 3.0f;
    input.roundedCorners = kRoundedRectTopLeft | kRoundedRectBottomRight;
    RoundedRectGeometry mesh{};
    EXPECT(tessellateRoundedRect(input, mesh) == RoundedRectGeometryStatus::Ok);
    EXPECT(mesh.outerPointCount <= kMaxRoundedRectBoundaryPoints);
    EXPECT(mesh.innerPointCount == mesh.outerPointCount);
    EXPECT(mesh.borderTriangleCount > 0);
    EXPECT(mesh.fillTriangleCount > 0);
    EXPECT(mesh.triangleCount <= kMaxRoundedRectTriangles);
    for (std::uint16_t i = 0; i < mesh.triangleCount; ++i) {
        const auto& t = mesh.triangles[i];
        const float area2 = (t.vertices[1].x - t.vertices[0].x) * (t.vertices[2].y - t.vertices[0].y) -
                            (t.vertices[1].y - t.vertices[0].y) * (t.vertices[2].x - t.vertices[0].x);
        EXPECT(std::isfinite(area2) && std::fabs(area2) > 1.0e-4f);
    }
}

void RadiusZeroSquaresEveryCornerAndLargeRadiusClamps() {
    auto input = rect();
    input.roundedCorners = kRoundedRectAllCorners;
    input.radius = 0.0f;
    RoundedRectGeometry square{};
    EXPECT(tessellateRoundedRect(input, square) == RoundedRectGeometryStatus::Ok);
    EXPECT(square.outerPointCount == 4);
    input.radius = 1000.0f;
    RoundedRectGeometry clamped{};
    EXPECT(tessellateRoundedRect(input, clamped) == RoundedRectGeometryStatus::Ok);
    EXPECT(clamped.outerBoundary[0].y == 20.0f);
    EXPECT(clamped.outerBoundary[0].x >= 79.9f && clamped.outerBoundary[0].x <= 80.1f);
}

void InvalidInputsRejectWithoutWritingOutput() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    AuthoredRoundedRectState cases[] = {rect(), rect(), rect(), rect(), rect(), rect()};
    cases[0].destinationX = nan;
    cases[1].destinationWidth = std::numeric_limits<float>::infinity();
    cases[2].destinationHeight = 0.0f;
    cases[3].radius = nan;
    cases[4].borderThickness = -1.0f;
    cases[5].roundedCorners = 0x80;
    for (const auto& input : cases) {
        RoundedRectGeometry mesh{};
        mesh.triangleCount = 99;
        EXPECT(tessellateRoundedRect(input, mesh) == RoundedRectGeometryStatus::InvalidInput);
        EXPECT(mesh.triangleCount == 0);
        EXPECT(mesh.outerPointCount == 0);
    }
}
}

int main() {
    SquareEmitsTwoTrianglesAndAllFourCorners();
    RoundedCornersAndBorderProduceBoundedNondegenerateTriangles();
    RadiusZeroSquaresEveryCornerAndLargeRadiusClamps();
    InvalidInputsRejectWithoutWritingOutput();
    if (failures) { std::cerr << failures << " assertion(s) failed\n"; return 1; }
    std::cout << "DreamcastRoundedRectGeometryTests: all tests passed\n";
    return 0;
}
