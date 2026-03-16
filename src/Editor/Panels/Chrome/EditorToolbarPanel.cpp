#include "Luma/Editor/Panels/Chrome/EditorToolbarPanel.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>

#include "Luma/Editor/UI/TooltipAPI.h"

namespace Luma::Editor
{
    namespace
    {
        void ShowItemTooltip(const std::string_view tooltip)
        {
            UI::Tooltip::Show(tooltip);
        }

        template <typename... Args>
        bool ButtonWithTooltip(const char* label, Args&&... args)
        {
            const bool pressed = ImGui::Button(label, std::forward<Args>(args)...);
            UI::Tooltip::ShowForItemLabel(label);
            return pressed;
        }
    }

    void EditorToolbarPanel::Draw(const EditorToolbarPanelContext& context)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return;
        }

        constexpr float toolbarHeight = 44.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.08f, 0.10f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.21f, 0.26f, 0.34f, 1.0f));
        const ImGuiWindowFlags toolbarFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoNavFocus;
        if (!ImGui::BeginViewportSideBar(
                "##EditorTopToolbarBar",
                const_cast<ImGuiViewport*>(viewport),
                ImGuiDir_Up,
                toolbarHeight,
                toolbarFlags))
        {
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            return;
        }

        constexpr float buttonSize = 22.0f;
        constexpr float iconSize = 14.0f;
        constexpr float spacing = 6.0f;
        constexpr float groupPaddingX = 10.0f;
        constexpr float groupPaddingY = 6.0f;
        const float groupWidth = groupPaddingX * 2.0f + (buttonSize * 3.0f) + (spacing * 2.0f);
        const float groupHeight = groupPaddingY * 2.0f + buttonSize;

        const float availableWidth = ImGui::GetContentRegionAvail().x;
        if (availableWidth > groupWidth)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availableWidth - groupWidth) * 0.5f);
        }

        const ImVec2 groupStart = ImGui::GetCursorScreenPos();
        const ImVec2 pillMin(groupStart.x, groupStart.y);
        const ImVec2 pillMax(groupStart.x + groupWidth, groupStart.y + groupHeight);
        const float pillRadius = std::max(1.0f, (pillMax.y - pillMin.y) * 0.5f);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(pillMin, pillMax, IM_COL32(31, 34, 40, 230), pillRadius);
        drawList->AddRect(pillMin, pillMax, IM_COL32(70, 76, 88, 220), pillRadius, 0, 1.0f);
        const float dividerTop = pillMin.y + 5.0f;
        const float dividerBottom = pillMax.y - 5.0f;
        const float dividerX1 = groupStart.x + groupPaddingX + buttonSize + (spacing * 0.5f);
        const float dividerX2 = dividerX1 + buttonSize + spacing;
        drawList->AddLine(ImVec2(dividerX1, dividerTop), ImVec2(dividerX1, dividerBottom), IM_COL32(85, 93, 108, 210), 1.0f);
        drawList->AddLine(ImVec2(dividerX2, dividerTop), ImVec2(dividerX2, dividerBottom), IM_COL32(85, 93, 108, 210), 1.0f);

        const ImVec2 buttonStart(groupStart.x + groupPaddingX, groupStart.y + groupPaddingY);
        ImGui::SetCursorScreenPos(buttonStart);

        auto drawToolbarButton = [](
                                     const char* id,
                                     void* texture,
                                     const char* fallbackLabel,
                                     const char* tooltip,
                                     const bool enabled,
                                     const bool active,
                                     const float buttonDim,
                                     const float iconDim)
        {
            const float padding = std::max(0.0f, (buttonDim - iconDim) * 0.5f);
            const bool pushDisabled = !enabled;
            if (pushDisabled)
            {
                ImGui::BeginDisabled();
            }
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.36f, 0.58f, 0.98f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.42f, 0.68f, 0.98f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.30f, 0.48f, 1.0f));
            }
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padding, padding));
            bool pressed = false;
            if (texture != nullptr)
            {
                pressed = ImGui::ImageButton(
                    id,
                    reinterpret_cast<ImTextureID>(texture),
                    ImVec2(iconDim, iconDim),
                    ImVec2(0.0f, 0.0f),
                    ImVec2(1.0f, 1.0f),
                    ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
                    ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            }
            else
            {
                pressed = ButtonWithTooltip(fallbackLabel, ImVec2(buttonDim, buttonDim));
            }
            ImGui::PopStyleVar();
            if (active)
            {
                ImGui::PopStyleColor(3);
            }
            if (pushDisabled)
            {
                ImGui::EndDisabled();
            }
            ShowItemTooltip(tooltip);
            return pressed;
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.14f, 0.18f, 0.96f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.30f, 0.40f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.40f, 0.54f, 1.0f));

        if (drawToolbarButton(
                "##EditorToolbarPlay",
                context.playIconTexture,
                "Play",
                context.playActive ? "Play mode is active." : "Enter play mode and simulate the current scene.",
                context.playEnabled,
                context.playActive,
                buttonSize,
                iconSize) &&
            context.onPlay)
        {
            context.onPlay();
        }

        ImGui::SameLine(0.0f, spacing);
        if (drawToolbarButton(
                "##EditorToolbarPause",
                context.pauseIconTexture,
                "Pause",
                context.pauseActive ? "Resume scene simulation." : "Pause scene simulation without leaving play mode.",
                context.pauseEnabled,
                context.pauseActive,
                buttonSize,
                iconSize) &&
            context.onPause)
        {
            context.onPause();
        }

        ImGui::SameLine(0.0f, spacing);
        if (drawToolbarButton(
                "##EditorToolbarStop",
                context.stopIconTexture,
                "Stop",
                "Stop scene simulation and restore the pre-play editor scene.",
                context.stopEnabled,
                false,
                buttonSize,
                iconSize) &&
            context.onStop)
        {
            context.onStop();
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }
}
