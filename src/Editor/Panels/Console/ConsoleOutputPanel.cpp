#include "Luma/Editor/Panels/Console/ConsoleOutputPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Core/Foundation/Logging.h"
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

        ImVec4 LogLevelColor(const LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Trace:
                return ImVec4(0.60f, 0.64f, 0.72f, 1.0f);
            case LogLevel::Info:
                return ImVec4(0.72f, 0.80f, 0.92f, 1.0f);
            case LogLevel::Warn:
                return ImVec4(0.95f, 0.77f, 0.35f, 1.0f);
            case LogLevel::Error:
                return ImVec4(0.96f, 0.43f, 0.43f, 1.0f);
            case LogLevel::Fatal:
                return ImVec4(1.00f, 0.22f, 0.22f, 1.0f);
            default:
                return ImVec4(0.72f, 0.80f, 0.92f, 1.0f);
            }
        }

        void ShowItemTooltip(const std::string_view tooltip)
        {
            UI::Tooltip::Show(tooltip);
        }

        void ShowItemTooltipFromLabel(const char* label, const char* prefix = nullptr)
        {
            UI::Tooltip::ShowForItemLabel(label, prefix == nullptr ? std::string_view {} : std::string_view(prefix));
        }

        bool IsLevelVisible(const ConsoleOutputPanelContext& context, const LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Trace:
                return context.filterTrace != nullptr && *context.filterTrace;
            case LogLevel::Info:
                return context.filterInfo != nullptr && *context.filterInfo;
            case LogLevel::Warn:
                return context.filterWarn != nullptr && *context.filterWarn;
            case LogLevel::Error:
                return context.filterError != nullptr && *context.filterError;
            case LogLevel::Fatal:
                return context.filterFatal != nullptr && *context.filterFatal;
            default:
                return true;
            }
        }

        template <typename... Args>
        bool ButtonWithTooltip(const char* label, Args&&... args)
        {
            const bool pressed = ImGui::Button(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return pressed;
        }

        template <typename... Args>
        bool CheckboxWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::Checkbox(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return changed;
        }

        template <typename... Args>
        bool InputTextWithHintWithTooltip(const char* label, const char* hint, Args&&... args)
        {
            const bool changed = ImGui::InputTextWithHint(label, hint, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return changed;
        }
    }

    void ConsoleOutputPanel::Draw(const ConsoleOutputPanelContext& context)
    {
        if (!context.searchQuery ||
            !context.commandInput ||
            !context.commandHistory ||
            !context.historyCursor ||
            !context.filterTrace ||
            !context.filterInfo ||
            !context.filterWarn ||
            !context.filterError ||
            !context.filterFatal ||
            !context.autoScroll ||
            !context.scrollToBottom ||
            !context.readEntries ||
            !context.executeCommand)
        {
            return;
        }

        ShowItemTooltip("Editor logs and command input.");
        if (ButtonWithTooltip("Clear"))
        {
            context.executeCommand("clear", false);
        }
        ShowItemTooltip("Clear console log output.");
        ImGui::SameLine();
        CheckboxWithTooltip("Auto", context.autoScroll);
        ShowItemTooltip("Auto-scroll console to latest log entries.");
        ImGui::SameLine();
        if (ButtonWithTooltip("Levels"))
        {
            ImGui::OpenPopup("##ConsoleLevelsPopup");
        }
        ShowItemTooltip("Choose visible log levels.");
        if (ImGui::BeginPopup("##ConsoleLevelsPopup"))
        {
            CheckboxWithTooltip("Trace", context.filterTrace);
            ShowItemTooltip("Show trace logs.");
            CheckboxWithTooltip("Info", context.filterInfo);
            ShowItemTooltip("Show info logs.");
            CheckboxWithTooltip("Warn", context.filterWarn);
            ShowItemTooltip("Show warning logs.");
            CheckboxWithTooltip("Error", context.filterError);
            ShowItemTooltip("Show error logs.");
            CheckboxWithTooltip("Fatal", context.filterFatal);
            ShowItemTooltip("Show fatal logs.");
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        std::array<char, 256> searchBuffer {};
        std::snprintf(searchBuffer.data(), searchBuffer.size(), "%s", context.searchQuery->c_str());
        ImGui::SetNextItemWidth(-1.0f);
        if (InputTextWithHintWithTooltip("##ConsoleSearch", "Search logs...", searchBuffer.data(), searchBuffer.size()))
        {
            *context.searchQuery = searchBuffer.data();
        }
        ShowItemTooltip("Filter console output by text.");

        const std::vector<ConsoleEntry> entries = context.readEntries();
        const std::string filterLower = ToLowerString(*context.searchQuery);

        const float commandRowHeight = ImGui::GetFrameHeightWithSpacing() + 4.0f;
        ImGui::BeginChild("##ConsoleLogOutput", ImVec2(0.0f, -commandRowHeight), true, ImGuiWindowFlags_HorizontalScrollbar);
        for (const ConsoleEntry& entry : entries)
        {
            if (!IsLevelVisible(context, entry.level))
            {
                continue;
            }

            if (!filterLower.empty() && ToLowerString(entry.line).find(filterLower) == std::string::npos)
            {
                continue;
            }

            ImGui::PushStyleColor(ImGuiCol_Text, LogLevelColor(entry.level));
            ImGui::TextUnformatted(entry.line.c_str());
            ImGui::PopStyleColor();
        }

        if (*context.scrollToBottom || (*context.autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f))
        {
            ImGui::SetScrollHereY(1.0f);
        }
        *context.scrollToBottom = false;
        ImGui::EndChild();

        std::array<char, 512> commandBuffer {};
        std::snprintf(commandBuffer.data(), commandBuffer.size(), "%s", context.commandInput->c_str());
        ImGui::SetNextItemWidth(-58.0f);
        const bool submitted = InputTextWithHintWithTooltip(
            "##ConsoleCommandInput",
            "Enter command (help)...",
            commandBuffer.data(),
            commandBuffer.size(),
            ImGuiInputTextFlags_EnterReturnsTrue);
        ShowItemTooltip("Run console commands. Press Enter to execute.");
        *context.commandInput = commandBuffer.data();

        if (ImGui::IsItemActive() && !context.commandHistory->empty())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            {
                if (*context.historyCursor < 0)
                {
                    *context.historyCursor = static_cast<int>(context.commandHistory->size()) - 1;
                }
                else if (*context.historyCursor > 0)
                {
                    --(*context.historyCursor);
                }
                *context.commandInput = (*context.commandHistory)[static_cast<std::size_t>(*context.historyCursor)];
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            {
                if (*context.historyCursor >= 0 &&
                    *context.historyCursor < static_cast<int>(context.commandHistory->size()) - 1)
                {
                    ++(*context.historyCursor);
                    *context.commandInput = (*context.commandHistory)[static_cast<std::size_t>(*context.historyCursor)];
                }
                else
                {
                    *context.historyCursor = -1;
                    context.commandInput->clear();
                }
            }
        }

        ImGui::SameLine();
        if (submitted || ButtonWithTooltip("Run", ImVec2(52.0f, 0.0f)))
        {
            const std::string line = *context.commandInput;
            context.commandInput->clear();
            context.executeCommand(line, true);
        }
        ShowItemTooltip("Execute the command in the input field.");
    }
}
