#include "platform/dreamcast/DreamcastAuthoredTransform.hpp"

#include <cmath>
#include <iostream>

namespace {

using helengine::dreamcast::GeneratedMatrixFields;
using helengine::dreamcast::Matrix4x4;
using helengine::dreamcast::Vec3;

int failures = 0;

void expect(bool condition, const char* expression) {
    if (!condition) {
        std::cerr << "expected " << expression << '\n';
        ++failures;
    }
}

#define EXPECT(value) expect((value), #value)

bool near(float left, float right, float epsilon = 1.0e-4f) {
    return std::fabs(left - right) <= epsilon;
}

void expectPoint(Vec3 actual, Vec3 expected) {
    EXPECT(near(actual.x, expected.x));
    EXPECT(near(actual.y, expected.y));
    EXPECT(near(actual.z, expected.z));
}

Matrix4x4 identity() {
    return Matrix4x4{{
        {1.0f, 0.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f},
    }};
}

GeneratedMatrixFields generatedFromNative(const Matrix4x4& native) {
    return GeneratedMatrixFields{
        native.m[0][0], native.m[1][0], native.m[2][0], native.m[3][0],
        native.m[0][1], native.m[1][1], native.m[2][1], native.m[3][1],
        native.m[0][2], native.m[1][2], native.m[2][2], native.m[3][2],
        native.m[0][3], native.m[1][3], native.m[2][3], native.m[3][3],
    };
}

void GeneratedTranslationIsTransposedIntoNativeColumnStorage() {
    const GeneratedMatrixFields generated{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 2.0f, -5.0f, 1.0f,
    };
    expectPoint(helengine::dreamcast::transformPoint(
                    helengine::dreamcast::matrixFromGenerated(generated), {0.0f, 0.0f, 0.0f}),
                {1.0f, 2.0f, -5.0f});
}

void CameraForwardIsConvertedToPositiveDepth() {
    Matrix4x4 model = identity();
    model.m[0][3] = 1.0f;
    model.m[2][3] = -5.0f;
    Matrix4x4 result{};
    EXPECT(helengine::dreamcast::makeCameraFromModel(model, identity(), result));
    expectPoint(helengine::dreamcast::transformPoint(result, {0.0f, 0.0f, 0.0f}),
                {1.0f, 0.0f, 5.0f});
}

void CameraTranslationAndRotationRemainRigid() {
    Matrix4x4 camera = identity();
    camera.m[0][0] = 0.0f;
    camera.m[0][2] = -1.0f;
    camera.m[2][0] = 1.0f;
    camera.m[2][2] = 0.0f;
    camera.m[0][3] = 2.0f;
    camera.m[2][3] = 3.0f;
    Matrix4x4 model = identity();
    model.m[0][3] = 7.0f;
    model.m[2][3] = 3.0f;
    Matrix4x4 result{};
    EXPECT(helengine::dreamcast::validateRigidCameraTransform(camera));
    EXPECT(helengine::dreamcast::makeCameraFromModel(model, camera, result));
    expectPoint(helengine::dreamcast::transformPoint(result, {0.0f, 0.0f, 0.0f}),
                {0.0f, 0.0f, 5.0f});
}

void ModelTransformRejectsMirrorsAndSingularMatrices() {
    Matrix4x4 mirrored = identity();
    mirrored.m[0][0] = -1.0f;
    EXPECT(!helengine::dreamcast::validateModelTransform(mirrored));
    Matrix4x4 singular = identity();
    singular.m[1][1] = 0.0f;
    EXPECT(!helengine::dreamcast::validateModelTransform(singular));
    Matrix4x4 scaled = identity();
    scaled.m[0][0] = 2.0f;
    scaled.m[1][1] = 1.5f;
    EXPECT(helengine::dreamcast::validateModelTransform(scaled));
    Matrix4x4 small = identity();
    small.m[0][0] = 0.01f;
    small.m[1][1] = 0.01f;
    small.m[2][2] = 0.01f;
    EXPECT(helengine::dreamcast::validateModelTransform(small));
}

void CameraRejectsScaleShearAndNonFiniteValues() {
    Matrix4x4 scaled = identity();
    scaled.m[0][0] = 2.0f;
    EXPECT(!helengine::dreamcast::validateRigidCameraTransform(scaled));
    Matrix4x4 sheared = identity();
    sheared.m[0][1] = 0.1f;
    EXPECT(!helengine::dreamcast::validateRigidCameraTransform(sheared));
    Matrix4x4 nonFinite = identity();
    nonFinite.m[2][2] = NAN;
    EXPECT(!helengine::dreamcast::validateRigidCameraTransform(nonFinite));
}

void GeneratedHierarchyMatchesParentTimesChildNativeOrder() {
    Matrix4x4 parent = identity();
    parent.m[0][0] = 0.0f;
    parent.m[0][2] = -1.0f;
    parent.m[2][0] = 1.0f;
    parent.m[2][2] = 0.0f;
    parent.m[0][3] = 2.0f;
    parent.m[2][3] = -3.0f;
    Matrix4x4 child = identity();
    child.m[0][0] = 2.0f;
    child.m[1][1] = 1.5f;
    child.m[2][2] = 0.5f;
    child.m[0][3] = 1.0f;
    child.m[2][3] = -2.0f;
    const Matrix4x4 expectedWorld = helengine::dreamcast::multiply(parent, child);

    // Generated hierarchy computes local * parent in its row-vector fields.
    const GeneratedMatrixFields generatedParent = generatedFromNative(parent);
    const GeneratedMatrixFields generatedChild = generatedFromNative(child);
    const Matrix4x4 generatedWorld = helengine::dreamcast::matrixFromGenerated(
        GeneratedMatrixFields{
            generatedChild.M11 * generatedParent.M11 + generatedChild.M12 * generatedParent.M21 +
                generatedChild.M13 * generatedParent.M31 + generatedChild.M14 * generatedParent.M41,
            generatedChild.M11 * generatedParent.M12 + generatedChild.M12 * generatedParent.M22 +
                generatedChild.M13 * generatedParent.M32 + generatedChild.M14 * generatedParent.M42,
            generatedChild.M11 * generatedParent.M13 + generatedChild.M12 * generatedParent.M23 +
                generatedChild.M13 * generatedParent.M33 + generatedChild.M14 * generatedParent.M43,
            generatedChild.M11 * generatedParent.M14 + generatedChild.M12 * generatedParent.M24 +
                generatedChild.M13 * generatedParent.M34 + generatedChild.M14 * generatedParent.M44,
            generatedChild.M21 * generatedParent.M11 + generatedChild.M22 * generatedParent.M21 +
                generatedChild.M23 * generatedParent.M31 + generatedChild.M24 * generatedParent.M41,
            generatedChild.M21 * generatedParent.M12 + generatedChild.M22 * generatedParent.M22 +
                generatedChild.M23 * generatedParent.M32 + generatedChild.M24 * generatedParent.M42,
            generatedChild.M21 * generatedParent.M13 + generatedChild.M22 * generatedParent.M23 +
                generatedChild.M23 * generatedParent.M33 + generatedChild.M24 * generatedParent.M43,
            generatedChild.M21 * generatedParent.M14 + generatedChild.M22 * generatedParent.M24 +
                generatedChild.M23 * generatedParent.M34 + generatedChild.M24 * generatedParent.M44,
            generatedChild.M31 * generatedParent.M11 + generatedChild.M32 * generatedParent.M21 +
                generatedChild.M33 * generatedParent.M31 + generatedChild.M34 * generatedParent.M41,
            generatedChild.M31 * generatedParent.M12 + generatedChild.M32 * generatedParent.M22 +
                generatedChild.M33 * generatedParent.M32 + generatedChild.M34 * generatedParent.M42,
            generatedChild.M31 * generatedParent.M13 + generatedChild.M32 * generatedParent.M23 +
                generatedChild.M33 * generatedParent.M33 + generatedChild.M34 * generatedParent.M43,
            generatedChild.M31 * generatedParent.M14 + generatedChild.M32 * generatedParent.M24 +
                generatedChild.M33 * generatedParent.M34 + generatedChild.M34 * generatedParent.M44,
            generatedChild.M41 * generatedParent.M11 + generatedChild.M42 * generatedParent.M21 +
                generatedChild.M43 * generatedParent.M31 + generatedChild.M44 * generatedParent.M41,
            generatedChild.M41 * generatedParent.M12 + generatedChild.M42 * generatedParent.M22 +
                generatedChild.M43 * generatedParent.M32 + generatedChild.M44 * generatedParent.M42,
            generatedChild.M41 * generatedParent.M13 + generatedChild.M42 * generatedParent.M23 +
                generatedChild.M43 * generatedParent.M33 + generatedChild.M44 * generatedParent.M43,
            generatedChild.M41 * generatedParent.M14 + generatedChild.M42 * generatedParent.M24 +
                generatedChild.M43 * generatedParent.M34 + generatedChild.M44 * generatedParent.M44,
        });
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            EXPECT(near(generatedWorld.m[row][column], expectedWorld.m[row][column]));
        }
    }
}

void NormalMatrixKeepsNormalsPerpendicularUnderNonuniformScale() {
    using helengine::dreamcast::makeNormalMatrix;
    // Squash Y to half. A model normal of (0,1,1)/sqrt(2) belongs to a face
    // whose tangent (0,-1,1) becomes (0,-0.5,1) after the squash.
    Matrix4x4 worldFromModel = identity();
    worldFromModel.m[1][1] = 0.5f;
    Matrix4x4 normalMatrix{};
    EXPECT(makeNormalMatrix(worldFromModel, normalMatrix));

    const float inverseRootTwo = 0.70710678f;
    Vec3 worldNormal = helengine::dreamcast::transformDirection(
        normalMatrix, Vec3{0.0f, inverseRootTwo, inverseRootTwo});
    const float length = std::sqrt(worldNormal.x * worldNormal.x +
                                   worldNormal.y * worldNormal.y +
                                   worldNormal.z * worldNormal.z);
    worldNormal = Vec3{worldNormal.x / length, worldNormal.y / length, worldNormal.z / length};

    EXPECT(near(worldNormal.x, 0.0f));
    EXPECT(near(worldNormal.y, 0.89442719f));
    EXPECT(near(worldNormal.z, 0.44721360f));
    // Perpendicular to the transformed tangent.
    const float dot = worldNormal.x * 0.0f + worldNormal.y * -0.5f + worldNormal.z * 1.0f;
    EXPECT(near(dot, 0.0f));
}

void NormalMatrixRejectsInvalidModelTransforms() {
    using helengine::dreamcast::makeNormalMatrix;
    Matrix4x4 mirrored = identity();
    mirrored.m[0][0] = -1.0f;
    Matrix4x4 normalMatrix{};
    EXPECT(!makeNormalMatrix(mirrored, normalMatrix));
}

void DirectionalLightForwardMatchesEngineRotation() {
    using helengine::dreamcast::directionalLightForward;
    using helengine::dreamcast::GeneratedQuaternion;

    Vec3 identityForward = directionalLightForward(GeneratedQuaternion{0.0f, 0.0f, 0.0f, 1.0f});
    EXPECT(near(identityForward.x, 0.0f));
    EXPECT(near(identityForward.y, 0.0f));
    EXPECT(near(identityForward.z, -1.0f));

    // Ninety degrees about +Y takes -Z to -X.
    const float halfRootTwo = 0.70710678f;
    Vec3 yawed = directionalLightForward(GeneratedQuaternion{0.0f, halfRootTwo, 0.0f, halfRootTwo});
    EXPECT(near(yawed.x, -1.0f));
    EXPECT(near(yawed.y, 0.0f));
    EXPECT(near(yawed.z, 0.0f));
}

} // namespace

int main() {
    GeneratedTranslationIsTransposedIntoNativeColumnStorage();
    CameraForwardIsConvertedToPositiveDepth();
    CameraTranslationAndRotationRemainRigid();
    ModelTransformRejectsMirrorsAndSingularMatrices();
    CameraRejectsScaleShearAndNonFiniteValues();
    GeneratedHierarchyMatchesParentTimesChildNativeOrder();
    NormalMatrixKeepsNormalsPerpendicularUnderNonuniformScale();
    NormalMatrixRejectsInvalidModelTransforms();
    DirectionalLightForwardMatchesEngineRotation();
    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return 1;
    }
    std::cout << "DreamcastAuthoredTransformTests: all tests passed\n";
    return 0;
}
