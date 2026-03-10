#pragma once

#include <filesystem>
#include <functional>

#include "Luma/Editor/Scene/SceneActionPrompt.h"

namespace Luma::Editor
{
    struct SceneActionExecutionCallbacks
    {
        std::function<void()> createNewScene;
        std::function<void(const std::filesystem::path&)> loadScene;
        std::function<bool()> saveActiveScene;
        std::function<void()> requestExit;
    };

    class SceneActionService
    {
    public:
        void RequestExit();
        void RequestNewScene(bool sceneDirty, const std::function<void()>& createNewScene);
        void RequestLoadScene(
            const std::filesystem::path& scenePath,
            bool sceneDirty,
            const std::function<void(const std::filesystem::path&)>& loadScene);
        void RequestReloadScene(
            bool hasCurrentScenePath,
            const std::filesystem::path& currentScenePath,
            bool sceneDirty,
            const std::function<void(const std::filesystem::path&)>& loadScene);
        void DrawPrompt(const SceneActionExecutionCallbacks& callbacks);

    private:
        void ExecutePendingAction(
            SceneActionPrompt::Action action,
            const std::filesystem::path& actionPath,
            const SceneActionExecutionCallbacks& callbacks) const;

        SceneActionPrompt m_Prompt;
    };
}
