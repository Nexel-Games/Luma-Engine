#pragma once

#include <filesystem>
#include <functional>

namespace Luma::Editor
{
    class SceneActionPrompt final
    {
    public:
        enum class Action
        {
            None = 0,
            NewScene,
            LoadScene,
            ReloadScene,
            ExitApplication
        };

        struct Callbacks
        {
            std::function<bool(Action, const std::filesystem::path&)> onSave;
            std::function<void(Action, const std::filesystem::path&)> onDiscard;
            std::function<void()> onCancel;
        };

        void RequestExit();
        void RequestNewScene();
        void RequestLoadScene(const std::filesystem::path& scenePath);
        void RequestReloadScene(const std::filesystem::path& scenePath);

        bool HasPendingAction() const;
        Action GetPendingAction() const;
        const std::filesystem::path& GetPendingScenePath() const;

        void Clear();
        void DrawModal(const Callbacks& callbacks);

    private:
        void Request(Action action, std::filesystem::path scenePath);

        Action m_PendingAction = Action::None;
        std::filesystem::path m_PendingScenePath;
        bool m_OpenPrompt = false;
    };
}
