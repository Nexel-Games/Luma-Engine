#pragma once

#include <functional>

#include <imgui.h>

#include "Luma/Editor/Core/EditorSelectionState.h"
#include "Luma/Editor/Viewport/EditorViewportController.h"
#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct EditorViewportInteractionContext
    {
        Scene* scene = nullptr;
        EditorViewportController* viewportController = nullptr;
        EditorSelectionState* selectionState = nullptr;
        const EditorViewportController::CameraState* editorCamera = nullptr;
        ImDrawList* drawList = nullptr;
        ImVec2 viewportMin { 0.0f, 0.0f };
        ImVec2 viewportMax { 0.0f, 0.0f };
        ImVec2 renderAreaSize { 0.0f, 0.0f };
        bool viewportInputBlockedByPopup = false;
        bool drawSceneTexture = false;
        EntityID lensSourceEntity = entt::null;
        std::function<void()> onSelectionChanged;
        std::function<void()> onTransformChanged;
    };

    class EditorViewportInteraction
    {
    public:
        void Handle(const EditorViewportInteractionContext& context);
    };
}
