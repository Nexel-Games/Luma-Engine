#pragma once

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct InspectorPhysicsEventsPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
    };

    class InspectorPhysicsEventsPanel
    {
    public:
        void Draw(const InspectorPhysicsEventsPanelContext& context);
    };
}
