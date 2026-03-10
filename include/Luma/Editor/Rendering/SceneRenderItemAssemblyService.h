#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Luma/Core/App/RenderPipeline.h"
#include "Luma/Editor/Rendering/SceneRenderCacheBuilder.h"

namespace Luma::Editor
{
    struct SceneRenderItemAssemblyContext
    {
        const std::vector<PendingSceneRenderSource>* pendingRenderSources = nullptr;
        std::uint64_t renderItemsStateHash = 0;
        std::function<MaterialRenderProxy(const Assets::MeshMaterialInfo&, const std::filesystem::path&)>
            buildImportedMaterialProxy;
        std::function<bool(const MeshRendererComponent&, std::size_t, MaterialRenderProxy&)>
            tryBuildSlotMaterialOverride;
        std::function<bool(EntityID, const Assets::MeshMaterialInfo*, MaterialRenderProxy&)>
            tryBuildEntityMaterialOverride;
    };

    struct SceneRenderItemAssemblyScratch
    {
        std::unordered_map<std::string, MeshDesc> sharedMeshPayloads;
        std::unordered_set<std::string> emittedMeshPayloads;
    };

    class SceneRenderItemAssemblyService
    {
    public:
        void BuildInto(
            const SceneRenderItemAssemblyContext& context,
            SceneRenderItemAssemblyScratch& scratch,
            std::vector<SceneRenderItem>& outRenderItems) const;
    };
}
