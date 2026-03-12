#pragma once

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct InspectorCameraLightingPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
        std::function<void(std::string)> setContentStatus;
    };

    class InspectorCameraLightingPanel
    {
    public:
        void Draw(const InspectorCameraLightingPanelContext& context);
    };
}
