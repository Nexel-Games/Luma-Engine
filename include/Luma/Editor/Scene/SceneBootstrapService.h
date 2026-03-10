#pragma once

#include <functional>

#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SkyLightComponent.h"

namespace Luma::Editor
{
    struct SceneBootstrapContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
        std::function<bool(EntityID)> isEntitySelected;
        std::function<void()> clearEntitySelection;
        std::function<void(EntityID)> selectSingleEntity;
        std::function<void(SkyLightComponent&)> initializeSkyLightDefaults;
        std::function<void()> resetViewportCamera;
    };

    class SceneBootstrapService
    {
    public:
        bool IsSelectionValid(const SceneBootstrapContext& context) const;
        void SeedDefaultSceneEntities(SceneBootstrapContext& context) const;
    };
}
