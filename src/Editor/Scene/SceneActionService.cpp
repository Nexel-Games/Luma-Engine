#include "Luma/Editor/Scene/SceneActionService.h"

namespace Luma::Editor
{
    void SceneActionService::RequestExit()
    {
        m_Prompt.RequestExit();
    }

    void SceneActionService::RequestNewScene(const bool sceneDirty, const std::function<void()>& createNewScene)
    {
        if (sceneDirty)
        {
            m_Prompt.RequestNewScene();
            return;
        }

        if (createNewScene)
        {
            createNewScene();
        }
    }

    void SceneActionService::RequestLoadScene(
        const std::filesystem::path& scenePath,
        const bool sceneDirty,
        const std::function<void(const std::filesystem::path&)>& loadScene)
    {
        if (scenePath.empty())
        {
            return;
        }

        if (sceneDirty)
        {
            m_Prompt.RequestLoadScene(scenePath);
            return;
        }

        if (loadScene)
        {
            loadScene(scenePath);
        }
    }

    void SceneActionService::RequestReloadScene(
        const bool hasCurrentScenePath,
        const std::filesystem::path& currentScenePath,
        const bool sceneDirty,
        const std::function<void(const std::filesystem::path&)>& loadScene)
    {
        if (!hasCurrentScenePath)
        {
            return;
        }

        if (sceneDirty)
        {
            m_Prompt.RequestReloadScene(currentScenePath);
            return;
        }

        if (loadScene)
        {
            loadScene(currentScenePath);
        }
    }

    void SceneActionService::DrawPrompt(const SceneActionExecutionCallbacks& callbacks)
    {
        m_Prompt.DrawModal({
            [this, &callbacks](const SceneActionPrompt::Action action, const std::filesystem::path& scenePath) -> bool
            {
                if (callbacks.saveActiveScene && !callbacks.saveActiveScene())
                {
                    return false;
                }

                ExecutePendingAction(action, scenePath, callbacks);
                return true;
            },
            [this, &callbacks](const SceneActionPrompt::Action action, const std::filesystem::path& scenePath)
            {
                ExecutePendingAction(action, scenePath, callbacks);
            },
            []() {}
        });
    }

    void SceneActionService::ExecutePendingAction(
        const SceneActionPrompt::Action action,
        const std::filesystem::path& actionPath,
        const SceneActionExecutionCallbacks& callbacks) const
    {
        switch (action)
        {
        case SceneActionPrompt::Action::NewScene:
            if (callbacks.createNewScene)
            {
                callbacks.createNewScene();
            }
            break;
        case SceneActionPrompt::Action::LoadScene:
        case SceneActionPrompt::Action::ReloadScene:
            if (callbacks.loadScene)
            {
                callbacks.loadScene(actionPath);
            }
            break;
        case SceneActionPrompt::Action::ExitApplication:
            if (callbacks.requestExit)
            {
                callbacks.requestExit();
            }
            break;
        case SceneActionPrompt::Action::None:
        default:
            break;
        }
    }
}
