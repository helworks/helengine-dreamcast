#include "DreamcastRoundedRectGeometry.hpp"

#include <cmath>

namespace helengine::dreamcast {
namespace {

constexpr float kPi = 3.14159265358979323846f;
bool finite(float value) noexcept { return std::isfinite(value); }
bool finiteColor(const ColorRgba& c) noexcept {
    return finite(c.r) && finite(c.g) && finite(c.b) && finite(c.a);
}

void appendCorner(RoundedRectPoint* points, std::uint8_t& count, float left, float top,
                  float width, float height, float radius, int corner, bool rounded,
                  bool preserveArcPoints) noexcept {
    if (!rounded || (radius <= 0.0f && !preserveArcPoints)) {
        switch (corner) {
        case 0: points[count++] = {left + width, top}; break;           // top-right
        case 1: points[count++] = {left + width, top + height}; break; // bottom-right
        case 2: points[count++] = {left, top + height}; break;         // bottom-left
        default: points[count++] = {left, top}; break;                 // top-left
        }
        return;
    }
    float centerX = 0.0f;
    float centerY = 0.0f;
    float startDegrees = 0.0f;
    switch (corner) {
    case 0: centerX = left + width - radius; centerY = top + radius; startDegrees = 270.0f; break;
    case 1: centerX = left + width - radius; centerY = top + height - radius; startDegrees = 0.0f; break;
    case 2: centerX = left + radius; centerY = top + height - radius; startDegrees = 90.0f; break;
    default: centerX = left + radius; centerY = top + radius; startDegrees = 180.0f; break;
    }
    for (std::uint8_t step = 0; step <= kRoundedRectSegmentsPerCorner; ++step) {
        const float angle = (startDegrees + 90.0f * step / kRoundedRectSegmentsPerCorner) * kPi / 180.0f;
        points[count++] = {centerX + std::cos(angle) * radius,
                           centerY + std::sin(angle) * radius};
    }
}

std::uint8_t makeBoundary(RoundedRectPoint* points, float left, float top, float width,
                          float height, float radius, std::uint8_t roundedCorners,
                          bool preserveArcPoints = false) noexcept {
    std::uint8_t count = 0;
    constexpr std::uint8_t cornerBits[4] = {
        kRoundedRectTopRight, kRoundedRectBottomRight,
        kRoundedRectBottomLeft, kRoundedRectTopLeft};
    for (int corner = 0; corner < 4; ++corner) {
        appendCorner(points, count, left, top, width, height, radius, corner,
                     (roundedCorners & cornerBits[corner]) != 0, preserveArcPoints);
    }
    return count;
}

float area2(const RoundedRectPoint& a, const RoundedRectPoint& b,
            const RoundedRectPoint& c) noexcept {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool appendTriangle(RoundedRectGeometry& out, const RoundedRectPoint& a,
                    const RoundedRectPoint& b, const RoundedRectPoint& c,
                    const ColorRgba& color, bool border) noexcept {
    if (std::fabs(area2(a, b, c)) <= 1.0e-5f) return true;
    if (out.triangleCount >= kMaxRoundedRectTriangles) return false;
    RoundedRectTriangle& triangle = out.triangles[out.triangleCount++];
    triangle.vertices[0] = {a.x, a.y, color};
    triangle.vertices[1] = {b.x, b.y, color};
    triangle.vertices[2] = {c.x, c.y, color};
    if (border) ++out.borderTriangleCount;
    else ++out.fillTriangleCount;
    return true;
}

void reset(RoundedRectGeometry& output) noexcept {
    output.triangleCount = 0;
    output.fillTriangleCount = 0;
    output.borderTriangleCount = 0;
    output.outerPointCount = 0;
    output.innerPointCount = 0;
}

} // namespace

RoundedRectGeometryStatus tessellateRoundedRect(const AuthoredRoundedRectState& rect,
                                                RoundedRectGeometry& output) noexcept {
    reset(output);
    if (!finite(rect.destinationX) || !finite(rect.destinationY) ||
        !finite(rect.destinationWidth) || !finite(rect.destinationHeight) ||
        !finite(rect.radius) || !finite(rect.borderThickness) ||
        !finiteColor(rect.fillColor) || !finiteColor(rect.borderColor) ||
        rect.destinationWidth <= 0.0f || rect.destinationHeight <= 0.0f ||
        rect.radius < 0.0f || rect.borderThickness < 0.0f ||
        (rect.roundedCorners & ~kRoundedRectAllCorners) != 0) {
        return RoundedRectGeometryStatus::InvalidInput;
    }
    const float radius = rect.radius < 0.5f * (rect.destinationWidth < rect.destinationHeight
                                                   ? rect.destinationWidth : rect.destinationHeight)
                             ? rect.radius
                             : 0.5f * (rect.destinationWidth < rect.destinationHeight
                                           ? rect.destinationWidth : rect.destinationHeight);
    output.outerPointCount = makeBoundary(output.outerBoundary, rect.destinationX,
                                          rect.destinationY, rect.destinationWidth,
                                          rect.destinationHeight, radius, rect.roundedCorners);
    const bool hasBorder = rect.borderThickness > 0.0f;
    if (!hasBorder) {
        output.innerPointCount = output.outerPointCount;
        for (std::uint8_t i = 0; i < output.outerPointCount; ++i)
            output.innerBoundary[i] = output.outerBoundary[i];
    } else {
        const float inset = rect.borderThickness;
        const float innerWidth = rect.destinationWidth - 2.0f * inset > 0.0f
                                     ? rect.destinationWidth - 2.0f * inset : 0.0f;
        const float innerHeight = rect.destinationHeight - 2.0f * inset > 0.0f
                                      ? rect.destinationHeight - 2.0f * inset : 0.0f;
        const float innerRadius = radius > inset ? radius - inset : 0.0f;
        output.innerPointCount = makeBoundary(output.innerBoundary,
                                              rect.destinationX + inset,
                                              rect.destinationY + inset,
                                              innerWidth, innerHeight, innerRadius,
                                              rect.roundedCorners, true);
        if (output.innerPointCount != output.outerPointCount) {
            reset(output);
            return RoundedRectGeometryStatus::CapacityExceeded;
        }
    }

    const RoundedRectPoint* fillBoundary = hasBorder ? output.innerBoundary : output.outerBoundary;
    const std::uint8_t fillPointCount = hasBorder ? output.innerPointCount : output.outerPointCount;
    for (std::uint8_t i = 1; i + 1 < fillPointCount; ++i) {
        if (!appendTriangle(output, fillBoundary[0], fillBoundary[i], fillBoundary[i + 1],
                            rect.fillColor, false)) {
            reset(output);
            return RoundedRectGeometryStatus::CapacityExceeded;
        }
    }
    if (hasBorder) {
        for (std::uint8_t i = 0; i < output.outerPointCount; ++i) {
            const std::uint8_t next = static_cast<std::uint8_t>((i + 1) % output.outerPointCount);
            if (!appendTriangle(output, output.outerBoundary[i], output.outerBoundary[next],
                                output.innerBoundary[next], rect.borderColor, true) ||
                !appendTriangle(output, output.outerBoundary[i], output.innerBoundary[next],
                                output.innerBoundary[i], rect.borderColor, true)) {
                reset(output);
                return RoundedRectGeometryStatus::CapacityExceeded;
            }
        }
    }
    return RoundedRectGeometryStatus::Ok;
}

} // namespace helengine::dreamcast
