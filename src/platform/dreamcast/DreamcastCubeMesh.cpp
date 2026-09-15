#include "DreamcastCubeMesh.hpp"

namespace helengine::dreamcast {
namespace {

constexpr ColorRgba kFrontColor{1.0f, 0.0f, 0.0f, 1.0f};
constexpr ColorRgba kBackColor{0.0f, 1.0f, 0.0f, 1.0f};
constexpr ColorRgba kRightColor{0.0f, 0.0f, 1.0f, 1.0f};
constexpr ColorRgba kLeftColor{1.0f, 1.0f, 0.0f, 1.0f};
constexpr ColorRgba kTopColor{0.0f, 1.0f, 1.0f, 1.0f};
constexpr ColorRgba kBottomColor{1.0f, 0.0f, 1.0f, 1.0f};

const SourceVertex kCubeVertices[24] = {
    // +Z (front)
    {{-1.0f, -1.0f, 1.0f}, kFrontColor},
    {{1.0f, -1.0f, 1.0f}, kFrontColor},
    {{1.0f, 1.0f, 1.0f}, kFrontColor},
    {{-1.0f, 1.0f, 1.0f}, kFrontColor},
    // -Z (back)
    {{-1.0f, -1.0f, -1.0f}, kBackColor},
    {{-1.0f, 1.0f, -1.0f}, kBackColor},
    {{1.0f, 1.0f, -1.0f}, kBackColor},
    {{1.0f, -1.0f, -1.0f}, kBackColor},
    // +X (right)
    {{1.0f, -1.0f, -1.0f}, kRightColor},
    {{1.0f, 1.0f, -1.0f}, kRightColor},
    {{1.0f, 1.0f, 1.0f}, kRightColor},
    {{1.0f, -1.0f, 1.0f}, kRightColor},
    // -X (left)
    {{-1.0f, -1.0f, -1.0f}, kLeftColor},
    {{-1.0f, -1.0f, 1.0f}, kLeftColor},
    {{-1.0f, 1.0f, 1.0f}, kLeftColor},
    {{-1.0f, 1.0f, -1.0f}, kLeftColor},
    // +Y (top)
    {{-1.0f, 1.0f, -1.0f}, kTopColor},
    {{-1.0f, 1.0f, 1.0f}, kTopColor},
    {{1.0f, 1.0f, 1.0f}, kTopColor},
    {{1.0f, 1.0f, -1.0f}, kTopColor},
    // -Y (bottom)
    {{-1.0f, -1.0f, -1.0f}, kBottomColor},
    {{1.0f, -1.0f, -1.0f}, kBottomColor},
    {{1.0f, -1.0f, 1.0f}, kBottomColor},
    {{-1.0f, -1.0f, 1.0f}, kBottomColor},
};

const TriangleIndex kCubeTriangles[12] = {
    {0, 1, 2}, {0, 2, 3},
    {4, 5, 6}, {4, 6, 7},
    {8, 9, 10}, {8, 10, 11},
    {12, 13, 14}, {12, 14, 15},
    {16, 17, 18}, {16, 18, 19},
    {20, 21, 22}, {20, 22, 23},
};

} // namespace

MeshView cubeMesh() noexcept {
    return MeshView{kCubeVertices, 24, kCubeTriangles, 12};
}

} // namespace helengine::dreamcast
