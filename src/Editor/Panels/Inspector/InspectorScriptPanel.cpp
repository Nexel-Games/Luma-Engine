#include "Luma/Editor/Panels/Inspector/InspectorScriptPanel.h"

#include <array>
#include <cstdio>
#include <string>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/LuaScriptComponent.h"

namespace Luma::Editor
{
    namespace
    {
        bool IsScriptSelection(const std::filesystem::path& path)
        {
            const std::string extension = path.extension().string();
            return extension == ".lua" || extension == ".lumascript";
        }
    }

    void InspectorScriptPanel::Draw(const InspectorScriptPanelContext& context)
    {
        if (context.scene == nullptr || context.selectedEntity == entt::null)
        {
            return;
        }

        auto& registry = context.scene->GetRegistry();
        if (!registry.valid(context.selectedEntity) || !registry.all_of<LuaScriptComponent>(context.selectedEntity))
        {
            return;
        }

        auto& component = registry.get<LuaScriptComponent>(context.selectedEntity);
        if (!ImGui::CollapsingHeader("Lua Script", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ImGui::Checkbox("Enabled", &component.enabled);
        UI::Tooltip::ShowForItemLabel("Enabled", "Toggle ");

        std::array<char, 260> pathBuffer {};
        std::snprintf(pathBuffer.data(), pathBuffer.size(), "%s", component.scriptAsset.c_str());
        if (ImGui::InputText("Script Asset", pathBuffer.data(), pathBuffer.size()))
        {
            component.scriptAsset = pathBuffer.data();
        }
        UI::Tooltip::ShowForItemLabel("Script Asset", "Set ");

        if (context.selectedContentEntry != nullptr &&
            !context.selectedContentEntry->empty() &&
            IsScriptSelection(*context.selectedContentEntry))
        {
            if (ImGui::Button("Use Selected Script"))
            {
                component.scriptAsset = context.selectedContentEntry->generic_string();
                if (context.setContentStatus)
                {
                    context.setContentStatus("Assigned Lua script: " + component.scriptAsset);
                }
            }
            UI::Tooltip::ShowForItemLabel("Use Selected Script");
        }

        if (component.scriptAsset.empty())
        {
            ImGui::TextDisabled("Assign a .lua source file or .lumascript asset.");
        }
        else
        {
            ImGui::TextDisabled("Path: %s", component.scriptAsset.c_str());
        }

        if (ImGui::Button("Remove Lua Script Component"))
        {
            registry.remove<LuaScriptComponent>(context.selectedEntity);
        }
        UI::Tooltip::ShowForItemLabel("Remove Lua Script Component");
    }
}
