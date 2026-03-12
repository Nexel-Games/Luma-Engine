#pragma once

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct InspectorDestructionPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
    };

    class InspectorDestructionPanel
    {
    public:
        void Draw(const InspectorDestructionPanelContext& context);
    };
}
