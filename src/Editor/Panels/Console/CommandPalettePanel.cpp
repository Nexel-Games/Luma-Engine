#include "Luma/Editor/Panels/Console/CommandPalettePanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"

namespace Luma::Editor
{
    namespace
    {
        std::string ToLowerString(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }

        void ShowItemTooltip(const std::string_view tooltip)
        {
            UI::Tooltip::Show(tooltip);
        }

        void ShowItemTooltipFromLabel(const char* label, const char* prefix = nullptr)
        {
            UI::Tooltip::ShowForItemLabel(label, prefix == nullptr ? std::string_view {} : std::string_view(prefix));
        }

        template <typename... Args>
        bool InputTextWithHintWithTooltip(const char* label, const char* hint, Args&&... args)
        {
            const bool changed = ImGui::InputTextWithHint(label, hint, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return changed;
        }

        template <typename... Args>
        bool SelectableWithTooltip(const char* label, Args&&... args)
        {
            const bool selected = ImGui::Selectable(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return selected;
        }

        template <typename... Args>
        bool MenuItemWithTooltip(const char* label, Args&&... args)
        {
            const bool activated = ImGui::MenuItem(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return activated;
        }
    }

    void CommandPalettePanel::Draw(const CommandPalettePanelContext& context)
    {
        if (context.query == nullptr || context.commands == nullptr || context.recentCommands == nullptr || context.favoriteCommands == nullptr)
        {
            return;
        }

        ShowItemTooltip("Find and run editor commands quickly.");
        std::array<char, 256> paletteBuffer {};
        std::snprintf(paletteBuffer.data(), paletteBuffer.size(), "%s", context.query->c_str());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 5.0f));
        if (InputTextWithHintWithTooltip("##CommandPaletteSearch", "Search commands...", paletteBuffer.data(), paletteBuffer.size()))
        {
            *context.query = paletteBuffer.data();
        }
        ShowItemTooltip("Search editor commands by name, category, usage, or description.");
        ImGui::PopStyleVar(2);

        if (!context.recentCommands->empty() && ImGui::CollapsingHeader("Recent Commands", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ShowItemTooltip("Recently executed commands.");
            const std::size_t recentCount = std::min<std::size_t>(context.recentCommands->size(), 6);
            for (std::size_t i = 0; i < recentCount; ++i)
            {
                const std::string& recentCommand = (*context.recentCommands)[i];
                if (SelectableWithTooltip(recentCommand.c_str(), false))
                {
                    if (context.setCommandInput)
                    {
                        context.setCommandInput(recentCommand);
                    }
                    if (context.executeCommand)
                    {
                        context.executeCommand(recentCommand);
                    }
                }
                ShowItemTooltip("Run recent command.");
            }
        }

        const std::string queryLower = ToLowerString(*context.query);
        ImGui::BeginChild("##CommandPaletteList", ImVec2(0.0f, 0.0f), true);
        int visibleCommands = 0;
        for (const ConsoleCommandDesc& command : *context.commands)
        {
            const std::string haystack =
                ToLowerString(command.name + " " + command.description + " " + command.category + " " + command.usage);
            if (!queryLower.empty() && haystack.find(queryLower) == std::string::npos)
            {
                continue;
            }

            ++visibleCommands;
            ImGui::PushID(command.name.c_str());
            const bool isFavorite =
                std::find(context.favoriteCommands->begin(), context.favoriteCommands->end(), command.name) !=
                context.favoriteCommands->end();
            const std::string commandLabel = std::string(isFavorite ? "[*] " : "    ") + command.name;
            if (SelectableWithTooltip(commandLabel.c_str(), false))
            {
                if (context.setCommandInput)
                {
                    context.setCommandInput(command.name);
                }
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && context.executeCommand)
                {
                    context.executeCommand(command.name);
                }
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip(
                    "%s\nCategory: %s\nUsage: %s\n\nDouble-click to run.\nRight-click for actions.",
                    command.description.c_str(),
                    command.category.c_str(),
                    command.usage.c_str());
            }
            if (ImGui::BeginPopupContextItem("##CommandActions"))
            {
                if (MenuItemWithTooltip("Run"))
                {
                    if (context.setCommandInput)
                    {
                        context.setCommandInput(command.name);
                    }
                    if (context.executeCommand)
                    {
                        context.executeCommand(command.name);
                    }
                }
                if (MenuItemWithTooltip(isFavorite ? "Remove Favorite" : "Add Favorite"))
                {
                    if (isFavorite)
                    {
                        context.favoriteCommands->erase(
                            std::remove(context.favoriteCommands->begin(), context.favoriteCommands->end(), command.name),
                            context.favoriteCommands->end());
                    }
                    else
                    {
                        context.favoriteCommands->push_back(command.name);
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
        if (visibleCommands == 0)
        {
            ImGui::TextDisabled("No commands match your search.");
        }
        ImGui::EndChild();
    }
}
