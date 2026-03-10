#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace Luma::Editor
{
    class SceneActionService;
    class SceneDocument;
    class SceneFileService;

    struct SceneActionHostContext
    {
        SceneActionService* actionService = nullptr;
        SceneFileService* sceneFileService = nullptr;
        SceneDocument* sceneDocument = nullptr;
        std::string* contentStatus = nullptr;
        std::function<bool()> isSceneDirty;
        std::function<void()> createNewScene;
        std::function<void(const std::filesystem::path&)> loadScene;
        std::function<bool(const std::filesystem::path&)> saveSceneToPath;
        std::function<void()> requestExit;
    };

    class SceneActionHostService
    {
    public:
        void RequestPendingClose(SceneActionHostContext& context) const;
        void RequestNewScene(SceneActionHostContext& context) const;
        void RequestLoadScene(SceneActionHostContext& context, const std::filesystem::path& scenePath) const;
        void RequestReloadScene(SceneActionHostContext& context) const;
        bool SaveActiveScene(SceneActionHostContext& context) const;
        bool OpenSaveSceneAsPrompt(SceneActionHostContext& context, std::string_view suggestedName) const;
        void DrawUnsavedScenePrompt(SceneActionHostContext& context) const;

    private:
        bool IsSceneDirty(SceneActionHostContext& context) const;
    };
}
