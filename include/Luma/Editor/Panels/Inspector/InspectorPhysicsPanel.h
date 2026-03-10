#pragma once

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct InspectorPhysicsPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
    };

    class InspectorPhysicsPanel
    {
    public:
        void Draw(const InspectorPhysicsPanelContext& context);
    };
}
