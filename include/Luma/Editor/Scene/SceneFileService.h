#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace Luma::Editor
{
    class SceneDocument;

    class SceneFileService
    {
    public:
        bool OpenSaveAsPrompt(
            const SceneDocument& sceneDocument,
            std::string& contentStatus,
            const std::function<bool(const std::filesystem::path&)>& saveSceneToPath,
            std::string_view suggestedName) const;
        bool SetProjectStartScene(
            const std::filesystem::path& scenePath,
            std::string& projectConfigStatus,
            const std::function<void()>& invalidateProjectSettingsDraft) const;

        std::filesystem::path ResolveScenePath(const SceneDocument& sceneDocument, const std::string& scenePath) const;
        std::filesystem::path GetDefaultScenePath(const SceneDocument& sceneDocument) const;
        std::filesystem::path BuildScenePathFromName(const SceneDocument& sceneDocument, std::string_view sceneName) const;
        std::string GetActiveSceneDisplayName(const SceneDocument& sceneDocument) const;
    };
}
