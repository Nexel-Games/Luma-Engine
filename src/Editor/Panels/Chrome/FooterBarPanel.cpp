#include "Luma/Editor/Panels/Chrome/FooterBarPanel.h"

#include <algorithm>

#include <imgui.h>
#include <imgui_internal.h>

namespace Luma::Editor
{
    namespace
    {
        ImVec4 FooterMessageColor(const LogLevel level)
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
    }

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

        const float metricsWidth = ImGui::CalcTextSize(context.metricsText.data()).x;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        ImGui::PushStyleColor(ImGuiCol_Text, FooterMessageColor(context.messageLevel));
        ImGui::TextUnformatted(context.messageText.data());
        ImGui::PopStyleColor();

        const float rightStart = ImGui::GetWindowContentRegionMax().x - metricsWidth;
        if (rightStart > ImGui::GetCursorPosX() + spacing)
        {
            ImGui::SameLine(rightStart);
        }
        ImGui::TextDisabled("%s", context.metricsText.data());

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }
}
