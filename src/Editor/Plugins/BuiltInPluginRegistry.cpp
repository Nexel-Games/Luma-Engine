#include "Luma/Editor/Plugins/BuiltInPluginRegistry.h"

#include <algorithm>

namespace Luma::Editor
{
    namespace
    {
        const std::vector<BuiltInPluginDescriptor> kBuiltInPlugins = {};
    }

    const std::vector<BuiltInPluginDescriptor>& GetBuiltInPlugins()
    {
        return kBuiltInPlugins;
    }

    bool HasBuiltInPlugins()
    {
        return !kBuiltInPlugins.empty();
    }

    const BuiltInPluginDescriptor* FindBuiltInPlugin(const std::string_view id)
    {
        const auto it = std::find_if(
            kBuiltInPlugins.begin(),
            kBuiltInPlugins.end(),
            [id](const BuiltInPluginDescriptor& plugin)
            {
                return plugin.id == id;
            });
        return it != kBuiltInPlugins.end() ? &(*it) : nullptr;
    }

    bool IsBuiltInPluginEnabled(const Project::ProjectConfig& config, const std::string_view pluginId)
    {
        const auto it = std::find_if(
            config.plugins.begin(),
            config.plugins.end(),
            [pluginId](const Project::ProjectConfig::PluginConfig& plugin)
            {
                return plugin.id == pluginId;
            });
        if (it != config.plugins.end())
        {
            return it->enabled;
        }

        const BuiltInPluginDescriptor* descriptor = FindBuiltInPlugin(pluginId);
        return descriptor != nullptr ? descriptor->enabledByDefault : false;
    }

    bool SetBuiltInPluginEnabled(Project::ProjectConfig& config, const std::string& pluginId, const bool enabled)
    {
        const auto it = std::find_if(
            config.plugins.begin(),
            config.plugins.end(),
            [&pluginId](const Project::ProjectConfig::PluginConfig& plugin)
            {
                return plugin.id == pluginId;
            });
        if (it == config.plugins.end())
        {
            config.plugins.push_back({ pluginId, enabled });
            std::sort(
                config.plugins.begin(),
                config.plugins.end(),
                [](const Project::ProjectConfig::PluginConfig& lhs, const Project::ProjectConfig::PluginConfig& rhs)
                {
                    return lhs.id < rhs.id;
                });
            return true;
        }

        if (it->enabled == enabled)
        {
            return false;
        }

        it->enabled = enabled;
        return true;
    }
}
