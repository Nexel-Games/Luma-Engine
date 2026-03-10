#include "Luma/Editor/Rendering/SceneRenderCacheBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>

namespace
{
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    Vec3 operator+(const Vec3& lhs, const Vec3& rhs)
    {
        return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
    }

    Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
    {
        return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
    }

    Vec3 operator*(const Vec3& value, const float scalar)
    {
        return { value.x * scalar, value.y * scalar, value.z * scalar };
    }

    float Dot(const Vec3& lhs, const Vec3& rhs)
    {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }

    Vec3 Normalize(const Vec3& value)
    {
        const float lengthSq = Dot(value, value);
        if (lengthSq <= 1.0e-8f)
        {
            return {};
        }

        const float invLength = 1.0f / std::sqrt(lengthSq);
        return value * invLength;
    }

    float Length(const Vec3& value)
    {
        return std::sqrt(Dot(value, value));
    }

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

        const Vec3 rotatedZ {
            p.x * cosZ - p.y * sinZ,
            p.x * sinZ + p.y * cosZ,
            p.z
        };
        return rotatedZ;
    }

    std::string ToLowerString(std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](const unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
        return value;
    }

    std::uint64_t HashBytes(const void* data, const std::size_t size, std::uint64_t seed)
    {
        constexpr std::uint64_t kFnvPrime = 1099511628211ull;
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t i = 0; i < size; ++i)
        {
            seed ^= static_cast<std::uint64_t>(bytes[i]);
            seed *= kFnvPrime;
        }
        return seed;
    }
}

namespace Luma::Editor
{
    SceneRenderCacheBuildResult SceneRenderCacheBuilder::Build(const SceneRenderCacheBuildContext& context) const
    {
        SceneRenderCacheBuildResult result;
        if (context.scene == nullptr)
        {
            return result;
        }

        auto& registry = context.scene->GetRegistry();
        result.vertices.reserve(1024);
        result.indices.reserve(3072);
        result.skyVertices.reserve(512);
        result.skyIndices.reserve(1536);
        if (context.collectRenderSources)
        {
            const auto view = registry.view<TransformComponent, MeshRendererComponent>();
            result.pendingRenderSources.reserve(static_cast<std::size_t>(view.size_hint()));
        }

        if (context.buildSkyPrimitiveMesh)
        {
            const EntityID skyEntity = context.findPrimarySkyEntity ? context.findPrimarySkyEntity() : entt::null;
            if (skyEntity != entt::null && registry.valid(skyEntity) && registry.all_of<SkyLightComponent>(skyEntity))
            {
                const auto& skyLight = registry.get<SkyLightComponent>(skyEntity);
                if (skyLight.active)
                {
                    const PrimitiveMeshData& skySphere = PrimitiveMeshFactory::GetSkySphere();
                    if (!skySphere.vertices.empty() && !skySphere.indices.empty())
                    {
                        const bool hasEnvironment =
                            context.skyEnvironmentLinearPixels != nullptr &&
                            !context.skyEnvironmentLinearPixels->empty() &&
                            context.skyEnvironmentWidth > 0 &&
                            context.skyEnvironmentHeight > 0;
                        constexpr float kSkyRadius = 900.0f;
                        const std::uint32_t indexBase = static_cast<std::uint32_t>(result.skyVertices.size());
                        for (const PrimitiveVertex& sourceVertex : skySphere.vertices)
                        {
                            const Vec3 localDirection = Normalize({
                                sourceVertex.position[0],
                                sourceVertex.position[1],
                                sourceVertex.position[2]
                            });
                            const Vec3 worldPosition = localDirection * kSkyRadius;

                            std::array<float, 3> linearSkyColor {};
                            if (context.computeSkyColor)
                            {
                                linearSkyColor = context.computeSkyColor(
                                    { localDirection.x, localDirection.y, localDirection.z },
                                    skyLight,
                                    hasEnvironment);
                            }

                            PrimitiveVertex outputVertex = sourceVertex;
                            outputVertex.position = { worldPosition.x, worldPosition.y, worldPosition.z };
                            outputVertex.color = {
                                std::max(0.0f, linearSkyColor[0]),
                                std::max(0.0f, linearSkyColor[1]),
                                std::max(0.0f, linearSkyColor[2])
                            };
                            result.skyVertices.push_back(outputVertex);
                        }

                        for (const std::uint32_t sourceIndex : skySphere.indices)
                        {
                            const std::uint64_t absoluteIndex =
                                static_cast<std::uint64_t>(indexBase) + static_cast<std::uint64_t>(sourceIndex);
                            if (absoluteIndex > static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max()))
                            {
                                result.skyIndexOverflow = true;
                                break;
                            }
                            result.skyIndices.push_back(static_cast<std::uint32_t>(absoluteIndex));
                        }
                    }
                }
            }
        }

        if (context.collectRenderSources)
        {
            const auto appendCommonRenderStateHash =
                [&](const EntityID entity, const TransformComponent& transform, const MeshRendererComponent& meshRenderer)
                {
                    result.renderItemsStateHash = HashBytes(&entity, sizeof(entity), result.renderItemsStateHash);
                    result.renderItemsStateHash =
                        HashBytes(transform.worldPosition.data(), sizeof(transform.worldPosition), result.renderItemsStateHash);
                    result.renderItemsStateHash =
                        HashBytes(transform.worldRotation.data(), sizeof(transform.worldRotation), result.renderItemsStateHash);
                    result.renderItemsStateHash =
                        HashBytes(transform.worldScale.data(), sizeof(transform.worldScale), result.renderItemsStateHash);
                    result.renderItemsStateHash =
                        HashBytes(meshRenderer.color.data(), sizeof(meshRenderer.color), result.renderItemsStateHash);
                    for (const std::string& materialOverride : meshRenderer.materialOverrides)
                    {
                        if (!materialOverride.empty())
                        {
                            result.renderItemsStateHash = HashBytes(
                                materialOverride.data(),
                                materialOverride.size(),
                                result.renderItemsStateHash);
                        }
                    }
                };

            const auto view = registry.view<TransformComponent, MeshRendererComponent>();
            for (const EntityID entity : view)
            {
                const auto& transform = view.get<TransformComponent>(entity);
                const auto& meshRenderer = view.get<MeshRendererComponent>(entity);
                if (!meshRenderer.visible)
                {
                    continue;
                }

                if (!meshRenderer.importedSceneSource.empty())
                {
                    MeshStreamingImportedScenePartsState* partsState =
                        context.resolveImportedSceneParts ? context.resolveImportedSceneParts(meshRenderer.importedSceneSource) : nullptr;
                    if (partsState == nullptr || partsState->parts.empty())
                    {
                        continue;
                    }

                    for (std::size_t partIndex = 0; partIndex < partsState->parts.size(); ++partIndex)
                    {
                        const Assets::MeshScenePart& part = partsState->parts[partIndex];
                        if (part.mesh.vertices.empty() || part.mesh.indices.empty())
                        {
                            continue;
                        }

                        std::string meshKey =
                            "importedscene:" + meshRenderer.importedSceneSource +
                            "|part=" + std::to_string(partIndex);
                        std::uint64_t meshRevision = 1469598103934665603ull;
                        meshRevision = HashBytes(meshKey.data(), meshKey.size(), meshRevision);
                        const std::size_t vertexCount = part.mesh.vertices.size();
                        const std::size_t indexCount = part.mesh.indices.size();
                        meshRevision = HashBytes(&vertexCount, sizeof(vertexCount), meshRevision);
                        meshRevision = HashBytes(&indexCount, sizeof(indexCount), meshRevision);
                        meshRevision = HashBytes(
                            part.mesh.vertices.front().position.data(),
                            sizeof(part.mesh.vertices.front().position),
                            meshRevision);
                        meshRevision = HashBytes(
                            part.mesh.vertices.back().position.data(),
                            sizeof(part.mesh.vertices.back().position),
                            meshRevision);

                        result.pendingRenderSources.push_back(PendingSceneRenderSource {
                            .entity = entity,
                            .renderKey =
                                "entity:" + std::to_string(static_cast<std::uint32_t>(entity)) +
                                "|importedpart:" + std::to_string(partIndex),
                            .transform = &transform,
                            .meshRenderer = &meshRenderer,
                            .geometry = &part.mesh,
                            .sourceMaterial = &part.material,
                            .sourceMaterialPath = partsState->resolvedPath,
                            .materialSlotIndex = partIndex,
                            .meshKey = std::move(meshKey),
                            .meshRevision = meshRevision });

                        appendCommonRenderStateHash(entity, transform, meshRenderer);
                        result.renderItemsStateHash = HashBytes(&partIndex, sizeof(partIndex), result.renderItemsStateHash);
                        if (!part.name.empty())
                        {
                            result.renderItemsStateHash =
                                HashBytes(part.name.data(), part.name.size(), result.renderItemsStateHash);
                        }
                        result.renderItemsStateHash =
                            HashBytes(&meshRevision, sizeof(meshRevision), result.renderItemsStateHash);
                    }

                    continue;
                }

                const PrimitiveMeshData* geometry =
                    context.resolveMeshRendererGeometry ? context.resolveMeshRendererGeometry(transform, meshRenderer) : nullptr;
                if (geometry == nullptr || geometry->vertices.empty() || geometry->indices.empty())
                {
                    continue;
                }

            if (!meshRenderer.usePrimitive &&
                meshRenderer.meshPartIndex == std::numeric_limits<std::uint32_t>::max() &&
                !meshRenderer.meshSource.empty() &&
                context.streamedMeshAssets != nullptr)
            {
                const std::filesystem::path resolvedMeshPath = Assets::ResolveMeshAssetPath(meshRenderer.meshSource);
                if (!resolvedMeshPath.empty() &&
                    ToLowerString(resolvedMeshPath.extension().string()) == ".lumamesh" &&
                    context.computeRequestedMeshLod)
                {
                    const std::uint32_t targetLod = context.computeRequestedMeshLod(transform, meshRenderer);
                    const std::string cacheKey =
                        resolvedMeshPath.generic_string() + "|lod:" + std::to_string(targetLod);
                    if (const auto streamedStateIt = context.streamedMeshAssets->find(cacheKey);
                        streamedStateIt != context.streamedMeshAssets->end())
                    {
                        const MeshStreamingAssetState& state = streamedStateIt->second;
                        std::size_t sectionMaterialSlotCount = 0;
                        for (const Assets::MeshAssetSection& section : state.assetData.sections)
                        {
                            sectionMaterialSlotCount =
                                std::max(sectionMaterialSlotCount, static_cast<std::size_t>(section.materialSlotIndex) + 1);
                        }

                        if (sectionMaterialSlotCount > 1 && !state.activeSections.empty())
                        {
                            for (const std::uint32_t sectionIndex : state.activeSections)
                            {
                                if (sectionIndex >= state.assetData.sections.size())
                                {
                                    continue;
                                }

                                const Assets::MeshAssetSection& section = state.assetData.sections[sectionIndex];
                                const PrimitiveMeshData* sectionMesh = &section.mesh;
                                if (!section.chunkPath.empty() && sectionIndex < state.sectionStreams.size())
                                {
                                    sectionMesh = &state.sectionStreams[sectionIndex].mesh;
                                }
                                if (sectionMesh == nullptr ||
                                    sectionMesh->vertices.empty() ||
                                    sectionMesh->indices.empty())
                                {
                                    continue;
                                }

                                std::string meshKey =
                                    "asset:" + meshRenderer.meshSource +
                                    "|lod=" + std::to_string(targetLod) +
                                    "|section=" + std::to_string(sectionIndex);
                                std::uint64_t meshRevision = 1469598103934665603ull;
                                meshRevision = HashBytes(meshKey.data(), meshKey.size(), meshRevision);
                                const std::size_t vertexCount = sectionMesh->vertices.size();
                                const std::size_t indexCount = sectionMesh->indices.size();
                                meshRevision = HashBytes(&vertexCount, sizeof(vertexCount), meshRevision);
                                meshRevision = HashBytes(&indexCount, sizeof(indexCount), meshRevision);
                                meshRevision = HashBytes(
                                    sectionMesh->vertices.front().position.data(),
                                    sizeof(sectionMesh->vertices.front().position),
                                    meshRevision);
                                meshRevision = HashBytes(
                                    sectionMesh->vertices.back().position.data(),
                                    sizeof(sectionMesh->vertices.back().position),
                                    meshRevision);

                                result.pendingRenderSources.push_back(PendingSceneRenderSource {
                                    .entity = entity,
                                    .renderKey =
                                        "entity:" + std::to_string(static_cast<std::uint32_t>(entity)) +
                                        "|section:" + std::to_string(sectionIndex),
                                    .transform = &transform,
                                    .meshRenderer = &meshRenderer,
                                    .geometry = sectionMesh,
                                    .sourceMaterial = nullptr,
                                    .sourceMaterialPath = resolvedMeshPath,
                                    .materialSlotIndex = static_cast<std::size_t>(section.materialSlotIndex),
                                    .meshKey = std::move(meshKey),
                                    .meshRevision = meshRevision });

                                result.renderItemsStateHash = HashBytes(&entity, sizeof(entity), result.renderItemsStateHash);
                                result.renderItemsStateHash = HashBytes(transform.worldPosition.data(), sizeof(transform.worldPosition), result.renderItemsStateHash);
                                result.renderItemsStateHash = HashBytes(transform.worldRotation.data(), sizeof(transform.worldRotation), result.renderItemsStateHash);
                                result.renderItemsStateHash = HashBytes(transform.worldScale.data(), sizeof(transform.worldScale), result.renderItemsStateHash);
                                result.renderItemsStateHash = HashBytes(meshRenderer.color.data(), sizeof(meshRenderer.color), result.renderItemsStateHash);
                                for (const std::string& materialOverride : meshRenderer.materialOverrides)
                                {
                                    if (!materialOverride.empty())
                                    {
                                        result.renderItemsStateHash = HashBytes(
                                            materialOverride.data(),
                                            materialOverride.size(),
                                            result.renderItemsStateHash);
                                    }
                                }
                                result.renderItemsStateHash = HashBytes(&sectionIndex, sizeof(sectionIndex), result.renderItemsStateHash);
                                result.renderItemsStateHash = HashBytes(
                                    &section.materialSlotIndex,
                                    sizeof(section.materialSlotIndex),
                                    result.renderItemsStateHash);
                                if (!section.materialSlotName.empty())
                                {
                                    result.renderItemsStateHash = HashBytes(
                                        section.materialSlotName.data(),
                                        section.materialSlotName.size(),
                                        result.renderItemsStateHash);
                                }
                                result.renderItemsStateHash = HashBytes(&meshRevision, sizeof(meshRevision), result.renderItemsStateHash);
                            }

                            continue;
                        }
                    }
                }
            }

            std::string meshKey;
            if (meshRenderer.usePrimitive)
            {
                meshKey = "primitive:" + std::to_string(static_cast<int>(meshRenderer.primitive));
            }
            else
            {
                meshKey =
                    "asset:" + meshRenderer.meshSource +
                    "|lod=" + std::to_string(meshRenderer.meshLod) +
                    "|part=" + std::to_string(meshRenderer.meshPartIndex);
            }

            std::uint64_t meshRevision = 1469598103934665603ull;
            meshRevision = HashBytes(meshKey.data(), meshKey.size(), meshRevision);
            const std::size_t vertexCount = geometry->vertices.size();
            const std::size_t indexCount = geometry->indices.size();
            meshRevision = HashBytes(&vertexCount, sizeof(vertexCount), meshRevision);
            meshRevision = HashBytes(&indexCount, sizeof(indexCount), meshRevision);
            if (!geometry->vertices.empty())
            {
                meshRevision = HashBytes(
                    geometry->vertices.front().position.data(),
                    sizeof(geometry->vertices.front().position),
                    meshRevision);
                meshRevision = HashBytes(
                    geometry->vertices.back().position.data(),
                    sizeof(geometry->vertices.back().position),
                    meshRevision);
            }

            const Assets::MeshMaterialInfo* sourceMaterial = nullptr;
            std::filesystem::path sourceMaterialPath;
            if (meshRenderer.meshPartIndex != std::numeric_limits<std::uint32_t>::max() &&
                !meshRenderer.meshSource.empty())
            {
                if (MeshStreamingImportedScenePartsState* partsState =
                        context.resolveImportedSceneParts ? context.resolveImportedSceneParts(meshRenderer.meshSource) : nullptr;
                    partsState != nullptr &&
                    meshRenderer.meshPartIndex < partsState->parts.size())
                {
                    sourceMaterial = &partsState->parts[meshRenderer.meshPartIndex].material;
                    sourceMaterialPath = partsState->resolvedPath;
                }
            }

                result.pendingRenderSources.push_back(PendingSceneRenderSource {
                    .entity = entity,
                    .renderKey = "entity:" + std::to_string(static_cast<std::uint32_t>(entity)),
                    .transform = &transform,
                    .meshRenderer = &meshRenderer,
                    .geometry = geometry,
                    .sourceMaterial = sourceMaterial,
                    .sourceMaterialPath = std::move(sourceMaterialPath),
                    .materialSlotIndex = 0,
                    .meshKey = std::move(meshKey),
                    .meshRevision = meshRevision });

                result.renderItemsStateHash = HashBytes(&entity, sizeof(entity), result.renderItemsStateHash);
                result.renderItemsStateHash = HashBytes(transform.worldPosition.data(), sizeof(transform.worldPosition), result.renderItemsStateHash);
                result.renderItemsStateHash = HashBytes(transform.worldRotation.data(), sizeof(transform.worldRotation), result.renderItemsStateHash);
                result.renderItemsStateHash = HashBytes(transform.worldScale.data(), sizeof(transform.worldScale), result.renderItemsStateHash);
                result.renderItemsStateHash = HashBytes(meshRenderer.color.data(), sizeof(meshRenderer.color), result.renderItemsStateHash);
                for (const std::string& materialOverride : meshRenderer.materialOverrides)
                {
                    if (!materialOverride.empty())
                    {
                        result.renderItemsStateHash = HashBytes(
                            materialOverride.data(),
                            materialOverride.size(),
                            result.renderItemsStateHash);
                    }
                }
                result.renderItemsStateHash = HashBytes(&meshRenderer.usePrimitive, sizeof(meshRenderer.usePrimitive), result.renderItemsStateHash);
                result.renderItemsStateHash = HashBytes(&meshRenderer.primitive, sizeof(meshRenderer.primitive), result.renderItemsStateHash);
                result.renderItemsStateHash = HashBytes(&meshRenderer.meshPartIndex, sizeof(meshRenderer.meshPartIndex), result.renderItemsStateHash);
                result.renderItemsStateHash = HashBytes(&meshRenderer.meshLod, sizeof(meshRenderer.meshLod), result.renderItemsStateHash);
                if (!meshRenderer.meshSource.empty())
                {
                    result.renderItemsStateHash = HashBytes(
                        meshRenderer.meshSource.data(),
                        meshRenderer.meshSource.size(),
                        result.renderItemsStateHash);
                }
                result.renderItemsStateHash = HashBytes(&meshRevision, sizeof(meshRevision), result.renderItemsStateHash);
            }
        }

        if (context.buildScenePrimitiveMesh && !result.indexOverflow && context.showGrid)
        {
            constexpr int kGridNearExtent = 24;
            constexpr int kGridMidExtent = 96;
            constexpr int kGridFarExtent = 240;
            constexpr float kGridSpacing = 1.0f;
            constexpr float kGridMinorHalfThickness = 0.008f;
            constexpr float kGridMajorHalfThickness = 0.016f;
            constexpr float kGridAxisHalfThickness = 0.024f;
            constexpr float kGridY = 0.001f;
            constexpr float kGridLayerOffset = 0.0020f;
            const float gridHalfSize = static_cast<float>(kGridFarExtent) * kGridSpacing;

            const auto appendGridQuad = [&](const Vec3& p0, const Vec3& p1, const Vec3& p2, const Vec3& p3, const std::array<float, 3>& color)
            {
                if (result.indexOverflow)
                {
                    return;
                }

                if (result.vertices.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max() - 4U))
                {
                    result.indexOverflow = true;
                    return;
                }

                const std::uint32_t indexBase = static_cast<std::uint32_t>(result.vertices.size());

                PrimitiveVertex vertex {};
                vertex.color = color;

                vertex.position = { p0.x, p0.y, p0.z };
                vertex.uv = { 0.0f, 0.0f };
                result.vertices.push_back(vertex);

                vertex.position = { p1.x, p1.y, p1.z };
                vertex.uv = { 1.0f, 0.0f };
                result.vertices.push_back(vertex);

                vertex.position = { p2.x, p2.y, p2.z };
                vertex.uv = { 1.0f, 1.0f };
                result.vertices.push_back(vertex);

                vertex.position = { p3.x, p3.y, p3.z };
                vertex.uv = { 0.0f, 1.0f };
                result.vertices.push_back(vertex);

                result.indices.push_back(indexBase + 0U);
                result.indices.push_back(indexBase + 1U);
                result.indices.push_back(indexBase + 2U);
                result.indices.push_back(indexBase + 0U);
                result.indices.push_back(indexBase + 2U);
                result.indices.push_back(indexBase + 3U);
            };

            const auto appendGridLineAlongX = [&](const float zCenter, const float halfThickness, const std::array<float, 3>& color)
            {
                const Vec3 p0 { -gridHalfSize, kGridY, zCenter - halfThickness };
                const Vec3 p1 { gridHalfSize, kGridY, zCenter - halfThickness };
                const Vec3 p2 { gridHalfSize, kGridY, zCenter + halfThickness };
                const Vec3 p3 { -gridHalfSize, kGridY, zCenter + halfThickness };
                appendGridQuad(p0, p1, p2, p3, color);
            };

            const auto appendGridLineAlongZ = [&](const float xCenter, const float halfThickness, const std::array<float, 3>& color)
            {
                const float zLayerY = kGridY + kGridLayerOffset;
                const Vec3 p0 { xCenter - halfThickness, zLayerY, -gridHalfSize };
                const Vec3 p1 { xCenter + halfThickness, zLayerY, -gridHalfSize };
                const Vec3 p2 { xCenter + halfThickness, zLayerY, gridHalfSize };
                const Vec3 p3 { xCenter - halfThickness, zLayerY, gridHalfSize };
                appendGridQuad(p0, p1, p2, p3, color);
            };

            for (int line = -kGridFarExtent; line <= kGridFarExtent; ++line)
            {
                const int absLine = std::abs(line);
                bool shouldDrawLine = false;
                if (absLine <= kGridNearExtent)
                {
                    shouldDrawLine = true;
                }
                else if (absLine <= kGridMidExtent)
                {
                    shouldDrawLine = (line % 5) == 0;
                }
                else
                {
                    shouldDrawLine = (line % 20) == 0;
                }
                if (!shouldDrawLine)
                {
                    continue;
                }

                const float offset = static_cast<float>(line) * kGridSpacing;
                const bool isAxisLine = line == 0;
                const bool isMajorLine = (line % 10) == 0;
                const float halfThickness =
                    isAxisLine ? kGridAxisHalfThickness : (isMajorLine ? kGridMajorHalfThickness : kGridMinorHalfThickness);

                const std::array<float, 3> nearMinorColor { 0.18f, 0.20f, 0.23f };
                const std::array<float, 3> midMinorColor { 0.14f, 0.16f, 0.19f };
                const std::array<float, 3> farMinorColor { 0.11f, 0.13f, 0.16f };
                const std::array<float, 3> majorColor { 0.26f, 0.29f, 0.33f };
                const std::array<float, 3> xAxisColor { 1.00f, 0.26f, 0.26f };
                const std::array<float, 3> zAxisColor { 0.30f, 0.68f, 1.00f };
                const std::array<float, 3>& minorColor =
                    absLine <= kGridNearExtent
                        ? nearMinorColor
                        : (absLine <= kGridMidExtent ? midMinorColor : farMinorColor);

                const std::array<float, 3> xLineColor = isAxisLine ? xAxisColor : (isMajorLine ? majorColor : minorColor);
                const std::array<float, 3> zLineColor = isAxisLine ? zAxisColor : (isMajorLine ? majorColor : minorColor);

                appendGridLineAlongX(offset, halfThickness, xLineColor);
                appendGridLineAlongZ(offset, halfThickness, zLineColor);

                if (result.indexOverflow)
                {
                    break;
                }
            }
        }

        return result;
    }
}
