#include "Luma/Editor/Panels/Console/ConsolePanel.h"

#include <imgui.h>

namespace Luma::Editor
{
    void ConsolePanel::Draw(bool* open, const ConsolePanelContext& context)
    {
        if (!ImGui::Begin("Console", open))
        {
            ImGui::End();
            return;
        }

        if (ImGui::BeginTabBar("##EditorConsoleTabBar"))
        {
            if (ImGui::BeginTabItem("Command Palette"))
            {
                m_CommandPalettePanel.Draw(context.commandPalette);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Console"))
            {
                m_ConsoleOutputPanel.Draw(context.output);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Tasks"))
            {
                m_ConsoleTasksPanel.Draw(context.tasks);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }
}
