#include "DreamcastGeometryPipeline.hpp"

#include <cmath>

namespace helengine::dreamcast {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kProjectedAreaEpsilon = 1.0e-5f;
constexpr float kVertexEqualityEpsilon = 1.0e-6f;

struct ClipVertex {
    Vec3 position;
    ColorRgba color;
};

bool finite(float value) noexcept {
    return std::isfinite(value);
}

bool finite(const Vec3& value) noexcept {
    return finite(value.x) && finite(value.y) && finite(value.z);
}

bool finite(const ColorRgba& value) noexcept {
    return finite(value.r) && finite(value.g) && finite(value.b) && finite(value.a);
}

bool finite(const Matrix4x4& value) noexcept {
    for (const auto& row : value.m) {
        for (float component : row) {
            if (!finite(component)) {
                return false;
            }
        }
    }
    return true;
}

bool approximatelyEqual(float a, float b) noexcept {
    return std::fabs(a - b) <= kVertexEqualityEpsilon;
}

bool sameVertex(const ClipVertex& a, const ClipVertex& b) noexcept {
    return approximatelyEqual(a.position.x, b.position.x) &&
           approximatelyEqual(a.position.y, b.position.y) &&
           approximatelyEqual(a.position.z, b.position.z) &&
           approximatelyEqual(a.color.r, b.color.r) &&
           approximatelyEqual(a.color.g, b.color.g) &&
           approximatelyEqual(a.color.b, b.color.b) &&
           approximatelyEqual(a.color.a, b.color.a);
}

void appendUnique(ClipVertex* polygon, std::uint8_t& count, const ClipVertex& vertex) noexcept {
    if (count != 0 && sameVertex(polygon[count - 1], vertex)) {
        return;
    }
    if (count < 4) {
        polygon[count++] = vertex;
    }
}

ClipVertex interpolate(const ClipVertex& a, const ClipVertex& b, float t) noexcept {
    return ClipVertex{
        {
            a.position.x + (b.position.x - a.position.x) * t,
            a.position.y + (b.position.y - a.position.y) * t,
            a.position.z + (b.position.z - a.position.z) * t,
        },
        {
            a.color.r + (b.color.r - a.color.r) * t,
            a.color.g + (b.color.g - a.color.g) * t,
            a.color.b + (b.color.b - a.color.b) * t,
            a.color.a + (b.color.a - a.color.a) * t,
        },
    };
}

bool validConfig(const GeometryConfig& config) noexcept {
    return finite(config.viewportWidth) && finite(config.viewportHeight) &&
           finite(config.verticalFovRadians) && finite(config.nearPlane) &&
           config.viewportWidth > 0.0f && config.viewportHeight > 0.0f &&
           config.verticalFovRadians > 0.0f && config.verticalFovRadians < kPi &&
           config.nearPlane > 0.0f;
}

Vec3 transformPoint(const Vec3& point, const Matrix4x4& matrix) noexcept {
    return Vec3{
        matrix.m[0][0] * point.x + matrix.m[0][1] * point.y +
            matrix.m[0][2] * point.z + matrix.m[0][3],
        matrix.m[1][0] * point.x + matrix.m[1][1] * point.y +
            matrix.m[1][2] * point.z + matrix.m[1][3],
        matrix.m[2][0] * point.x + matrix.m[2][1] * point.y +
            matrix.m[2][2] * point.z + matrix.m[2][3],
    };
}

ProjectedVertex projectVertex(const ClipVertex& vertex,
                              const GeometryConfig& config,
                              float focalLength) noexcept {
    const float reciprocalDepth = 1.0f / vertex.position.z;
    return ProjectedVertex{
        config.viewportWidth * 0.5f + focalLength * vertex.position.x * reciprocalDepth,
        config.viewportHeight * 0.5f - focalLength * vertex.position.y * reciprocalDepth,
        reciprocalDepth,
        vertex.color,
    };
}

float projectedArea(const ProjectedTriangle& triangle) noexcept {
    const ProjectedVertex& a = triangle.vertices[0];
    const ProjectedVertex& b = triangle.vertices[1];
    const ProjectedVertex& c = triangle.vertices[2];
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool finite(const ProjectedTriangle& triangle) noexcept {
    for (const ProjectedVertex& vertex : triangle.vertices) {
        if (!finite(vertex.x) || !finite(vertex.y) || !finite(vertex.z) ||
            vertex.z <= 0.0f || !finite(vertex.color)) {
            return false;
        }
    }
    return true;
}

} // namespace

GeometryStatus processGeometry(const CubeMeshView& mesh,
                               const Matrix4x4& cameraFromModel,
                               const GeometryConfig& config,
                               GeometryFrame& output) noexcept {
    output = GeometryFrame{};

    if (!validConfig(config)) {
        return GeometryStatus::InvalidConfig;
    }
    if (!finite(cameraFromModel)) {
        return GeometryStatus::NonFiniteInput;
    }
    if ((mesh.vertexCount != 0 && mesh.vertices == nullptr) ||
        (mesh.triangleCount != 0 && mesh.triangles == nullptr)) {
        return GeometryStatus::InvalidMesh;
    }
    for (std::uint16_t i = 0; i < mesh.vertexCount; ++i) {
        if (!finite(mesh.vertices[i].position) || !finite(mesh.vertices[i].color)) {
            return GeometryStatus::NonFiniteInput;
        }
    }

    const float tangent = std::tan(config.verticalFovRadians * 0.5f);
    const float focalLength = config.viewportHeight / (2.0f * tangent);
    if (!finite(tangent) || tangent <= 0.0f || !finite(focalLength) || focalLength <= 0.0f) {
        return GeometryStatus::InvalidConfig;
    }

    GeometryFrame staged{};
    for (std::uint16_t triangleIndex = 0; triangleIndex < mesh.triangleCount;
         ++triangleIndex) {
        const TriangleIndex& index = mesh.triangles[triangleIndex];
        if (index.a >= mesh.vertexCount || index.b >= mesh.vertexCount ||
            index.c >= mesh.vertexCount) {
            return GeometryStatus::InvalidMesh;
        }

        const SourceVertex* source[3] = {
            &mesh.vertices[index.a],
            &mesh.vertices[index.b],
            &mesh.vertices[index.c],
        };
        ClipVertex input[3]{};
        std::uint8_t outsideCount = 0;
        for (std::uint8_t i = 0; i < 3; ++i) {
            input[i] = ClipVertex{transformPoint(source[i]->position, cameraFromModel),
                                  source[i]->color};
            if (!finite(input[i].position)) {
                return GeometryStatus::NonFiniteInput;
            }
            if (input[i].position.z < config.nearPlane) {
                ++outsideCount;
            }
        }

        ClipVertex polygon[4]{};
        std::uint8_t polygonCount = 0;
        ClipVertex previous = input[2];
        bool previousInside = previous.position.z >= config.nearPlane;
        for (const ClipVertex& current : input) {
            const bool currentInside = current.position.z >= config.nearPlane;
            if (currentInside != previousInside) {
                const float a = previous.position.z - config.nearPlane;
                const float b = current.position.z - config.nearPlane;
                const float denominator = a - b;
                if (denominator != 0.0f) {
                    float t = a / denominator;
                    if (t < 0.0f) {
                        t = 0.0f;
                    } else if (t > 1.0f) {
                        t = 1.0f;
                    }
                    appendUnique(polygon, polygonCount, interpolate(previous, current, t));
                }
            }
            if (currentInside) {
                appendUnique(polygon, polygonCount, current);
            }
            previous = current;
            previousInside = currentInside;
        }
        if (polygonCount > 1 && sameVertex(polygon[0], polygon[polygonCount - 1])) {
            --polygonCount;
        }

        if (outsideCount != 0 && polygonCount != 0) {
            ++staged.clippedTriangleCount;
        }

        ProjectedTriangle candidates[2]{};
        std::uint8_t candidateCount = 0;
        if (polygonCount >= 3) {
            ProjectedVertex projected[4]{};
            for (std::uint8_t i = 0; i < polygonCount; ++i) {
                projected[i] = projectVertex(polygon[i], config, focalLength);
            }
            for (std::uint8_t i = 1; i + 1 < polygonCount; ++i) {
                candidates[candidateCount] =
                    ProjectedTriangle{{projected[0], projected[i], projected[i + 1]}};
                if (finite(candidates[candidateCount]) &&
                    std::fabs(projectedArea(candidates[candidateCount])) >
                        kProjectedAreaEpsilon) {
                    ++candidateCount;
                }
            }
        }

        if (candidateCount == 0) {
            ++staged.rejectedTriangleCount;
            continue;
        }
        if (staged.triangleCount + candidateCount > GeometryFrame::kMaxTriangles) {
            output = GeometryFrame{};
            output.overflow = true;
            return GeometryStatus::OutputCapacityExceeded;
        }
        for (std::uint8_t i = 0; i < candidateCount; ++i) {
            staged.triangles[staged.triangleCount++] = candidates[i];
        }
    }

    staged.emittedVertexCount = static_cast<std::uint16_t>(staged.triangleCount * 3);
    output = staged;
    return GeometryStatus::Ok;
}

} // namespace helengine::dreamcast
