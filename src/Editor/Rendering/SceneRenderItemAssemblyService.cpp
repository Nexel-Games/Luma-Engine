#include "Luma/Editor/Rendering/SceneRenderItemAssemblyService.h"

#include <array>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "Luma/Renderer/PrimitiveMeshFactory.h"

namespace
{
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    Vec3 RotateByEulerDegrees(const Vec3& point, const std::array<float, 3>& degrees)
    {
        constexpr float kPi = 3.14159265359f;
        const float rx = degrees[0] * (kPi / 180.0f);
        const float ry = degrees[1] * (kPi / 180.0f);
        const float rz = degrees[2] * (kPi / 180.0f);

        const float cosX = std::cos(rx);
        const float sinX = std::sin(rx);
        const float cosY = std::cos(ry);
        const float sinY = std::sin(ry);
        const float cosZ = std::cos(rz);
        const float sinZ = std::sin(rz);

        Vec3 p = point;

        const Vec3 rotatedX {
            p.x,
            p.y * cosX - p.z * sinX,
            p.y * sinX + p.z * cosX
        };
        p = rotatedX;

        const Vec3 rotatedY {
            p.x * cosY + p.z * sinY,
            p.y,
            -p.x * sinY + p.z * cosY
        };
        p = rotatedY;

        return {
            p.x * cosZ - p.y * sinZ,
            p.x * sinZ + p.y * cosZ,
            p.z
        };
    }

    std::array<float, 16> BuildTransformMatrix(
        const std::array<float, 3>& translation,
        const std::array<float, 3>& rotationDegrees,
        const std::array<float, 3>& scale)
    {
        const Vec3 xAxis = RotateByEulerDegrees(Vec3 { scale[0], 0.0f, 0.0f }, rotationDegrees);
        const Vec3 yAxis = RotateByEulerDegrees(Vec3 { 0.0f, scale[1], 0.0f }, rotationDegrees);
        const Vec3 zAxis = RotateByEulerDegrees(Vec3 { 0.0f, 0.0f, scale[2] }, rotationDegrees);

        return {
            xAxis.x, xAxis.y, xAxis.z, 0.0f,
            yAxis.x, yAxis.y, yAxis.z, 0.0f,
            zAxis.x, zAxis.y, zAxis.z, 0.0f,
            translation[0], translation[1], translation[2], 1.0f
        };
    }
}

namespace Luma::Editor
{
    void SceneRenderItemAssemblyService::BuildInto(
        const SceneRenderItemAssemblyContext& context,
        SceneRenderItemAssemblyScratch& scratch,
        std::vector<SceneRenderItem>& outRenderItems) const
    {
        outRenderItems.clear();
        if (context.pendingRenderSources == nullptr)
        {
            return;
        }

        outRenderItems.reserve(context.pendingRenderSources->size());
        scratch.sharedMeshPayloads.clear();
        scratch.sharedMeshPayloads.reserve(context.pendingRenderSources->size());
        scratch.emittedMeshPayloads.clear();
        scratch.emittedMeshPayloads.reserve(context.pendingRenderSources->size());

        for (const PendingSceneRenderSource& pending : *context.pendingRenderSources)
        {
            const auto& transform = *pending.transform;
            const auto& meshRenderer = *pending.meshRenderer;
            const PrimitiveMeshData& geometry = *pending.geometry;

            SceneRenderItem renderItem;
            renderItem.key = pending.renderKey;
            renderItem.meshKey = pending.meshKey;
            if (scratch.sharedMeshPayloads.find(pending.meshKey) == scratch.sharedMeshPayloads.end())
            {
                scratch.sharedMeshPayloads.emplace(pending.meshKey, PrimitiveMeshFactory::BuildMeshDesc(geometry));
            }
            if (scratch.emittedMeshPayloads.insert(pending.meshKey).second)
            {
                renderItem.mesh = scratch.sharedMeshPayloads[pending.meshKey];
            }
            renderItem.meshRevision = pending.meshRevision;
            renderItem.worldPosition = transform.worldPosition;
            renderItem.worldTransform =
                BuildTransformMatrix(transform.worldPosition, transform.worldRotation, transform.worldScale);
            renderItem.material.baseColor = {
                meshRenderer.color[0],
                meshRenderer.color[1],
                meshRenderer.color[2],
                meshRenderer.color[3]
            };

            if (pending.sourceMaterial != nullptr && context.buildImportedMaterialProxy)
            {
                renderItem.material =
                    context.buildImportedMaterialProxy(*pending.sourceMaterial, pending.sourceMaterialPath);
            }

            MaterialRenderProxy overrideMaterialProxy;
            if (context.tryBuildSlotMaterialOverride &&
                context.tryBuildSlotMaterialOverride(meshRenderer, pending.materialSlotIndex, overrideMaterialProxy))
            {
                renderItem.material = std::move(overrideMaterialProxy);
            }
            else
            {
                MaterialRenderProxy entityMaterialProxy;
                if (context.tryBuildEntityMaterialOverride &&
                    context.tryBuildEntityMaterialOverride(
                        pending.entity,
                        pending.sourceMaterial,
                        entityMaterialProxy))
                {
                    renderItem.material = std::move(entityMaterialProxy);
                }
            }

            renderItem.material.baseColor = {
                renderItem.material.baseColor[0] * meshRenderer.color[0],
                renderItem.material.baseColor[1] * meshRenderer.color[1],
                renderItem.material.baseColor[2] * meshRenderer.color[2],
                renderItem.material.baseColor[3] * meshRenderer.color[3]
            };

            if (renderItem.material.opacityTexture.empty() &&
                renderItem.material.blendMode == 0u)
            {
                renderItem.material.opacity = 1.0f;
            }

            renderItem.revision = context.renderItemsStateHash == 0 ? 1 : context.renderItemsStateHash;
            outRenderItems.push_back(std::move(renderItem));
        }
    }
}
