#pragma once

#include <functional>

#include <imgui.h>

#include "Luma/Editor/Viewport/EditorViewportController.h"
#include "Luma/RHI/RendererAPI.h"
#include "Luma/Scene/Scene.h"

namespace Luma
{
    class IRenderBackend;
}

namespace Luma::Editor
{
    struct ViewportPanelContext
    {
        bool* open = nullptr;
        EditorViewportController* viewportController = nullptr;
        IRenderBackend* renderer = nullptr;
        void* skyboxPreviewTexture = nullptr;
        bool showColliderDebug = false;
        float deltaTimeSeconds = 0.0f;
        std::function<float()> resolveLensFovDegrees;
        std::function<bool(const ImVec2&, const ImVec2&)> isInputBlockedByPopup;
        std::function<void()> handleAssetDrop;
        std::function<void(ImDrawList*, const ImVec2&)> drawToolbar;
        std::function<void(ImDrawList*, const ImVec2&, const ImVec2&, EntityID)> drawDebugOverlay;
        std::function<void(ImDrawList*, const ImVec2&, const ImVec2&, const ImVec2&, bool, EntityID)> handleInteraction;
        std::function<EntityID()> findEditorCameraEntity;
    };

    class ViewportPanel
    {
    public:
        void Draw(const ViewportPanelContext& context);
    };
}
