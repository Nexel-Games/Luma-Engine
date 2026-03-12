#pragma once

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
    };

    class InspectorEntityPanel
    {
    public:
        void Draw(const InspectorEntityPanelContext& context);
    };
}
