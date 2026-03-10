#pragma once

#include <array>
#include <cstdint>

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    class EditorViewportController final
    {
    public:
        enum class GizmoOperation : std::uint8_t
        {
            Translate = 0,
            Rotate = 1,
            Scale = 2
        };

        struct CameraState
        {
            std::array<float, 3> position { 0.0f, 0.0f, 5.0f };
            float yaw = -90.0f;
            float pitch = 0.0f;
            float focusDistance = 5.0f;
            float moveSpeed = 6.0f;
            float lookSensitivity = 0.10f;
            float panSpeed = 0.01f;
            float zoomSpeed = 1.25f;
        };

        CameraState& Camera();
        const CameraState& Camera() const;

        bool& ShowGrid();
        bool ShowGrid() const;

        bool& ShowCameraDebugOverlay();
        bool ShowCameraDebugOverlay() const;

        bool& PreviewSceneCameraLens();
        bool PreviewSceneCameraLens() const;

        bool& SelectToolActive();
        bool SelectToolActive() const;

        bool& MarqueeSelecting();
        bool MarqueeSelecting() const;

        std::array<float, 2>& MarqueeStart();
        const std::array<float, 2>& MarqueeStart() const;

        std::array<float, 2>& MarqueeCurrent();
        const std::array<float, 2>& MarqueeCurrent() const;

        GizmoOperation& ActiveGizmoOperation();
        GizmoOperation ActiveGizmoOperation() const;

        bool& GizmoLocalSpace();
        bool GizmoLocalSpace() const;

        bool& GizmoSnapEnabled();
        bool GizmoSnapEnabled() const;

        std::array<float, 3>& GizmoTranslateSnap();
        const std::array<float, 3>& GizmoTranslateSnap() const;

        float& GizmoRotateSnap();
        float GizmoRotateSnap() const;

        std::array<float, 3>& GizmoScaleSnap();
        const std::array<float, 3>& GizmoScaleSnap() const;

        EntityID LensSourceEntity() const;
        void SetLensSourceEntity(EntityID entity);

        bool ViewportFocused() const;
        bool ViewportHovered() const;
        void SetViewportWindowState(bool focused, bool hovered, float width, float height);

        float ViewportWidth() const;
        float ViewportHeight() const;

        bool GizmoInteracting() const;
        void SetGizmoInteracting(bool interacting);

        bool RightMousePressed() const;
        bool MiddleMousePressed() const;

        void ClearViewportInteraction();
        void ResetCameraToDefault();
        std::array<float, 3> ComputeDropSpawnPosition() const;
        void ApplyToolHotkeys(bool allowHotkeys);
        void HandleCameraInput(float deltaTimeSeconds);

    private:
        CameraState m_Camera {};
        bool m_ShowGrid = true;
        bool m_ShowCameraDebugOverlay = true;
        bool m_PreviewSceneCameraLens = true;
        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;
        bool m_GizmoInteracting = false;
        float m_ViewportWidth = 0.0f;
        float m_ViewportHeight = 0.0f;
        bool m_ViewportSelectTool = false;
        bool m_ViewportMarqueeSelecting = false;
        std::array<float, 2> m_ViewportMarqueeStart { 0.0f, 0.0f };
        std::array<float, 2> m_ViewportMarqueeCurrent { 0.0f, 0.0f };
        GizmoOperation m_GizmoOperation = GizmoOperation::Translate;
        bool m_GizmoLocalSpace = true;
        bool m_GizmoSnapEnabled = false;
        std::array<float, 3> m_GizmoTranslateSnap { 0.5f, 0.5f, 0.5f };
        float m_GizmoRotateSnap = 15.0f;
        std::array<float, 3> m_GizmoScaleSnap { 0.1f, 0.1f, 0.1f };
        bool m_RightMousePressed = false;
        bool m_MiddleMousePressed = false;
        bool m_LastRightMouseState = false;
        bool m_LastMiddleMouseState = false;
        EntityID m_ViewportLensSourceEntity = entt::null;
        double m_LastViewportMouseX = 0.0;
        double m_LastViewportMouseY = 0.0;
        bool m_HasViewportMouseSample = false;
    };
}
