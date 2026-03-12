#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace Luma
{
    class Scene;

    namespace Editor
    {
        class SceneDocument;

        struct SceneDocumentHostContext
        {
            Scene* scene = nullptr;
            SceneDocument* sceneDocument = nullptr;
            std::string* contentStatus = nullptr;
            std::filesystem::path* selectedContentEntry = nullptr;
            std::function<void()> afterLoad;
            std::function<void()> beforeSave;
            std::function<void()> afterSave;
            std::function<void()> afterNewScene;
        };

        class SceneDocumentHostService
        {
        public:
            bool LoadSceneFromPath(SceneDocumentHostContext& context, const std::filesystem::path& scenePath) const;
            bool SaveSceneToPath(SceneDocumentHostContext& context, const std::filesystem::path& scenePath) const;
            bool CaptureSceneSnapshot(
                const SceneDocumentHostContext& context,
                std::string& outSnapshot,
                std::string& outError) const;
            bool RestoreSceneSnapshot(
                SceneDocumentHostContext& context,
                std::string_view snapshot) const;
            bool IsSceneDirty(SceneDocumentHostContext& context) const;
            void UpdateSceneDirtyState(SceneDocumentHostContext& context) const;
            void CreateNewScene(SceneDocumentHostContext& context) const;
            void RefreshWindowTitle(const SceneDocumentHostContext& context) const;

        private:
            void UpdateSelectedContentEntry(
                const SceneDocumentHostContext& context,
                const std::filesystem::path& activeScenePath) const;
        };
    }
}
