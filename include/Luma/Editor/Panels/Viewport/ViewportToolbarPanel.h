#pragma once

#include <imgui.h>

#include "Luma/Editor/Viewport/EditorViewportController.h"

namespace Luma::Editor
{
    struct ViewportToolbarPanelContext
    {
        EditorViewportController* viewportController = nullptr;
        ImDrawList* drawList = nullptr;
        ImVec2 viewportMin { 0.0f, 0.0f };
        void* selectIconTexture = nullptr;
        void* translateIconTexture = nullptr;
        void* rotateIconTexture = nullptr;
        void* scaleIconTexture = nullptr;
        void* snapIconTexture = nullptr;
        void* gridIconTexture = nullptr;
    };

    class ViewportToolbarPanel
    {
    public:
        void Draw(const ViewportToolbarPanelContext& context);
    };
}
