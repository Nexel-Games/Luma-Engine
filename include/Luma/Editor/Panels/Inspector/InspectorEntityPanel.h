#pragma once

#include <functional>
#include <string_view>
#include <vector>

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct InspectorEntityPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
        std::string_view physicsBackendName;
        bool* physicsSimulationEnabled = nullptr;
        const std::vector<std::string>* availableTags = nullptr;
        const std::vector<std::string>* availableLayers = nullptr;
        std::function<void()> markSceneRenderCacheDirty;
        std::function<bool(EntityID)> createPrefabFromEntity;
        std::function<bool(EntityID)> applyPrefabInstance;
        std::function<bool(EntityID)> revertPrefabInstance;
        std::function<std::string(EntityID)> getPrefabInstanceStatus;
        std::function<std::vector<std::string>(EntityID)> getPrefabOverridePaths;
        std::function<bool(EntityID, std::string_view)> revertPrefabComponent;
        std::function<bool(EntityID, std::string_view)> revertPrefabOverridePath;
        std::function<void(EntityID)> selectPrefabAsset;
    };

    class InspectorEntityPanel
    {
    public:
        void Draw(const InspectorEntityPanelContext& context);
    };
}
