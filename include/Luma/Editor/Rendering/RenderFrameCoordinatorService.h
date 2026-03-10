#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Luma/Core/App/RenderPipeline.h"
#include "Luma/Editor/Rendering/SceneRenderCacheDirtyFlags.h"
#include "Luma/Editor/Rendering/SceneViewBuilder.h"

namespace Luma
{
    class IRenderBackend;
    class GPUResourceManager;

    namespace Assets
    {
        class ResourceStreamingService;
    }

    namespace Editor
    {
        class EditorViewportController;

        struct RenderFrameCoordinatorContext
        {
            IRenderBackend* renderer = nullptr;
            IRenderBackend** lastRenderer = nullptr;
            GPUResourceManager* gpuResourceManager = nullptr;
            std::unique_ptr<IRenderPipeline>* activePipeline = nullptr;
            RenderPipelineProfile* activeProfile = nullptr;
            RenderPipelineProfile desiredProfile = RenderPipelineProfile::CoreLite;
            Assets::ResourceStreamingService* streamingService = nullptr;
            const Scene* scene = nullptr;
            EditorViewportController* viewportController = nullptr;
            EntityID selectedEntity = entt::null;
            float timeSeconds = 0.0f;
            const MeshDesc* skyMesh = nullptr;
            std::uint64_t skyMeshRevision = 0;
            bool hasSkyMesh = false;
            const MeshDesc* gridMesh = nullptr;
            std::uint64_t gridMeshRevision = 0;
            bool hasGridMesh = false;
            const std::vector<SceneRenderItem>* renderItems = nullptr;
            std::uint64_t renderItemsRevision = 0;
            std::array<float, 3> skyAverageColor { 0.0f, 0.0f, 0.0f };
            SceneRenderCacheDirtyFlags renderSceneCacheDirtyFlags = SceneRenderCacheDirtyFlags::None;
            bool lastViewportGridEnabled = false;
            bool hasScenePrimitiveMesh = false;
            bool hasSkyPrimitiveMesh = false;
            const std::string* lastSkyMeshSignature = nullptr;
            const std::filesystem::path* skyboxSourcePath = nullptr;
            float* lastSceneRebuildMs = nullptr;
            float* lastSceneViewBuildMs = nullptr;
            float* lastRenderFrameMs = nullptr;
            std::function<void()> onRendererChanged;
            std::function<void()> syncSkyEnvironmentResources;
            std::function<EntityID()> findPrimarySkyEntity;
            std::function<std::string(EntityID)> buildSkyMeshSignature;
            std::function<void()> rebuildScenePrimitiveMesh;
            std::function<std::filesystem::path(const std::string&)> resolveSkyAssetPath;
            std::function<void(EntityID)> setLensSourceEntity;
            std::function<void(EntityID)> clearSkyRebuildRequested;
            std::function<void(const std::array<float, 3>&, ScenePostProcessView&)> buildBlendedPostProcessView;
        };

        class RenderFrameCoordinatorService
        {
        public:
            bool Render(RenderFrameCoordinatorContext& context) const;

        private:
            void UpdateRendererBinding(RenderFrameCoordinatorContext& context) const;
            void ResolveSceneOutputSize(
                const RenderFrameCoordinatorContext& context,
                std::uint32_t& outWidth,
                std::uint32_t& outHeight) const;
            bool EnsureActivePipeline(RenderFrameCoordinatorContext& context) const;
            void RebuildSceneRenderCacheIfNeeded(RenderFrameCoordinatorContext& context) const;
            SceneViewBuildInput BuildSceneViewInput(
                const RenderFrameCoordinatorContext& context,
                std::uint32_t outputWidth,
                std::uint32_t outputHeight) const;
        };
    }
}
