#include "Luma/Editor/Viewport/EditorViewportController.h"

#include <algorithm>
#include <cmath>

#include <imgui.h>

#include "Luma/Input/Input.h"

namespace Luma::Editor
{
    namespace
    {
        struct Vec3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        Vec3 operator+(const Vec3& lhs, const Vec3& rhs)
        {
            return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
        }

        Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
        {
            return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
        }

        Vec3 operator*(const Vec3& value, const float scalar)
        {
            return { value.x * scalar, value.y * scalar, value.z * scalar };
        }

        float Dot(const Vec3& lhs, const Vec3& rhs)
        {
            return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
        }

        Vec3 Cross(const Vec3& lhs, const Vec3& rhs)
        {
            return {
                lhs.y * rhs.z - lhs.z * rhs.y,
                lhs.z * rhs.x - lhs.x * rhs.z,
                lhs.x * rhs.y - lhs.y * rhs.x
            };
        }

        Vec3 Normalize(const Vec3& value)
        {
            const float lengthSq = Dot(value, value);
            if (lengthSq <= 1.0e-8f)
            {
                return {};
            }

            const float invLength = 1.0f / std::sqrt(lengthSq);
            return value * invLength;
        }
    }

    EditorViewportController::CameraState& EditorViewportController::Camera()
    {
        return m_Camera;
    }

    const EditorViewportController::CameraState& EditorViewportController::Camera() const
    {
        return m_Camera;
    }

    bool& EditorViewportController::ShowGrid()
    {
        return m_ShowGrid;
    }

    bool EditorViewportController::ShowGrid() const
    {
        return m_ShowGrid;
    }

    bool& EditorViewportController::ShowCameraDebugOverlay()
    {
        return m_ShowCameraDebugOverlay;
    }

    bool EditorViewportController::ShowCameraDebugOverlay() const
    {
        return m_ShowCameraDebugOverlay;
    }

    bool& EditorViewportController::PreviewSceneCameraLens()
    {
        return m_PreviewSceneCameraLens;
    }

    bool EditorViewportController::PreviewSceneCameraLens() const
    {
        return m_PreviewSceneCameraLens;
    }

    bool& EditorViewportController::SelectToolActive()
    {
        return m_ViewportSelectTool;
    }

    bool EditorViewportController::SelectToolActive() const
    {
        return m_ViewportSelectTool;
    }

    bool& EditorViewportController::MarqueeSelecting()
    {
        return m_ViewportMarqueeSelecting;
    }

    bool EditorViewportController::MarqueeSelecting() const
    {
        return m_ViewportMarqueeSelecting;
    }

    std::array<float, 2>& EditorViewportController::MarqueeStart()
    {
        return m_ViewportMarqueeStart;
    }

    const std::array<float, 2>& EditorViewportController::MarqueeStart() const
    {
        return m_ViewportMarqueeStart;
    }

    std::array<float, 2>& EditorViewportController::MarqueeCurrent()
    {
        return m_ViewportMarqueeCurrent;
    }

    const std::array<float, 2>& EditorViewportController::MarqueeCurrent() const
    {
        return m_ViewportMarqueeCurrent;
    }

    EditorViewportController::GizmoOperation& EditorViewportController::ActiveGizmoOperation()
    {
        return m_GizmoOperation;
    }

    EditorViewportController::GizmoOperation EditorViewportController::ActiveGizmoOperation() const
    {
        return m_GizmoOperation;
    }

    bool& EditorViewportController::GizmoLocalSpace()
    {
        return m_GizmoLocalSpace;
    }

    bool EditorViewportController::GizmoLocalSpace() const
    {
        return m_GizmoLocalSpace;
    }

    bool& EditorViewportController::GizmoSnapEnabled()
    {
        return m_GizmoSnapEnabled;
    }

    bool EditorViewportController::GizmoSnapEnabled() const
    {
        return m_GizmoSnapEnabled;
    }

    std::array<float, 3>& EditorViewportController::GizmoTranslateSnap()
    {
        return m_GizmoTranslateSnap;
    }

    const std::array<float, 3>& EditorViewportController::GizmoTranslateSnap() const
    {
        return m_GizmoTranslateSnap;
    }

    float& EditorViewportController::GizmoRotateSnap()
    {
        return m_GizmoRotateSnap;
    }

    float EditorViewportController::GizmoRotateSnap() const
    {
        return m_GizmoRotateSnap;
    }

    std::array<float, 3>& EditorViewportController::GizmoScaleSnap()
    {
        return m_GizmoScaleSnap;
    }

    const std::array<float, 3>& EditorViewportController::GizmoScaleSnap() const
    {
        return m_GizmoScaleSnap;
    }

    EntityID EditorViewportController::LensSourceEntity() const
    {
        return m_ViewportLensSourceEntity;
    }

    void EditorViewportController::SetLensSourceEntity(const EntityID entity)
    {
        m_ViewportLensSourceEntity = entity;
    }

    bool EditorViewportController::ViewportFocused() const
    {
        return m_ViewportFocused;
    }

    bool EditorViewportController::ViewportHovered() const
    {
        return m_ViewportHovered;
    }

    void EditorViewportController::SetViewportWindowState(
        const bool focused,
        const bool hovered,
        const float width,
        const float height)
    {
        m_ViewportFocused = focused;
        m_ViewportHovered = hovered;
        m_ViewportWidth = width;
        m_ViewportHeight = height;
    }

    float EditorViewportController::ViewportWidth() const
    {
        return m_ViewportWidth;
    }

    float EditorViewportController::ViewportHeight() const
    {
        return m_ViewportHeight;
    }

    bool EditorViewportController::GizmoInteracting() const
    {
        return m_GizmoInteracting;
    }

    void EditorViewportController::SetGizmoInteracting(const bool interacting)
    {
        m_GizmoInteracting = interacting;
    }

    bool EditorViewportController::RightMousePressed() const
    {
        return m_RightMousePressed;
    }

    bool EditorViewportController::MiddleMousePressed() const
    {
        return m_MiddleMousePressed;
    }

    void EditorViewportController::ClearViewportInteraction()
    {
        m_RightMousePressed = false;
        m_MiddleMousePressed = false;
        m_LastRightMouseState = false;
        m_LastMiddleMouseState = false;
        m_HasViewportMouseSample = false;
        m_GizmoInteracting = false;
        m_ViewportMarqueeSelecting = false;
    }

    void EditorViewportController::ResetCameraToDefault()
    {
        m_Camera = CameraState {};
        m_Camera.position = { 0.0f, 0.8f, 5.5f };
        m_Camera.pitch = 0.0f;
        m_Camera.yaw = -90.0f;
        const Vec3 cameraPosition {
            m_Camera.position[0],
            m_Camera.position[1],
            m_Camera.position[2]
        };
        m_Camera.focusDistance = std::clamp(
            std::sqrt(std::max(Dot(cameraPosition, cameraPosition), 0.0f)),
            0.5f,
            10000.0f);
    }

    std::array<float, 3> EditorViewportController::ComputeDropSpawnPosition() const
    {
        constexpr float kPi = 3.14159265359f;
        const float yawRadians = m_Camera.yaw * (kPi / 180.0f);
        const float pitchRadians = m_Camera.pitch * (kPi / 180.0f);
        const Vec3 eye {
            m_Camera.position[0],
            m_Camera.position[1],
            m_Camera.position[2]
        };
        const Vec3 forward = Normalize({
            std::cos(yawRadians) * std::cos(pitchRadians),
            std::sin(pitchRadians),
            std::sin(yawRadians) * std::cos(pitchRadians)
        });
        const float spawnDistance = std::clamp(m_Camera.focusDistance, 2.5f, 12.0f);
        const Vec3 spawnPosition = eye + forward * spawnDistance;
        return { spawnPosition.x, std::max(spawnPosition.y, 0.0f), spawnPosition.z };
    }

    void EditorViewportController::ApplyToolHotkeys(const bool allowHotkeys)
    {
        if (!allowHotkeys)
        {
            return;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Q))
        {
            m_ViewportSelectTool = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_W))
        {
            m_ViewportSelectTool = false;
            m_GizmoOperation = GizmoOperation::Translate;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E))
        {
            m_ViewportSelectTool = false;
            m_GizmoOperation = GizmoOperation::Rotate;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R))
        {
            m_ViewportSelectTool = false;
            m_GizmoOperation = GizmoOperation::Scale;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_X))
        {
            m_GizmoLocalSpace = !m_GizmoLocalSpace;
        }
    }

    void EditorViewportController::HandleCameraInput(const float deltaTimeSeconds)
    {
        const bool isMouseOverViewport = m_ViewportHovered;
        const RawInputState rawInput = Input::GetRawState();
        const bool currentRightMouseState = Input::IsMouseButtonDown(MouseButton::Right);
        const bool currentMiddleMouseState = Input::IsMouseButtonDown(MouseButton::Middle);

        if (isMouseOverViewport)
        {
            if (currentRightMouseState && !m_LastRightMouseState)
            {
                m_RightMousePressed = true;
                m_LastViewportMouseX = rawInput.mouseX;
                m_LastViewportMouseY = rawInput.mouseY;
                m_HasViewportMouseSample = true;
            }
            else if (!currentRightMouseState && m_LastRightMouseState)
            {
                m_RightMousePressed = false;
            }

            if (currentMiddleMouseState && !m_LastMiddleMouseState)
            {
                m_MiddleMousePressed = true;
                m_LastViewportMouseX = rawInput.mouseX;
                m_LastViewportMouseY = rawInput.mouseY;
                m_HasViewportMouseSample = true;
            }
            else if (!currentMiddleMouseState && m_LastMiddleMouseState)
            {
                m_MiddleMousePressed = false;
            }
        }
        else
        {
            m_RightMousePressed = false;
            m_MiddleMousePressed = false;
            m_HasViewportMouseSample = false;
        }

        m_LastRightMouseState = currentRightMouseState;
        m_LastMiddleMouseState = currentMiddleMouseState;

        float lookX = 0.0f;
        float lookY = 0.0f;
        if ((m_RightMousePressed || m_MiddleMousePressed) && isMouseOverViewport)
        {
            if (!m_HasViewportMouseSample)
            {
                m_LastViewportMouseX = rawInput.mouseX;
                m_LastViewportMouseY = rawInput.mouseY;
                m_HasViewportMouseSample = true;
            }
            lookX = static_cast<float>(rawInput.mouseX - m_LastViewportMouseX);
            lookY = static_cast<float>(m_LastViewportMouseY - rawInput.mouseY);
            m_LastViewportMouseX = rawInput.mouseX;
            m_LastViewportMouseY = rawInput.mouseY;
        }
        else if (isMouseOverViewport)
        {
            m_LastViewportMouseX = rawInput.mouseX;
            m_LastViewportMouseY = rawInput.mouseY;
            m_HasViewportMouseSample = true;
        }

        const ImGuiIO& io = ImGui::GetIO();
        const bool blockCameraInputForGizmo = isMouseOverViewport && m_GizmoInteracting;
        const bool allowKeyboardMove =
            isMouseOverViewport &&
            m_RightMousePressed &&
            !io.WantCaptureKeyboard &&
            !blockCameraInputForGizmo;

        float moveStep = m_Camera.moveSpeed * std::max(deltaTimeSeconds, 1.0e-4f);
        if (isMouseOverViewport && m_RightMousePressed)
        {
            moveStep *= 3.75f;
        }
        if (Input::IsKeyDown(KeyCode::LeftShift) || Input::IsKeyDown(KeyCode::RightShift))
        {
            moveStep *= 2.0f;
        }

        const Vec3 worldUp { 0.0f, 1.0f, 0.0f };
        Vec3 position {
            m_Camera.position[0],
            m_Camera.position[1],
            m_Camera.position[2]
        };

        bool cameraChanged = false;
        if (m_RightMousePressed && isMouseOverViewport && !blockCameraInputForGizmo)
        {
            m_Camera.yaw += lookX * m_Camera.lookSensitivity;
            m_Camera.pitch += lookY * m_Camera.lookSensitivity;
            m_Camera.pitch = std::clamp(m_Camera.pitch, -89.0f, 89.0f);
            if (m_Camera.yaw > 180.0f || m_Camera.yaw < -180.0f)
            {
                m_Camera.yaw = std::remainder(m_Camera.yaw, 360.0f);
            }
            cameraChanged = true;
        }

        const float yawRadians = m_Camera.yaw * (3.14159265359f / 180.0f);
        const float pitchRadians = m_Camera.pitch * (3.14159265359f / 180.0f);
        const Vec3 forward = Normalize({
            std::cos(yawRadians) * std::cos(pitchRadians),
            std::sin(pitchRadians),
            std::sin(yawRadians) * std::cos(pitchRadians)
        });
        const Vec3 right = Normalize(Cross(forward, worldUp));
        const Vec3 up = Normalize(Cross(right, forward));

        if (allowKeyboardMove)
        {
            if (Input::IsKeyDown(KeyCode::W))
            {
                position = position + forward * moveStep;
                cameraChanged = true;
            }
            if (Input::IsKeyDown(KeyCode::S))
            {
                position = position - forward * moveStep;
                cameraChanged = true;
            }
            if (Input::IsKeyDown(KeyCode::A))
            {
                position = position - right * moveStep;
                cameraChanged = true;
            }
            if (Input::IsKeyDown(KeyCode::D))
            {
                position = position + right * moveStep;
                cameraChanged = true;
            }
            if (Input::IsKeyDown(KeyCode::Q))
            {
                position = position - worldUp * moveStep;
                cameraChanged = true;
            }
            if (Input::IsKeyDown(KeyCode::E))
            {
                position = position + worldUp * moveStep;
                cameraChanged = true;
            }
        }

        if (!blockCameraInputForGizmo && isMouseOverViewport && std::abs(rawInput.mouseWheelY) > 1.0e-6f)
        {
            position = position + forward * (rawInput.mouseWheelY * m_Camera.zoomSpeed);
            cameraChanged = true;
        }

        if (!blockCameraInputForGizmo && m_MiddleMousePressed && isMouseOverViewport)
        {
            const float panScale = std::max(m_Camera.panSpeed, 1.0e-4f);
            position = position - right * (lookX * panScale);
            position = position + up * (lookY * panScale);
            cameraChanged = true;
        }

        if (!cameraChanged)
        {
            return;
        }

        m_Camera.position = { position.x, position.y, position.z };
        m_Camera.focusDistance = std::clamp(
            std::sqrt(std::max(Dot(position, position), 0.0f)),
            0.5f,
            10000.0f);
    }
}
