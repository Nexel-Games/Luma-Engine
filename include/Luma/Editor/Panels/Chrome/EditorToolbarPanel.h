#pragma once

#include <functional>

namespace Luma::Editor
{
    struct EditorToolbarPanelContext
    {
        void* playIconTexture = nullptr;
        void* pauseIconTexture = nullptr;
        void* stopIconTexture = nullptr;
        bool playEnabled = true;
        bool pauseEnabled = false;
        bool stopEnabled = false;
        bool playActive = false;
        bool pauseActive = false;
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
