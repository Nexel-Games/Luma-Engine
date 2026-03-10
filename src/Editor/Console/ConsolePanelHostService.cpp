#include "Luma/Editor/Console/ConsolePanelHostService.h"

namespace Luma::Editor
{
    void ConsolePanelHostService::Draw(const ConsolePanelHostContext& context) const
    {
        if (context.panel == nullptr)
        {
            return;
        }

        ConsolePanelContext panelContext {};
        panelContext.commandPalette.query = context.commandPaletteQuery;
        panelContext.commandPalette.commands = context.commands;
        panelContext.commandPalette.recentCommands = context.recentCommands;
        panelContext.commandPalette.favoriteCommands = context.favoriteCommands;
        panelContext.commandPalette.setCommandInput = [&context](const std::string_view commandInput)
        {
            if (context.commandInput != nullptr)
            {
                *context.commandInput = commandInput;
            }
        };
        panelContext.commandPalette.executeCommand = [&context](const std::string_view command)
        {
            if (context.executeCommand)
            {
                context.executeCommand(command, true);
            }
        };

        panelContext.output.readEntries = [&context]()
        {
            if (context.entries == nullptr || context.entriesMutex == nullptr)
            {
                return std::vector<ConsoleEntry> {};
            }

            std::scoped_lock lock(*context.entriesMutex);
            return *context.entries;
        };
        panelContext.output.executeCommand = context.executeCommand;
        panelContext.output.searchQuery = context.searchQuery;
        panelContext.output.commandInput = context.commandInput;
        panelContext.output.commandHistory = context.commandHistory;
        panelContext.output.historyCursor = context.historyCursor;
        panelContext.output.filterTrace = context.filterTrace;
        panelContext.output.filterInfo = context.filterInfo;
        panelContext.output.filterWarn = context.filterWarn;
        panelContext.output.filterError = context.filterError;
        panelContext.output.filterFatal = context.filterFatal;
        panelContext.output.autoScroll = context.autoScroll;
        panelContext.output.scrollToBottom = context.scrollToBottom;

        panelContext.tasks.tasks = context.tasks;
        panelContext.tasks.startTask = context.startTask;
        panelContext.tasks.cancelTask = context.cancelTask;

        context.panel->Draw(context.open, panelContext);
    }
}
