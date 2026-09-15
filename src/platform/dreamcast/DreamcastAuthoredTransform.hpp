#pragma once

#include "DreamcastGeometryPipeline.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)
struct float4x4;
#endif

namespace helengine::dreamcast {

/// The generated float4x4 fields are row-vector fields: translation is stored
/// in M41..M43 and generated hierarchy multiplication is local * parent. The
/// native geometry pipeline stores an equivalent matrix for column-vector
/// multiplication, so the adapter transposes those fields exactly once.
struct GeneratedMatrixFields {
    float M11;
    float M12;
    float M13;
    float M14;
    float M21;
    float M22;
    float M23;
    float M24;
    float M31;
    float M32;
    float M33;
    float M34;
    float M41;
    float M42;
    float M43;
    float M44;
};

Matrix4x4 matrixFromGenerated(const GeneratedMatrixFields& generated) noexcept;

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)
Matrix4x4 matrixFromGenerated(const ::float4x4& generated) noexcept;
#endif

Matrix4x4 multiply(const Matrix4x4& left, const Matrix4x4& right) noexcept;

Vec3 transformPoint(const Matrix4x4& matrix, const Vec3& point) noexcept;

bool validateModelTransform(const Matrix4x4& worldFromModel) noexcept;

bool validateRigidCameraTransform(const Matrix4x4& worldFromCamera) noexcept;

/// Builds renderer camera space from engine world matrices. The generated
/// camera looks along -Z; the final row reflection maps that forward axis to
/// the positive depth convention used by the Dreamcast geometry pipeline.
bool makeCameraFromModel(const Matrix4x4& worldFromModel,
                         const Matrix4x4& worldFromCamera,
                         Matrix4x4& cameraFromModel) noexcept;

} // namespace helengine::dreamcast
