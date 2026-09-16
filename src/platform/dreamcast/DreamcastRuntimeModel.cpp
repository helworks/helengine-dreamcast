#include "DreamcastRuntimeModel.hpp"

#if defined(HELENGINE_DREAMCAST_HAS_GENERATED_CORE)

#include "ModelPrimitiveTopology.hpp"
#include "ModelSubmeshAsset.hpp"
#include "RuntimeSubmesh.hpp"
#include "runtime/array.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace helengine::dreamcast {
namespace {

constexpr std::uint16_t kMaxVertices = 64;
constexpr std::uint16_t kMaxTriangles = 128;

bool finite(float value) noexcept {
    return std::isfinite(value);
}

void destroyModelAsset(::ModelAsset* asset) noexcept {
    if (asset == nullptr) {
        return;
    }
    asset->Dispose();
    if (asset->FormerAuthoringAssetIds != nullptr &&
        asset->FormerAuthoringAssetIds != ::Array<std::string>::Empty()) {
        delete asset->FormerAuthoringAssetIds;
    }
    asset->FormerAuthoringAssetIds = nullptr;
    delete asset;
}

std::uint32_t sourceIndexCount(const ::ModelAsset& asset) {
    const bool has16 = asset.Indices16 != nullptr && asset.Indices16->get_Length() != 0;
    const bool has32 = asset.Indices32 != nullptr && asset.Indices32->get_Length() != 0;
    if (has16 == has32) {
        throw std::invalid_argument("Dreamcast model requires exactly one nonempty index buffer.");
    }
    const std::uint32_t count = static_cast<std::uint32_t>(
        has16 ? asset.Indices16->get_Length() : asset.Indices32->get_Length());
    if (count == 0 || count % 3 != 0 || count / 3 > kMaxTriangles) {
        throw std::invalid_argument("Dreamcast model index count must contain 1..128 triangles.");
    }
    return count;
}

void validateAsset(const ::ModelAsset& asset, std::uint32_t indexCount) {
    if (asset.Positions == nullptr || asset.Positions->get_Length() == 0 ||
        asset.Positions->get_Length() > kMaxVertices) {
        throw std::invalid_argument("Dreamcast model positions must contain 1..64 values.");
    }
    // A model without explicit submeshes is one implicit submesh covering every index,
    // matching the engine's ModelSubmeshResolver; otherwise exactly one covering submesh.
    if (asset.Submeshes != nullptr && asset.Submeshes->get_Length() != 0) {
        if (asset.Submeshes->get_Length() != 1 || (*asset.Submeshes)[0] == nullptr) {
            throw std::invalid_argument("Dreamcast model supports at most one submesh.");
        }
        const ::ModelSubmeshAsset& submesh = *(*asset.Submeshes)[0];
        if (submesh.IndexStart != 0 || submesh.IndexCount != static_cast<int32_t>(indexCount)) {
            throw std::invalid_argument("Dreamcast model submesh must cover the complete index buffer.");
        }
    }
    if (!finite(asset.BoundsMin.X) || !finite(asset.BoundsMin.Y) ||
        !finite(asset.BoundsMin.Z) || !finite(asset.BoundsMax.X) ||
        !finite(asset.BoundsMax.Y) || !finite(asset.BoundsMax.Z)) {
        throw std::invalid_argument("Dreamcast model bounds must be finite.");
    }
    for (int32_t index = 0; index < asset.Positions->get_Length(); ++index) {
        const ::float3& position = (*asset.Positions)[index];
        if (!finite(position.X) || !finite(position.Y) || !finite(position.Z)) {
            throw std::invalid_argument("Dreamcast model positions must be finite.");
        }
    }
    if (asset.Normals != nullptr && asset.Normals->get_Length() != 0) {
        if (asset.Normals->get_Length() != asset.Positions->get_Length()) {
            throw std::invalid_argument("Dreamcast model normal count must match its position count.");
        }
        for (int32_t index = 0; index < asset.Normals->get_Length(); ++index) {
            const ::float3& normal = (*asset.Normals)[index];
            if (!finite(normal.X) || !finite(normal.Y) || !finite(normal.Z)) {
                throw std::invalid_argument("Dreamcast model normals must be finite.");
            }
        }
    }
}

std::uint32_t readIndex(const ::ModelAsset& asset, std::uint32_t index) noexcept {
    if (asset.Indices16 != nullptr && asset.Indices16->get_Length() != 0) {
        return (*asset.Indices16)[static_cast<int32_t>(index)];
    }
    return (*asset.Indices32)[static_cast<int32_t>(index)];
}

} // namespace

DreamcastRuntimeModel::DreamcastRuntimeModel() : ::RuntimeModel() {
}

DreamcastRuntimeModel::~DreamcastRuntimeModel() {
    Dispose();
}

void DreamcastRuntimeModel::Dispose() {
    if (disposed_) {
        return;
    }
    disposed_ = true;
    delete[] vertices_;
    vertices_ = nullptr;
    delete[] triangles_;
    triangles_ = nullptr;
    vertexCount_ = 0;
    triangleCount_ = 0;
    hasNormals_ = false;
    ::ModelAsset* rawAsset = get_RawModelAsset();
    if (rawAsset != nullptr) {
        if (rawAsset->FormerAuthoringAssetIds != nullptr &&
            rawAsset->FormerAuthoringAssetIds != ::Array<std::string>::Empty()) {
            delete rawAsset->FormerAuthoringAssetIds;
        }
        rawAsset->FormerAuthoringAssetIds = nullptr;
    }
    ::RuntimeModel::Dispose();
}

MeshView DreamcastRuntimeModel::meshView() const noexcept {
    return MeshView{vertices_, vertexCount_, triangles_, triangleCount_, hasNormals_};
}

DreamcastRuntimeModel* DreamcastRuntimeModel::CreateOwned(::ModelAsset* asset) {
    if (asset == nullptr) {
        throw std::invalid_argument("Dreamcast model asset cannot be null.");
    }
    struct AssetLease {
        ::ModelAsset* value;
        ~AssetLease() {
            destroyModelAsset(value);
        }
        void release() noexcept {
            value = nullptr;
        }
    } assetLease{asset};
    auto runtime = std::unique_ptr<DreamcastRuntimeModel>(new DreamcastRuntimeModel());
    try {
        const std::uint32_t indexCount = sourceIndexCount(*assetLease.value);
        validateAsset(*assetLease.value, indexCount);

        runtime->vertexCount_ = static_cast<std::uint16_t>(assetLease.value->Positions->get_Length());
        runtime->triangleCount_ = static_cast<std::uint16_t>(indexCount / 3);
        runtime->vertices_ = new SourceVertex[runtime->vertexCount_]();
        runtime->triangles_ = new TriangleIndex[runtime->triangleCount_]();
        runtime->hasNormals_ = assetLease.value->Normals != nullptr &&
                               assetLease.value->Normals->get_Length() == runtime->vertexCount_;
        for (std::uint16_t index = 0; index < runtime->vertexCount_; ++index) {
            const ::float3& position = (*assetLease.value->Positions)[index];
            SourceVertex vertex{{position.X, position.Y, position.Z},
                                {1.0f, 1.0f, 1.0f, 1.0f},
                                {0.0f, 0.0f, 0.0f}};
            if (runtime->hasNormals_) {
                const ::float3& normal = (*assetLease.value->Normals)[index];
                vertex.normal = Vec3{normal.X, normal.Y, normal.Z};
            }
            runtime->vertices_[index] = vertex;
        }
        for (std::uint32_t index = 0; index < indexCount; ++index) {
            const std::uint32_t value = readIndex(*assetLease.value, index);
            if (value >= runtime->vertexCount_) {
                throw std::invalid_argument("Dreamcast model index exceeds the position buffer.");
            }
            const std::uint16_t converted = static_cast<std::uint16_t>(value);
            TriangleIndex& triangle = runtime->triangles_[index / 3];
            if (index % 3 == 0) {
                triangle.a = converted;
            } else if (index % 3 == 1) {
                triangle.b = converted;
            } else {
                triangle.c = converted;
            }
        }
        runtime->SetBounds(::float3(assetLease.value->BoundsMin.X, assetLease.value->BoundsMin.Y,
                                    assetLease.value->BoundsMin.Z),
                           ::float3(assetLease.value->BoundsMax.X, assetLease.value->BoundsMax.Y,
                                    assetLease.value->BoundsMax.Z));
        runtime->set_Id(assetLease.value->get_Id());
        auto submeshes = std::unique_ptr<::Array<::RuntimeSubmesh*>>(
            new ::Array<::RuntimeSubmesh*>(1));
        auto submesh = std::unique_ptr<::RuntimeSubmesh>(new ::RuntimeSubmesh());
        const bool hasExplicitSubmesh =
            assetLease.value->Submeshes != nullptr && assetLease.value->Submeshes->get_Length() == 1;
        submesh->set_MaterialSlotName(
            hasExplicitSubmesh ? (*assetLease.value->Submeshes)[0]->MaterialSlotName : std::string());
        submesh->set_IndexStart(0);
        submesh->set_IndexCount(static_cast<int32_t>(indexCount));
        submesh->set_PrimitiveTopology(::ModelPrimitiveTopology::TriangleList);
        (*submeshes)[0] = submesh.get();
        runtime->SetSubmeshes(submeshes.get());
        submeshes.reset();
        submesh.release();
        runtime->SetRawModelAsset(assetLease.value);
        assetLease.release();
        return runtime.release();
    } catch (...) {
        throw;
    }
}

} // namespace helengine::dreamcast

#endif
