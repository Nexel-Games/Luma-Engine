#pragma once

#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Editor/Panels/Console/CommandPalettePanel.h"
#include "Luma/Editor/Panels/Console/ConsoleOutputPanel.h"
#include "Luma/Editor/Panels/Console/ConsolePanel.h"
#include "Luma/Editor/Panels/Console/ConsoleTasksPanel.h"

namespace Luma::Editor
{
    struct ConsolePanelHostContext
    {
        ConsolePanel* panel = nullptr;
        bool* open = nullptr;

        std::string* commandPaletteQuery = nullptr;
        std::vector<ConsoleCommandDesc>* commands = nullptr;
        std::vector<std::string>* recentCommands = nullptr;
        std::vector<std::string>* favoriteCommands = nullptr;
        std::string* commandInput = nullptr;
        std::function<void(std::string_view, bool)> executeCommand;

        std::vector<ConsoleEntry>* entries = nullptr;
        std::mutex* entriesMutex = nullptr;
        std::string* searchQuery = nullptr;
        std::vector<std::string>* commandHistory = nullptr;
        int* historyCursor = nullptr;
        bool* filterTrace = nullptr;
        bool* filterInfo = nullptr;
        bool* filterWarn = nullptr;
        bool* filterError = nullptr;
        bool* filterFatal = nullptr;
        bool* autoScroll = nullptr;
        bool* scrollToBottom = nullptr;

        std::vector<ConsoleTaskState>* tasks = nullptr;
        std::function<void(std::size_t)> startTask;
        std::function<void(std::size_t)> cancelTask;
    };

    class ConsolePanelHostService
    {
    public:
        void Draw(const ConsolePanelHostContext& context) const;
    };
}
