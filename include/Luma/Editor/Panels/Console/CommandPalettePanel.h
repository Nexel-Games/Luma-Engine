#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Luma::Editor
{
    struct ConsoleCommandDesc
    {
        std::string name;
        std::string category;
        std::string description;
        std::string usage;
    };

    struct CommandPalettePanelContext
    {
        std::string* query = nullptr;
        const std::vector<ConsoleCommandDesc>* commands = nullptr;
        const std::vector<std::string>* recentCommands = nullptr;
        std::vector<std::string>* favoriteCommands = nullptr;
        std::function<void(std::string_view)> setCommandInput;
        std::function<void(std::string_view)> executeCommand;
    };

    class CommandPalettePanel
    {
    public:
        void Draw(const CommandPalettePanelContext& context);
    };
}
