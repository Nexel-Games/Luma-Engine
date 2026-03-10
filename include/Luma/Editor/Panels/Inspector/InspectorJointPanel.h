#pragma once

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct InspectorJointPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
    };

    class InspectorJointPanel
    {
    public:
        void Draw(const InspectorJointPanelContext& context);
    };
}
