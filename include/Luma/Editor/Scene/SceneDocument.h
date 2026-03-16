#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace Luma
{
    class Scene;
}

namespace Luma::Editor
{
    class SceneDocument final
    {
    public:
        struct Callbacks
        {
            std::function<void()> beforeSerialize;
            std::function<void()> afterLoad;
            std::function<void()> afterNewScene;
            std::function<void()> afterSave;
            std::function<void(const std::filesystem::path&)> onScenePathChanged;
        };

        const std::filesystem::path& GetCurrentScenePath() const;
        bool HasCurrentScenePath() const;
        bool IsDirtyFlag() const;
        float GetDirtyRefreshAccumulator() const;
        void AccumulateDirtyRefresh(float deltaTimeSeconds);
        std::string GetDisplayName() const;
        std::string BuildWindowTitle(std::string_view projectName) const;
        const std::string& GetLastWindowTitle() const;
        void SetLastWindowTitle(std::string title);

        bool LoadFromPath(Scene& scene, const std::filesystem::path& scenePath, std::string& ioStatus, const Callbacks& callbacks = {});
        bool SaveToPath(Scene& scene, const std::filesystem::path& scenePath, std::string& ioStatus, const Callbacks& callbacks = {});
        void CreateNew(Scene& scene, std::string& ioStatus, const Callbacks& callbacks = {});
        bool RestoreSnapshot(Scene& scene, std::string_view snapshot, std::string& ioStatus, const Callbacks& callbacks = {});

        bool CaptureSnapshot(const Scene& scene, std::string& outSnapshot, std::string& outError) const;
        void RefreshDirtyState(const Scene& scene);

        std::filesystem::path ResolveScenePath(
            const std::string& scenePath,
            bool projectLoaded,
            const std::filesystem::path& projectRoot) const;
        std::filesystem::path GetDefaultScenePath(
            bool projectLoaded,
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& scenesPath,
            const std::string& configuredStartScene) const;
        std::filesystem::path BuildScenePathFromName(
            std::string_view sceneName,
            bool projectLoaded,
            const std::filesystem::path& scenesPath) const;

    private:
        std::filesystem::path m_CurrentScenePath;
        std::string m_UnsavedSceneName = "Untitled";
        std::string m_LastWindowTitle;
        std::string m_LastSavedSceneSnapshot;
        bool m_IsSceneDirty = false;
        float m_SceneDirtyRefreshAccumulator = 0.0f;
    };
}
