#include "Luma/Editor/Panels/Chrome/FooterBarPanel.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace Luma::Editor
{
    void FooterBarPanel::Draw(const FooterBarPanelContext& context)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return;
        }

        constexpr float footerHeight = 30.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.08f, 0.10f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.21f, 0.26f, 0.34f, 1.0f));
        const ImGuiWindowFlags footerFlags =
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoNavFocus;

        if (!ImGui::BeginViewportSideBar(
                "##EditorFooterBar",
                const_cast<ImGuiViewport*>(viewport),
                ImGuiDir_Down,
                footerHeight,
                footerFlags))
        {
            ImGui::End();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);
            return;
        }

        ImGui::TextDisabled("Status:");
        ImGui::SameLine(0.0f, 6.0f);
        ImGui::TextUnformatted(context.statusText.data());

        const float metricsWidth = ImGui::CalcTextSize(context.metricsText.data()).x;
        const float rightStart = ImGui::GetWindowContentRegionMax().x - metricsWidth;
        if (rightStart > ImGui::GetCursorPosX())
        {
            ImGui::SameLine(rightStart);
        }
        ImGui::TextDisabled("%s", context.metricsText.data());

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }
}
