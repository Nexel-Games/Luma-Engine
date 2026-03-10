#pragma once

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct InspectorFieldBuoyancyPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
    };

    class InspectorFieldBuoyancyPanel
    {
    public:
        void Draw(const InspectorFieldBuoyancyPanelContext& context);
    };
}
