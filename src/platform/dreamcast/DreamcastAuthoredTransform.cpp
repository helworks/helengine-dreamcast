#include "DreamcastAuthoredTransform.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)
#include "float4x4.hpp"
#endif

#include <cmath>

namespace helengine::dreamcast {
namespace {

constexpr float kRigidLengthEpsilon = 1.0e-4f;

bool finite(float value) noexcept {
    return std::isfinite(value);
}

bool finite(const Matrix4x4& matrix) noexcept {
    for (const auto& row : matrix.m) {
        for (float value : row) {
            if (!finite(value)) {
                return false;
            }
        }
    }
    return true;
}

bool approximately(float left, float right, float epsilon = kRigidLengthEpsilon) noexcept {
    return std::fabs(left - right) <= epsilon;
}

float determinant3(const Matrix4x4& matrix) noexcept {
    return matrix.m[0][0] * (matrix.m[1][1] * matrix.m[2][2] -
                             matrix.m[1][2] * matrix.m[2][1]) -
           matrix.m[0][1] * (matrix.m[1][0] * matrix.m[2][2] -
                             matrix.m[1][2] * matrix.m[2][0]) +
           matrix.m[0][2] * (matrix.m[1][0] * matrix.m[2][1] -
                             matrix.m[1][1] * matrix.m[2][0]);
}

bool affine(const Matrix4x4& matrix) noexcept {
    return approximately(matrix.m[3][0], 0.0f) &&
           approximately(matrix.m[3][1], 0.0f) &&
           approximately(matrix.m[3][2], 0.0f) &&
           approximately(matrix.m[3][3], 1.0f);
}

float dotColumn(const Matrix4x4& matrix, int left, int right) noexcept {
    return matrix.m[0][left] * matrix.m[0][right] +
           matrix.m[1][left] * matrix.m[1][right] +
           matrix.m[2][left] * matrix.m[2][right];
}

Matrix4x4 inverseRigid(const Matrix4x4& matrix) noexcept {
    Matrix4x4 inverse = Matrix4x4{{
        {matrix.m[0][0], matrix.m[1][0], matrix.m[2][0], 0.0f},
        {matrix.m[0][1], matrix.m[1][1], matrix.m[2][1], 0.0f},
        {matrix.m[0][2], matrix.m[1][2], matrix.m[2][2], 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    }};
    inverse.m[0][3] = -(inverse.m[0][0] * matrix.m[0][3] +
                        inverse.m[0][1] * matrix.m[1][3] +
                        inverse.m[0][2] * matrix.m[2][3]);
    inverse.m[1][3] = -(inverse.m[1][0] * matrix.m[0][3] +
                        inverse.m[1][1] * matrix.m[1][3] +
                        inverse.m[1][2] * matrix.m[2][3]);
    inverse.m[2][3] = -(inverse.m[2][0] * matrix.m[0][3] +
                        inverse.m[2][1] * matrix.m[1][3] +
                        inverse.m[2][2] * matrix.m[2][3]);
    return inverse;
}

} // namespace

Matrix4x4 matrixFromGenerated(const GeneratedMatrixFields& generated) noexcept {
    return Matrix4x4{{
        {generated.M11, generated.M21, generated.M31, generated.M41},
        {generated.M12, generated.M22, generated.M32, generated.M42},
        {generated.M13, generated.M23, generated.M33, generated.M43},
        {generated.M14, generated.M24, generated.M34, generated.M44},
    }};
}

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)
Matrix4x4 matrixFromGenerated(const ::float4x4& generated) noexcept {
    return matrixFromGenerated(GeneratedMatrixFields{
        generated.M11, generated.M12, generated.M13, generated.M14,
        generated.M21, generated.M22, generated.M23, generated.M24,
        generated.M31, generated.M32, generated.M33, generated.M34,
        generated.M41, generated.M42, generated.M43, generated.M44});
}
#endif

Matrix4x4 multiply(const Matrix4x4& left, const Matrix4x4& right) noexcept {
    Matrix4x4 result{};
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            for (int index = 0; index < 4; ++index) {
                result.m[row][column] += left.m[row][index] * right.m[index][column];
            }
        }
    }
    return result;
}

Vec3 transformPoint(const Matrix4x4& matrix, const Vec3& point) noexcept {
    return Vec3{
        matrix.m[0][0] * point.x + matrix.m[0][1] * point.y +
            matrix.m[0][2] * point.z + matrix.m[0][3],
        matrix.m[1][0] * point.x + matrix.m[1][1] * point.y +
            matrix.m[1][2] * point.z + matrix.m[1][3],
        matrix.m[2][0] * point.x + matrix.m[2][1] * point.y +
            matrix.m[2][2] * point.z + matrix.m[2][3],
    };
}

bool validateModelTransform(const Matrix4x4& worldFromModel) noexcept {
    const float determinant = determinant3(worldFromModel);
    return finite(worldFromModel) && affine(worldFromModel) && finite(determinant) &&
           determinant > 0.0f;
}

bool validateRigidCameraTransform(const Matrix4x4& worldFromCamera) noexcept {
    if (!finite(worldFromCamera) || !affine(worldFromCamera)) {
        return false;
    }
    const float determinant = determinant3(worldFromCamera);
    return finite(determinant) && determinant > 0.0f &&
           approximately(dotColumn(worldFromCamera, 0, 0), 1.0f) &&
           approximately(dotColumn(worldFromCamera, 1, 1), 1.0f) &&
           approximately(dotColumn(worldFromCamera, 2, 2), 1.0f) &&
           approximately(dotColumn(worldFromCamera, 0, 1), 0.0f) &&
           approximately(dotColumn(worldFromCamera, 0, 2), 0.0f) &&
           approximately(dotColumn(worldFromCamera, 1, 2), 0.0f);
}

bool makeCameraFromModel(const Matrix4x4& worldFromModel,
                         const Matrix4x4& worldFromCamera,
                         Matrix4x4& cameraFromModel) noexcept {
    if (!validateModelTransform(worldFromModel) ||
        !validateRigidCameraTransform(worldFromCamera)) {
        return false;
    }
    const Matrix4x4 engineCameraFromModel =
        multiply(inverseRigid(worldFromCamera), worldFromModel);
    cameraFromModel = engineCameraFromModel;
    // Generated camera space is forward -Z. Geometry uses positive depth, so
    // reflect only the camera-space Z row and preserve screen right/up.
    for (float& component : cameraFromModel.m[2]) {
        component = -component;
    }
    return finite(cameraFromModel);
}

} // namespace helengine::dreamcast
