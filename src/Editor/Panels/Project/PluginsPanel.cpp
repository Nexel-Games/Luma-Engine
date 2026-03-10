#include "Luma/Editor/Panels/Project/PluginsPanel.h"

#include <utility>

#include <imgui.h>

#include "Luma/Core/App/Project.h"
#include "Luma/Editor/Plugins/BuiltInPluginRegistry.h"
#include "Luma/Editor/UI/TooltipAPI.h"

namespace Luma::Editor
{
    namespace
    {
        void ShowItemTooltip(const std::string_view tooltip)
        {
            UI::Tooltip::Show(tooltip);
        }

        void ShowItemTooltipFromLabel(const char* label, const char* prefix = nullptr)
        {
            UI::Tooltip::ShowForItemLabel(label, prefix == nullptr ? std::string_view {} : std::string_view(prefix));
        }

        template <typename... Args>
        bool CheckboxWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::Checkbox(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Toggle ");
            return changed;
        }
    }

    void PluginsPanel::Draw(bool* open, const PluginsPanelContext& context)
    {
        ImGui::SetNextWindowSize(ImVec2(840.0f, 520.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Plugins", open))
        {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("Built-In Plugins");
        ImGui::Separator();
        ImGui::TextWrapped(
            "Built-in plugins are compiled into this Luma build already. Enabling a plugin activates its "
            "editor panels and project integration for the current project.");

        if (!Project::IsLoaded())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("No loaded project. Open a project before enabling plugins.");
            ImGui::End();
            return;
        }

        const auto& plugins = GetBuiltInPlugins();
        if (plugins.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("No built-in plugins are registered in this build.");
            ImGui::End();
            return;
        }

        ImGui::Spacing();
        for (const auto& plugin : plugins)
        {
            const bool enabled = IsBuiltInPluginEnabled(Project::GetConfig(), plugin.id);
            ImGui::PushID(plugin.id.c_str());
            ImGui::BeginChild("##PluginCard", ImVec2(0.0f, 108.0f), true);

            bool nextEnabled = enabled;
            if (CheckboxWithTooltip("##PluginEnabled", &nextEnabled))
            {
                Project::ProjectConfig updatedConfig = Project::GetConfig();
                if (SetBuiltInPluginEnabled(updatedConfig, plugin.id, nextEnabled) &&
                    Project::UpdateSettings(updatedConfig, true))
                {
                    if (context.invalidateProjectSettingsDraft)
                    {
                        context.invalidateProjectSettingsDraft();
                    }
                    if (context.setProjectConfigStatus)
                    {
                        context.setProjectConfigStatus(
                            (nextEnabled ? "Enabled " : "Disabled ") + plugin.displayName + " plugin.");
                    }
                }
                else
                {
                    if (context.setProjectConfigStatus)
                    {
                        context.setProjectConfigStatus(
                            "Failed to update plugin state for " + plugin.displayName + ".");
                    }
                    nextEnabled = enabled;
                }
            }
            ShowItemTooltip(
                enabled
                    ? "Disable this plugin for the current project."
                    : "Enable this plugin for the current project.");

            ImGui::SameLine();
            ImGui::TextUnformatted(plugin.displayName.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", plugin.id.c_str());

            ImGui::TextWrapped("%s", plugin.description.c_str());
            ImGui::TextDisabled(
                "Activation: %s | Panel: %s | Scope: %s",
                plugin.activationPolicy == BuiltInPluginActivationPolicy::Immediate ? "Immediate" : "Restart Required",
                plugin.panelName.empty() ? "None" : plugin.panelName.c_str(),
                plugin.editorOnly ? "Editor" : "Runtime");

            ImGui::EndChild();
            ImGui::PopID();
            ImGui::Spacing();
        }

        const std::string_view status = context.getProjectConfigStatus ? context.getProjectConfigStatus() : std::string_view {};
        if (!status.empty())
        {
            ImGui::TextDisabled("%.*s", static_cast<int>(status.size()), status.data());
        }

        ImGui::End();
    }
}
