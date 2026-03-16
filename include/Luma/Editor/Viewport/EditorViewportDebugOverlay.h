#pragma once

#include <imgui.h>

#include "Luma/Editor/Core/EditorSelectionState.h"
#include "Luma/Editor/Viewport/EditorViewportController.h"
#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct EditorViewportDebugOverlayContext
    {
        const Scene* scene = nullptr;
        const EditorSelectionState* selectionState = nullptr;
        const EditorViewportController::CameraState* editorCamera = nullptr;
        ImDrawList* drawList = nullptr;
        ImVec2 viewportMin { 0.0f, 0.0f };
        ImVec2 renderAreaSize { 0.0f, 0.0f };
        EntityID lensSourceEntity = entt::null;
        void* pointLightIconTexture = nullptr;
    };

    class EditorViewportDebugOverlay
    {
    public:
        void Draw(const EditorViewportDebugOverlayContext& context);
    };
}
