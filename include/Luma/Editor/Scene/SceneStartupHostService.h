#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace Luma
{
    class Scene;

    namespace Editor
    {
        class SceneDocument;

        struct SceneStartupHostContext
        {
            Scene* scene = nullptr;
            SceneDocument* sceneDocument = nullptr;
            std::string* contentStatus = nullptr;
            std::filesystem::path* selectedContentEntry = nullptr;
            bool projectLoaded = false;
            std::function<bool(const std::filesystem::path&)> loadSceneFromPath;
            std::function<void()> seedDefaultSceneEntities;
            std::function<void()> refreshWindowTitle;
            std::function<void()> updateWorldTransforms;
        };

        class SceneStartupHostService
        {
        public:
            void Bootstrap(SceneStartupHostContext& context, const std::filesystem::path& startupScenePath) const;
        };
    }
}
