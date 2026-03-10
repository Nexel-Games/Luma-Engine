#include "Luma/Editor/Scene/SceneStartupHostService.h"

#include <system_error>

#include "Luma/Editor/Scene/SceneDocument.h"
#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    void SceneStartupHostService::Bootstrap(
        SceneStartupHostContext& context,
        const std::filesystem::path& startupScenePath) const
    {
        bool loadedStartupScene = false;
        if (!startupScenePath.empty() && context.loadSceneFromPath)
        {
            std::error_code ec;
            if (std::filesystem::exists(startupScenePath, ec) && std::filesystem::is_regular_file(startupScenePath, ec))
            {
                loadedStartupScene = context.loadSceneFromPath(startupScenePath);
            }
        }

        if (!loadedStartupScene && context.scene != nullptr)
        {
            context.scene->Clear();
            if (context.seedDefaultSceneEntities)
            {
                context.seedDefaultSceneEntities();
            }

            if (!startupScenePath.empty() &&
                context.sceneDocument != nullptr &&
                context.contentStatus != nullptr)
            {
                const bool savedDefaultScene = context.sceneDocument->SaveToPath(
                    *context.scene,
                    startupScenePath,
                    *context.contentStatus,
                    SceneDocument::Callbacks {
                        [&context]()
                        {
                            if (context.updateWorldTransforms)
                            {
                                context.updateWorldTransforms();
                            }
                        },
                        {},
                        {},
                        {},
                        [&context](const std::filesystem::path& activeScenePath)
                        {
                            if (context.selectedContentEntry != nullptr)
                            {
                                *context.selectedContentEntry = activeScenePath;
                            }
                        } });
                if (savedDefaultScene && context.projectLoaded)
                {
                    *context.contentStatus = "Created default scene: " + startupScenePath.filename().string();
                }
            }
        }

        if (context.refreshWindowTitle)
        {
            context.refreshWindowTitle();
        }
    }
}
