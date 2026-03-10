#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Luma
{
    enum class LogLevel : int;
}

namespace Luma::Editor
{
    struct ConsoleEntry
    {
        LogLevel level;
        std::string category;
        std::string message;
        std::string line;
    };

    struct ConsoleOutputPanelContext
    {
        std::function<std::vector<ConsoleEntry>()> readEntries;
        std::function<void(std::string_view, bool)> executeCommand;
        std::string* searchQuery = nullptr;
        std::string* commandInput = nullptr;
        const std::vector<std::string>* commandHistory = nullptr;
        int* historyCursor = nullptr;
        bool* filterTrace = nullptr;
        bool* filterInfo = nullptr;
        bool* filterWarn = nullptr;
        bool* filterError = nullptr;
        bool* filterFatal = nullptr;
        bool* autoScroll = nullptr;
        bool* scrollToBottom = nullptr;
    };

    class ConsoleOutputPanel
    {
    public:
        void Draw(const ConsoleOutputPanelContext& context);
    };
}
