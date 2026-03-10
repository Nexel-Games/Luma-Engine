#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace Luma::Editor
{
    struct PluginsPanelContext
    {
        std::function<void()> invalidateProjectSettingsDraft;
        std::function<void(std::string)> setProjectConfigStatus;
        std::function<std::string_view()> getProjectConfigStatus;
    };

    class PluginsPanel
    {
    public:
        void Draw(bool* open, const PluginsPanelContext& context);
    };
}
