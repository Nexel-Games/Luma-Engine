#include "Luma/Editor/Panels/Viewport/ViewportToolbarPanel.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

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

        template <typename... Args>
        bool SmallButtonWithTooltip(const char* label, Args&&... args)
        {
            const bool pressed = ImGui::SmallButton(label, std::forward<Args>(args)...);
            UI::Tooltip::ShowForItemLabel(label);
            return pressed;
        }

        bool GizmoToggleButton(
            const char* iconId,
            void* iconTexture,
            const char* fallbackLabel,
            const char* tooltip,
            const bool active,
            const ImVec2 iconSize)
        {
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.40f, 0.68f, 0.95f));
            }

            bool pressed = false;
            if (iconTexture != nullptr)
            {
                pressed = ImGui::ImageButton(
                    iconId,
                    reinterpret_cast<ImTextureID>(iconTexture),
                    iconSize,
                    ImVec2(0.0f, 0.0f),
                    ImVec2(1.0f, 1.0f),
                    ImVec4(0.0f, 0.0f, 0.0f, 0.0f),
                    ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            }
            else
            {
                pressed = SmallButtonWithTooltip(fallbackLabel);
            }

            if (active)
            {
                ImGui::PopStyleColor();
            }

            ShowItemTooltip(tooltip);
            return pressed;
        }
    }

    void ViewportToolbarPanel::Draw(const ViewportToolbarPanelContext& context)
    {
        if (context.viewportController == nullptr || context.drawList == nullptr)
        {
            return;
        }

        auto& viewportController = *context.viewportController;
        ImDrawList* drawList = context.drawList;
        const ImVec2 toolbarAnchor(context.viewportMin.x + 10.0f, context.viewportMin.y + 2.0f);
        const ImVec2 toolbarInnerPadding(5.0f, 3.0f);

        drawList->ChannelsSplit(2);
        drawList->ChannelsSetCurrent(1);

        ImGui::SetCursorScreenPos(ImVec2(toolbarAnchor.x + toolbarInnerPadding.x, toolbarAnchor.y + toolbarInnerPadding.y));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 2.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 3.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.12f, 0.16f, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.25f, 0.36f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.24f, 0.34f, 0.49f, 0.98f));
        ImGui::BeginGroup();

        std::array<float, 3> toolbarDividerXs {};
        int toolbarDividerCount = 0;
        constexpr float kToolbarDividerOffset = 1.5f;

        const bool selectActive = viewportController.SelectToolActive();
        const bool translateActive =
            viewportController.ActiveGizmoOperation() == EditorViewportController::GizmoOperation::Translate &&
            !viewportController.SelectToolActive();
        const bool rotateActive =
            viewportController.ActiveGizmoOperation() == EditorViewportController::GizmoOperation::Rotate;
        const bool scaleActive =
            viewportController.ActiveGizmoOperation() == EditorViewportController::GizmoOperation::Scale;

        if (GizmoToggleButton(
                "##GizmoSelectIcon",
                context.selectIconTexture,
                "Sel##GizmoSelect",
                "Select tool (Q). Drag to marquee-select entities.",
                selectActive,
                ImVec2(16.0f, 16.0f)))
        {
            viewportController.SelectToolActive() = true;
        }
        toolbarDividerXs[toolbarDividerCount++] = ImGui::GetItemRectMax().x + kToolbarDividerOffset;

        ImGui::SameLine();
        if (GizmoToggleButton(
                "##GizmoTranslateIcon",
                context.translateIconTexture,
                "T##GizmoTranslate",
                "Translate gizmo (W).",
                translateActive,
                ImVec2(16.0f, 16.0f)))
        {
            viewportController.SelectToolActive() = false;
            viewportController.ActiveGizmoOperation() = EditorViewportController::GizmoOperation::Translate;
        }
        toolbarDividerXs[toolbarDividerCount++] = ImGui::GetItemRectMax().x + kToolbarDividerOffset;

        ImGui::SameLine();
        if (GizmoToggleButton(
                "##GizmoRotateIcon",
                context.rotateIconTexture,
                "R##GizmoRotate",
                "Rotate gizmo (E).",
                rotateActive,
                ImVec2(16.0f, 16.0f)))
        {
            viewportController.SelectToolActive() = false;
            viewportController.ActiveGizmoOperation() = EditorViewportController::GizmoOperation::Rotate;
        }
        toolbarDividerXs[toolbarDividerCount++] = ImGui::GetItemRectMax().x + kToolbarDividerOffset;

        ImGui::SameLine();
        if (GizmoToggleButton(
                "##GizmoScaleIcon",
                context.scaleIconTexture,
                "S##GizmoScale",
                "Scale gizmo (R).",
                scaleActive,
                ImVec2(16.0f, 16.0f)))
        {
            viewportController.SelectToolActive() = false;
            viewportController.ActiveGizmoOperation() = EditorViewportController::GizmoOperation::Scale;
        }

        ImGui::EndGroup();
        const ImVec2 toolbarContentMin = ImGui::GetItemRectMin();
        const ImVec2 toolbarContentMax = ImGui::GetItemRectMax();

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);

        drawList->ChannelsSetCurrent(0);
        const ImVec2 pillMin(
            toolbarContentMin.x - toolbarInnerPadding.x,
            toolbarContentMin.y - toolbarInnerPadding.y);
        const ImVec2 pillMax(
            toolbarContentMax.x + toolbarInnerPadding.x,
            toolbarContentMax.y + toolbarInnerPadding.y);
        const float pillRadius = std::max(1.0f, (pillMax.y - pillMin.y) * 0.5f);
        drawList->AddRectFilled(pillMin, pillMax, IM_COL32(194, 198, 206, 210), pillRadius);

        const float dividerTop = pillMin.y + 4.0f;
        const float dividerBottom = pillMax.y - 4.0f;
        for (int dividerIndex = 0; dividerIndex < toolbarDividerCount; ++dividerIndex)
        {
            drawList->AddLine(
                ImVec2(toolbarDividerXs[dividerIndex], dividerTop),
                ImVec2(toolbarDividerXs[dividerIndex], dividerBottom),
                IM_COL32(132, 139, 151, 200),
                1.0f);
        }
        drawList->AddRect(pillMin, pillMax, IM_COL32(142, 148, 160, 220), pillRadius, 0, 1.0f);
        drawList->ChannelsMerge();

        const char* localSpaceLabel = viewportController.GizmoLocalSpace() ? "Local##GizmoSpace" : "World##GizmoSpace";
        const char* localSpaceText = viewportController.GizmoLocalSpace() ? "Local" : "World";
        const ImVec2 localTextSize = ImGui::CalcTextSize(localSpaceText);
        const float localPillWidth = localTextSize.x + 22.0f;
        const ImVec2 localPillMin(pillMax.x + 8.0f, pillMin.y);
        const ImVec2 localPillMax(localPillMin.x + localPillWidth, pillMax.y);
        const float localPillRadius = std::max(1.0f, (localPillMax.y - localPillMin.y) * 0.5f);
        drawList->AddRectFilled(localPillMin, localPillMax, IM_COL32(194, 198, 206, 210), localPillRadius);
        drawList->AddRect(localPillMin, localPillMax, IM_COL32(142, 148, 160, 220), localPillRadius, 0, 1.0f);

        const ImVec2 localButtonSize(localTextSize.x + 12.0f, ImGui::GetFrameHeight());
        const ImVec2 localButtonPos(
            localPillMin.x + std::max(0.0f, (localPillMax.x - localPillMin.x - localButtonSize.x) * 0.5f),
            localPillMin.y + std::max(0.0f, (localPillMax.y - localPillMin.y - localButtonSize.y) * 0.5f));
        ImGui::SetCursorScreenPos(localButtonPos);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, localButtonSize.y * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.28f, 0.37f, 0.20f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.40f, 0.68f, 0.28f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.10f, 0.14f, 0.95f));
        if (ButtonWithTooltip(localSpaceLabel, localButtonSize))
        {
            viewportController.GizmoLocalSpace() = !viewportController.GizmoLocalSpace();
        }
        ShowItemTooltip("Toggle gizmo space between Local and World (X).");
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);

        const float snapY = localPillMin.y + std::max(0.0f, (localPillMax.y - localPillMin.y - ImGui::GetFrameHeight()) * 0.5f);
        ImGui::SetCursorScreenPos(ImVec2(localPillMax.x + 6.0f, snapY));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.5f, 2.5f));
        if (GizmoToggleButton(
                "##GizmoSnapIconOutside",
                context.snapIconTexture,
                "Snap##GizmoSnapOutside",
                "Toggle transform snapping.",
                viewportController.GizmoSnapEnabled(),
                ImVec2(16.0f, 16.0f)))
        {
            viewportController.GizmoSnapEnabled() = !viewportController.GizmoSnapEnabled();
        }

        const ImVec2 snapButtonMax = ImGui::GetItemRectMax();
        ImGui::SetCursorScreenPos(ImVec2(snapButtonMax.x + 6.0f, snapY));
        if (GizmoToggleButton(
                "##GizmoGridIconOutside",
                context.gridIconTexture,
                "Grid##GizmoGridOutside",
                "Show or hide viewport grid.",
                viewportController.ShowGrid(),
                ImVec2(16.0f, 16.0f)))
        {
            viewportController.ShowGrid() = !viewportController.ShowGrid();
        }
        ImGui::PopStyleVar();
    }
}
