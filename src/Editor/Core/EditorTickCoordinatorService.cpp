#include "Luma/Editor/Core/EditorTickCoordinatorService.h"

#include <chrono>

#include "Luma/Asset/Streaming/ResourceStreamingService.h"
#include "Luma/Editor/Content/ContentBrowserCache.h"
#include "Luma/Editor/Scene/SceneDocument.h"
#include "Luma/Physics/PhysicsSystem.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/TransformComponent.h"

namespace Luma::Editor
{
    void EditorTickCoordinatorService::Tick(EditorTickCoordinatorContext& context) const
    {
        if (context.pruneEntitySelection)
        {
            context.pruneEntitySelection();
        }
        if (context.tickPackageManager)
        {
            context.tickPackageManager();
        }
        if (context.pumpContentFolderTreeRebuild)
        {
            context.pumpContentFolderTreeRebuild();
        }
        if (context.pumpContentEntriesRefresh)
        {
            context.pumpContentEntriesRefresh();
        }
        if (context.tickContentImportQueue)
        {
            context.tickContentImportQueue();
        }

        if (context.lastContentBrowserTickMs != nullptr)
        {
            if (context.renderer != nullptr &&
                context.showContentBrowserPanel &&
                context.contentBrowserCache != nullptr)
            {
                const auto contentBrowserTickStart = std::chrono::steady_clock::now();
                context.contentBrowserCache->Tick(*context.renderer);
                *context.lastContentBrowserTickMs = std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - contentBrowserTickStart)
                                                        .count();
            }
            else
            {
                *context.lastContentBrowserTickMs = 0.0f;
            }
        }

        if (context.lastStreamingTickMs != nullptr && context.resourceStreamingService != nullptr)
        {
            const auto streamingTickStart = std::chrono::steady_clock::now();
            context.resourceStreamingService->Tick();
            *context.lastStreamingTickMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - streamingTickStart)
                                                .count();
        }
        if (context.updateStreamingTaskState)
        {
            context.updateStreamingTaskState();
        }

        if (context.timeSeconds != nullptr)
        {
            *context.timeSeconds += context.deltaTimeSeconds;
        }
        if (context.lastDeltaTimeSeconds != nullptr)
        {
            *context.lastDeltaTimeSeconds = context.deltaTimeSeconds;
        }
        if (context.updateConsoleTasks)
        {
            context.updateConsoleTasks(context.deltaTimeSeconds);
        }

        if (context.physicsSystem != nullptr && context.scene != nullptr)
        {
            context.physicsSystem->SetEnabled(context.physicsSimulationEnabled);
            context.physicsSystem->Simulate(*context.scene, context.deltaTimeSeconds);
        }

        bool anyTransformDirty = false;
        if (context.scene != nullptr)
        {
            const auto transformView = context.scene->GetRegistry().view<TransformComponent>();
            for (const EntityID entity : transformView)
            {
                if (transformView.get<TransformComponent>(entity).dirty)
                {
                    anyTransformDirty = true;
                    break;
                }
            }

            if (anyTransformDirty && context.markSceneRenderCacheDirty)
            {
                context.markSceneRenderCacheDirty();
            }

            context.scene->UpdateWorldTransforms();
        }

        if (context.sceneDocument != nullptr)
        {
            context.sceneDocument->AccumulateDirtyRefresh(context.deltaTimeSeconds);
            if (context.sceneDocument->GetDirtyRefreshAccumulator() >= context.sceneDirtyRefreshThreshold &&
                context.updateSceneDirtyState)
            {
                context.updateSceneDirtyState();
            }
        }
    }
}
