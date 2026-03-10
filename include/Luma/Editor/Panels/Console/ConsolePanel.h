#pragma once

#include "Luma/Editor/Panels/Console/CommandPalettePanel.h"
#include "Luma/Editor/Panels/Console/ConsoleOutputPanel.h"
#include "Luma/Editor/Panels/Console/ConsoleTasksPanel.h"

namespace Luma::Editor
{
    struct ConsolePanelContext
    {
        CommandPalettePanelContext commandPalette;
        ConsoleOutputPanelContext output;
        ConsoleTasksPanelContext tasks;
    };

    class ConsolePanel
    {
    public:
        void Draw(bool* open, const ConsolePanelContext& context);

    private:
        CommandPalettePanel m_CommandPalettePanel;
        ConsoleOutputPanel m_ConsoleOutputPanel;
        ConsoleTasksPanel m_ConsoleTasksPanel;
    };
}
