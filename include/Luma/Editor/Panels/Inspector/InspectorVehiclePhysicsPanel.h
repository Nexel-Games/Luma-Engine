#pragma once

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct InspectorVehiclePhysicsPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
    };

    class InspectorVehiclePhysicsPanel
    {
    public:
        void Draw(const InspectorVehiclePhysicsPanelContext& context);
    };
}
