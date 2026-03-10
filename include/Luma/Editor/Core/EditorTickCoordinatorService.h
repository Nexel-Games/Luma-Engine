#pragma once

#include <functional>

namespace Luma
{
    class IRenderBackend;
    class PhysicsSystem;
    class Scene;

    namespace Assets
    {
        class ResourceStreamingService;
    }

    namespace Editor
    {
        class ContentBrowserCache;
        class SceneDocument;

        struct EditorTickCoordinatorContext
        {
            float deltaTimeSeconds = 0.0f;
            float* timeSeconds = nullptr;
            float* lastDeltaTimeSeconds = nullptr;
            IRenderBackend* renderer = nullptr;
            bool showContentBrowserPanel = false;
            ContentBrowserCache* contentBrowserCache = nullptr;
            float* lastContentBrowserTickMs = nullptr;
            Assets::ResourceStreamingService* resourceStreamingService = nullptr;
            float* lastStreamingTickMs = nullptr;
            PhysicsSystem* physicsSystem = nullptr;
            bool physicsSimulationEnabled = true;
            Scene* scene = nullptr;
            SceneDocument* sceneDocument = nullptr;
            float sceneDirtyRefreshThreshold = 0.35f;
            std::function<void()> pruneEntitySelection;
            std::function<void()> tickPackageManager;
            std::function<void()> pumpContentFolderTreeRebuild;
            std::function<void()> pumpContentEntriesRefresh;
            std::function<void()> tickContentImportQueue;
            std::function<void()> updateStreamingTaskState;
            std::function<void(float)> updateConsoleTasks;
            std::function<void()> markSceneRenderCacheDirty;
            std::function<void()> updateSceneDirtyState;
        };

        class EditorTickCoordinatorService
        {
        public:
            void Tick(EditorTickCoordinatorContext& context) const;
        };
    }
}
