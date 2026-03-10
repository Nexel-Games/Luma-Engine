#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace Luma
{
    class IRenderBackend;

    using EditorTaskHandle = std::uint64_t;

    struct EditorTaskDesc
    {
        std::string title;
        std::string subtask;
        bool blocking = true;
        bool cancellable = false;
    };

    class EditorTaskManager
    {
    public:
        static void SetRenderBackend(IRenderBackend* backend);
        static EditorTaskHandle BeginTask(const EditorTaskDesc& desc);
        static bool SetTitle(EditorTaskHandle handle, std::string_view title);
        static bool SetSubtask(EditorTaskHandle handle, std::string_view subtask);
        static bool SetProgress(EditorTaskHandle handle, float progress01);
        static bool EndTask(EditorTaskHandle handle);

        static bool IsCancelRequested(EditorTaskHandle handle);
        static bool HasBlockingTask();
        static void DrawOverlay();
        static void Reset();
    };
}
