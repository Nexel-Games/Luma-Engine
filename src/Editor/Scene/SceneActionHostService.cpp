#include "Luma/Editor/Scene/SceneActionHostService.h"

#include "Luma/Core/App/Application.h"
#include "Luma/Editor/Scene/SceneActionService.h"
#include "Luma/Editor/Scene/SceneDocument.h"
#include "Luma/Editor/Scene/SceneFileService.h"

namespace Luma::Editor
{
    void SceneActionHostService::RequestPendingClose(SceneActionHostContext& context) const
    {
        if (context.actionService != nullptr)
        {
            context.actionService->RequestExit();
        }
    }

    void SceneActionHostService::RequestNewScene(SceneActionHostContext& context) const
    {
        if (context.actionService == nullptr)
        {
            return;
        }

        context.actionService->RequestNewScene(
            IsSceneDirty(context),
            [context]()
            {
                if (context.createNewScene)
                {
                    context.createNewScene();
                }
            });
    }

    void SceneActionHostService::RequestLoadScene(
        SceneActionHostContext& context,
        const std::filesystem::path& scenePath) const
    {
        if (context.actionService == nullptr)
        {
            return;
        }

        context.actionService->RequestLoadScene(
            scenePath,
            IsSceneDirty(context),
            [context](const std::filesystem::path& requestedScenePath)
            {
                if (context.loadScene)
                {
                    context.loadScene(requestedScenePath);
                }
            });
    }

    void SceneActionHostService::RequestReloadScene(SceneActionHostContext& context) const
    {
        if (context.actionService == nullptr || context.sceneDocument == nullptr)
        {
            return;
        }

        context.actionService->RequestReloadScene(
            context.sceneDocument->HasCurrentScenePath(),
            context.sceneDocument->GetCurrentScenePath(),
            IsSceneDirty(context),
            [context](const std::filesystem::path& requestedScenePath)
            {
                if (context.loadScene)
                {
                    context.loadScene(requestedScenePath);
                }
            });
    }

    bool SceneActionHostService::SaveActiveScene(SceneActionHostContext& context) const
    {
        if (context.sceneDocument == nullptr)
        {
            return false;
        }

        const std::filesystem::path savePath = context.sceneDocument->GetCurrentScenePath();
        if (savePath.empty())
        {
            return OpenSaveSceneAsPrompt(context, {});
        }

        return context.saveSceneToPath ? context.saveSceneToPath(savePath) : false;
    }

    bool SceneActionHostService::OpenSaveSceneAsPrompt(
        SceneActionHostContext& context,
        const std::string_view suggestedName) const
    {
        if (context.sceneFileService == nullptr ||
            context.sceneDocument == nullptr ||
            context.contentStatus == nullptr)
        {
            return false;
        }

        return context.sceneFileService->OpenSaveAsPrompt(
            *context.sceneDocument,
            *context.contentStatus,
            [context](const std::filesystem::path& scenePath)
            {
                return context.saveSceneToPath ? context.saveSceneToPath(scenePath) : false;
            },
            suggestedName);
    }

    void SceneActionHostService::DrawUnsavedScenePrompt(SceneActionHostContext& context) const
    {
        if (context.actionService == nullptr)
        {
            return;
        }

        context.actionService->DrawPrompt({
            [context]()
            {
                if (context.createNewScene)
                {
                    context.createNewScene();
                }
            },
            [context](const std::filesystem::path& scenePath)
            {
                if (context.loadScene)
                {
                    context.loadScene(scenePath);
                }
            },
            [this, &context]() -> bool
            {
                return SaveActiveScene(context);
            },
            [context]()
            {
                if (context.requestExit)
                {
                    context.requestExit();
                    return;
                }

                if (Application* app = Application::Get(); app != nullptr)
                {
                    app->RequestExit();
                }
            }
        });
    }

    bool SceneActionHostService::IsSceneDirty(SceneActionHostContext& context) const
    {
        return context.isSceneDirty ? context.isSceneDirty() : false;
    }
}
