#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "Luma/Scene/Scene.h"

namespace Luma
{
    class LuaScriptRuntime;
}

namespace Luma::Editor
{
    struct InspectorScriptPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
        const std::filesystem::path* selectedContentEntry = nullptr;
        ::Luma::LuaScriptRuntime* luaScriptRuntime = nullptr;
        bool playModeActive = false;
        std::function<void(std::string)> setContentStatus;
    };

    class InspectorScriptPanel
    {
    public:
        void Draw(const InspectorScriptPanelContext& context);
    };
}
