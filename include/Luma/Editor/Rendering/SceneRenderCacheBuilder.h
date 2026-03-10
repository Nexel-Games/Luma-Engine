#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Luma/Asset/Core/MeshAssetIO.h"
#include "Luma/Editor/Assets/MeshStreamingGeometryService.h"
#include "Luma/Editor/Rendering/SceneRenderCacheDirtyFlags.h"
#include "Luma/Renderer/PrimitiveMeshFactory.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SkyLightComponent.h"
#include "Luma/Scene/TransformComponent.h"

namespace Luma::Editor
{
    struct PendingSceneRenderSource
    {
        EntityID entity = entt::null;
        std::string renderKey;
        const TransformComponent* transform = nullptr;
        const MeshRendererComponent* meshRenderer = nullptr;
        const PrimitiveMeshData* geometry = nullptr;
        const Assets::MeshMaterialInfo* sourceMaterial = nullptr;
        std::filesystem::path sourceMaterialPath;
        std::size_t materialSlotIndex = 0;
        std::string meshKey;
        std::uint64_t meshRevision = 0;
    };

    struct SceneRenderCacheBuildContext
    {
        Scene* scene = nullptr;
        bool showGrid = false;
        const std::vector<float>* skyEnvironmentLinearPixels = nullptr;
        int skyEnvironmentWidth = 0;
        int skyEnvironmentHeight = 0;
        const std::unordered_map<std::string, MeshStreamingAssetState>* streamedMeshAssets = nullptr;
        bool collectRenderSources = true;
        bool buildScenePrimitiveMesh = true;
        bool buildSkyPrimitiveMesh = true;
        std::function<EntityID()> findPrimarySkyEntity;
        std::function<MeshStreamingImportedScenePartsState*(const std::string&)> resolveImportedSceneParts;
        std::function<const PrimitiveMeshData*(const TransformComponent&, const MeshRendererComponent&)>
            resolveMeshRendererGeometry;
        std::function<std::uint32_t(const TransformComponent&, const MeshRendererComponent&)> computeRequestedMeshLod;
        std::function<std::array<float, 3>(const std::array<float, 3>&, const SkyLightComponent&, bool)> computeSkyColor;
    };

    struct SceneRenderCacheBuildResult
    {
        std::vector<PrimitiveVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::vector<PrimitiveVertex> skyVertices;
        std::vector<std::uint32_t> skyIndices;
        std::vector<PendingSceneRenderSource> pendingRenderSources;
        std::uint64_t renderItemsStateHash = 1469598103934665603ull;
        bool indexOverflow = false;
        bool skyIndexOverflow = false;
    };

    class SceneRenderCacheBuilder
    {
    public:
        SceneRenderCacheBuildResult Build(const SceneRenderCacheBuildContext& context) const;
    };
}
