#include "Luma/Editor/Panels/Console/ConsoleTasksPanel.h"

#include <string_view>
#include <utility>

#include <imgui.h>

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
        bool SmallButtonWithTooltip(const char* label, Args&&... args)
        {
            const bool pressed = ImGui::SmallButton(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return pressed;
        }
    }

    void ConsoleTasksPanel::Draw(const ConsoleTasksPanelContext& context)
    {
        if (context.tasks == nullptr)
        {
            return;
        }

        ShowItemTooltip("Run and monitor long editor tasks.");
        for (std::size_t index = 0; index < context.tasks->size(); ++index)
        {
            ConsoleTaskState& task = (*context.tasks)[index];
            ImGui::PushID(task.name.c_str());
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(task.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("- %s", task.description.c_str());
            ImGui::ProgressBar(task.progress, ImVec2(-72.0f, 8.0f));
            ImGui::SameLine();
            if (!task.running)
            {
                if (SmallButtonWithTooltip("Run") && context.startTask)
                {
                    context.startTask(index);
                }
            }
            else if (task.cancellable)
            {
                if (SmallButtonWithTooltip("Cancel") && context.cancelTask)
                {
                    context.cancelTask(index);
                }
            }
            ImGui::PopID();
        }
    }
}
