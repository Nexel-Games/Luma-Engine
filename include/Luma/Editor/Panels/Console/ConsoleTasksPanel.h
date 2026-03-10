#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace Luma::Editor
{
    struct ConsoleTaskState
    {
        std::string name;
        std::string description;
        bool running = false;
        bool cancellable = true;
        float progress = 0.0f;
        float speed = 0.12f;
    };

    struct ConsoleTasksPanelContext
    {
        std::vector<ConsoleTaskState>* tasks = nullptr;
        std::function<void(std::size_t)> startTask;
        std::function<void(std::size_t)> cancelTask;
    };

    class ConsoleTasksPanel
    {
    public:
        void Draw(const ConsoleTasksPanelContext& context);
    };
}
