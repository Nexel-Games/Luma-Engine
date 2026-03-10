#pragma once

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct InspectorAdvancedPhysicsPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
    };

    class InspectorAdvancedPhysicsPanel
    {
    public:
        void Draw(const InspectorAdvancedPhysicsPanelContext& context);
    };
}
