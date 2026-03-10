#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/App/Project.h"

namespace Luma::Editor
{
    enum class BuiltInPluginActivationPolicy : std::uint8_t
    {
        Immediate = 0,
        RestartRequired
    };

    struct BuiltInPluginDescriptor
    {
        std::string id;
        std::string displayName;
        std::string description;
        std::string panelName;
        bool enabledByDefault = false;
        bool editorOnly = true;
        BuiltInPluginActivationPolicy activationPolicy = BuiltInPluginActivationPolicy::Immediate;
    };

    const std::vector<BuiltInPluginDescriptor>& GetBuiltInPlugins();
    const BuiltInPluginDescriptor* FindBuiltInPlugin(std::string_view id);
    bool IsBuiltInPluginEnabled(const Project::ProjectConfig& config, std::string_view pluginId);
    bool SetBuiltInPluginEnabled(Project::ProjectConfig& config, const std::string& pluginId, bool enabled);
}
