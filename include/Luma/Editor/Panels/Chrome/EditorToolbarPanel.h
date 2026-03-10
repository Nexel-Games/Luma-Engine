#pragma once

#include <functional>

namespace Luma::Editor
{
    struct EditorToolbarPanelContext
    {
        void* playIconTexture = nullptr;
        void* pauseIconTexture = nullptr;
        void* stopIconTexture = nullptr;
        std::function<void()> onPlay;
        std::function<void()> onPause;
        std::function<void()> onStop;
    };

    class EditorToolbarPanel
    {
    public:
        void Draw(const EditorToolbarPanelContext& context);
    };
}
