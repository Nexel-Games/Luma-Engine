#include "Luma/Editor/Scene/SceneDocumentHostService.h"

#include <string_view>

#include <GLFW/glfw3.h>

#include "Luma/Core/App/Application.h"
#include "Luma/Core/App/Project.h"
#include "Luma/Editor/Scene/SceneDocument.h"
#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    bool SceneDocumentHostService::LoadSceneFromPath(
        SceneDocumentHostContext& context,
        const std::filesystem::path& scenePath) const
    {
        if (context.scene == nullptr || context.sceneDocument == nullptr || context.contentStatus == nullptr)
        {
            return false;
        }

        const SceneDocument::Callbacks callbacks {
            {},
            [&context]()
            {
                if (context.afterLoad)
                {
                    context.afterLoad();
                }
            },
            {},
            {},
            [this, &context](const std::filesystem::path& activeScenePath)
            {
                UpdateSelectedContentEntry(context, activeScenePath);
            }
        };

        const bool loaded =
            context.sceneDocument->LoadFromPath(*context.scene, scenePath, *context.contentStatus, callbacks);
        if (loaded)
        {
            RefreshWindowTitle(context);
        }
        return loaded;
    }

    bool SceneDocumentHostService::SaveSceneToPath(
        SceneDocumentHostContext& context,
        const std::filesystem::path& scenePath) const
    {
        if (context.scene == nullptr || context.sceneDocument == nullptr || context.contentStatus == nullptr)
        {
            return false;
        }

        const SceneDocument::Callbacks callbacks {
            [&context]()
            {
                if (context.beforeSave)
                {
                    context.beforeSave();
                }
            },
            {},
            {},
            [&context]()
            {
                if (context.afterSave)
                {
                    context.afterSave();
                }
            },
            [this, &context](const std::filesystem::path& activeScenePath)
            {
                UpdateSelectedContentEntry(context, activeScenePath);
            }
        };

        const bool saved =
            context.sceneDocument->SaveToPath(*context.scene, scenePath, *context.contentStatus, callbacks);
        if (saved)
        {
            RefreshWindowTitle(context);
        }
        return saved;
    }

    bool SceneDocumentHostService::CaptureSceneSnapshot(
        const SceneDocumentHostContext& context,
        std::string& outSnapshot,
        std::string& outError) const
    {
        if (context.scene == nullptr || context.sceneDocument == nullptr)
        {
            outError = "Scene document context is incomplete.";
            return false;
        }

        return context.sceneDocument->CaptureSnapshot(*context.scene, outSnapshot, outError);
    }

    bool SceneDocumentHostService::IsSceneDirty(SceneDocumentHostContext& context) const
    {
        UpdateSceneDirtyState(context);
        return context.sceneDocument != nullptr && context.sceneDocument->IsDirtyFlag();
    }

    void SceneDocumentHostService::UpdateSceneDirtyState(SceneDocumentHostContext& context) const
    {
        if (context.scene == nullptr || context.sceneDocument == nullptr)
        {
            return;
        }

        const bool previousDirty = context.sceneDocument->IsDirtyFlag();
        context.sceneDocument->RefreshDirtyState(*context.scene);
        if (previousDirty != context.sceneDocument->IsDirtyFlag())
        {
            RefreshWindowTitle(context);
        }
    }

    void SceneDocumentHostService::CreateNewScene(SceneDocumentHostContext& context) const
    {
        if (context.scene == nullptr || context.sceneDocument == nullptr || context.contentStatus == nullptr)
        {
            return;
        }

        const SceneDocument::Callbacks callbacks {
            {},
            {},
            [&context]()
            {
                if (context.afterNewScene)
                {
                    context.afterNewScene();
                }
            },
            {},
            [this, &context](const std::filesystem::path& activeScenePath)
            {
                UpdateSelectedContentEntry(context, activeScenePath);
            }
        };

        context.sceneDocument->CreateNew(*context.scene, *context.contentStatus, callbacks);
        RefreshWindowTitle(context);
    }

    void SceneDocumentHostService::RefreshWindowTitle(const SceneDocumentHostContext& context) const
    {
        if (context.sceneDocument == nullptr)
        {
            return;
        }

        GLFWwindow* window = nullptr;
        if (Application* app = Application::Get(); app != nullptr)
        {
            window = app->GetWindowHandle();
        }
        if (window == nullptr)
        {
            return;
        }

        const std::string title = context.sceneDocument->BuildWindowTitle(
            Project::IsLoaded() ? std::string_view(Project::GetConfig().name) : std::string_view {});
        if (title == context.sceneDocument->GetLastWindowTitle())
        {
            return;
        }

        glfwSetWindowTitle(window, title.c_str());
        context.sceneDocument->SetLastWindowTitle(title);
    }

    void SceneDocumentHostService::UpdateSelectedContentEntry(
        const SceneDocumentHostContext& context,
        const std::filesystem::path& activeScenePath) const
    {
        if (context.selectedContentEntry != nullptr)
        {
            *context.selectedContentEntry = activeScenePath;
        }
    }
}
