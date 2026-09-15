#include "DreamcastGeometryPipeline.hpp"

#include <cmath>

namespace helengine::dreamcast {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kProjectedAreaEpsilon = 1.0e-5f;
constexpr float kVertexEqualityEpsilon = 1.0e-6f;
constexpr std::uint16_t kMaxInputVertices = 64;
constexpr std::uint16_t kMaxInputTriangles = 128;
constexpr std::uint8_t kClipVertexCapacity = 5;

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

bool appendUnique(ClipVertex* polygon,
                  std::uint8_t& count,
                  const ClipVertex& vertex) noexcept {
    if (count != 0 && sameVertex(polygon[count - 1], vertex)) {
        return true;
    }
    if (count >= kClipVertexCapacity) {
        return false;
    }
    polygon[count++] = vertex;
    return true;
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
           finite(config.farPlane) &&
           config.viewportWidth > 0.0f && config.viewportHeight > 0.0f &&
           config.verticalFovRadians > 0.0f && config.verticalFovRadians < kPi &&
           config.nearPlane > 0.0f && config.farPlane > config.nearPlane;
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

void resetGeometryFrame(GeometryFrame& frame) noexcept {
    frame.triangleCount = 0;
    frame.rejectedTriangleCount = 0;
    frame.clippedTriangleCount = 0;
    frame.emittedVertexCount = 0;
    frame.overflow = false;
}

GeometryStatus processGeometry(const MeshView& mesh,
                               const Matrix4x4& cameraFromModel,
                               const GeometryConfig& config,
                               GeometryFrame& output) noexcept {
    resetGeometryFrame(output);

    if (!validConfig(config)) {
        return GeometryStatus::InvalidConfig;
    }
    if (!finite(cameraFromModel)) {
        return GeometryStatus::NonFiniteInput;
    }
    if (mesh.vertexCount == 0 || mesh.vertexCount > kMaxInputVertices ||
        mesh.triangleCount == 0 || mesh.triangleCount > kMaxInputTriangles ||
        mesh.vertices == nullptr || mesh.triangles == nullptr) {
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
        bool clipped = false;
        for (std::uint8_t i = 0; i < 3; ++i) {
            input[i] = ClipVertex{transformPoint(source[i]->position, cameraFromModel),
                                  source[i]->color};
            if (!finite(input[i].position)) {
                return GeometryStatus::NonFiniteInput;
            }
            if (input[i].position.z < config.nearPlane ||
                input[i].position.z > config.farPlane) {
                clipped = true;
            }
        }

        ClipVertex polygonA[kClipVertexCapacity]{};
        ClipVertex polygonB[kClipVertexCapacity]{};
        std::uint8_t polygonACount = 3;
        for (std::uint8_t i = 0; i < 3; ++i) {
            polygonA[i] = input[i];
        }

        const auto clipAgainstPlane = [](const ClipVertex* sourceVertices,
                                         std::uint8_t sourceCount,
                                         float plane,
                                         bool keepGreater,
                                         ClipVertex* destinationVertices,
                                         std::uint8_t& destinationCount) noexcept {
            destinationCount = 0;
            if (sourceCount == 0) {
                return true;
            }
            ClipVertex previous = sourceVertices[sourceCount - 1];
            const auto distance = [plane, keepGreater](const ClipVertex& vertex) {
                return keepGreater ? vertex.position.z - plane : plane - vertex.position.z;
            };
            float previousDistance = distance(previous);
            bool previousInside = previousDistance >= 0.0f;
            for (std::uint8_t i = 0; i < sourceCount; ++i) {
                const ClipVertex current = sourceVertices[i];
                const float currentDistance = distance(current);
                const bool currentInside = currentDistance >= 0.0f;
                if (currentInside != previousInside) {
                    const float denominator = previousDistance - currentDistance;
                    if (!finite(denominator) || denominator == 0.0f) {
                        return false;
                    }
                    float t = previousDistance / denominator;
                    if (!finite(t)) {
                        return false;
                    }
                    if (t < 0.0f) {
                        t = 0.0f;
                    } else if (t > 1.0f) {
                        t = 1.0f;
                    }
                    if (!appendUnique(destinationVertices, destinationCount,
                                      interpolate(previous, current, t))) {
                        return false;
                    }
                }
                if (currentInside &&
                    !appendUnique(destinationVertices, destinationCount, current)) {
                    return false;
                }
                previous = current;
                previousDistance = currentDistance;
                previousInside = currentInside;
            }
            if (destinationCount > 1 &&
                sameVertex(destinationVertices[0], destinationVertices[destinationCount - 1])) {
                --destinationCount;
            }
            return true;
        };

        std::uint8_t polygonBCount = 0;
        if (!clipAgainstPlane(polygonA, polygonACount, config.nearPlane, true, polygonB,
                              polygonBCount)) {
            resetGeometryFrame(output);
            output.overflow = true;
            return GeometryStatus::OutputCapacityExceeded;
        }
        if (polygonBCount != polygonACount) {
            clipped = true;
        }
        std::uint8_t polygonCount = 0;
        if (!clipAgainstPlane(polygonB, polygonBCount, config.farPlane, false, polygonA,
                              polygonCount)) {
            resetGeometryFrame(output);
            output.overflow = true;
            return GeometryStatus::OutputCapacityExceeded;
        }
        if (polygonCount != polygonBCount) {
            clipped = true;
        }

        if (clipped && polygonCount != 0) {
            ++output.clippedTriangleCount;
        }

        ProjectedTriangle candidates[3]{};
        std::uint8_t candidateCount = 0;
        if (polygonCount >= 3) {
            ProjectedVertex projected[kClipVertexCapacity]{};
            for (std::uint8_t i = 0; i < polygonCount; ++i) {
                projected[i] = projectVertex(polygonA[i], config, focalLength);
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
            ++output.rejectedTriangleCount;
            continue;
        }
        if (output.triangleCount + candidateCount > GeometryFrame::kMaxTriangles) {
            resetGeometryFrame(output);
            output.overflow = true;
            return GeometryStatus::OutputCapacityExceeded;
        }
        for (std::uint8_t i = 0; i < candidateCount; ++i) {
            output.triangles[output.triangleCount++] = candidates[i];
        }
    }

    output.emittedVertexCount = static_cast<std::uint16_t>(output.triangleCount * 3);
    return GeometryStatus::Ok;
}

} // namespace helengine::dreamcast
