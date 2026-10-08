#pragma once

#include "DreamcastSpriteFrame.hpp"

#include <cstdint>

namespace helengine::dreamcast {

constexpr std::uint8_t kRoundedRectSegmentsPerCorner = 6;
constexpr std::uint8_t kMaxRoundedRectBoundaryPoints = 4 * (kRoundedRectSegmentsPerCorner + 1);
constexpr std::uint16_t kMaxRoundedRectTriangles =
    (kMaxRoundedRectBoundaryPoints - 2) + 2 * kMaxRoundedRectBoundaryPoints;

struct RoundedRectPoint {
    float x;
    float y;
};

struct RoundedRectVertex {
    float x;
    float y;
    ColorRgba color;
};

struct RoundedRectTriangle {
    RoundedRectVertex vertices[3];
};

struct RoundedRectGeometry {
    RoundedRectTriangle triangles[kMaxRoundedRectTriangles]{};
    RoundedRectPoint outerBoundary[kMaxRoundedRectBoundaryPoints]{};
    RoundedRectPoint innerBoundary[kMaxRoundedRectBoundaryPoints]{};
    std::uint16_t triangleCount = 0;
    std::uint16_t fillTriangleCount = 0;
    std::uint16_t borderTriangleCount = 0;
    std::uint8_t outerPointCount = 0;
    std::uint8_t innerPointCount = 0;
};

enum class RoundedRectGeometryStatus : std::uint8_t { Ok, InvalidInput, CapacityExceeded };

/// Creates a fixed-capacity triangle mesh in screen coordinates. Radius is
/// clamped to half the smaller extent; selected corners alone are rounded.
RoundedRectGeometryStatus tessellateRoundedRect(const AuthoredRoundedRectState& rect,
                                                RoundedRectGeometry& output) noexcept;

} // namespace helengine::dreamcast
