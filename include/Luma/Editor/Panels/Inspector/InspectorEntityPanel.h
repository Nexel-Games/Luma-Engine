#pragma once

#include <string_view>

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct InspectorEntityPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
        std::string_view physicsBackendName;
        bool* physicsSimulationEnabled = nullptr;
    };

    class InspectorEntityPanel
    {
    public:
        void Draw(const InspectorEntityPanelContext& context);
    };
}
