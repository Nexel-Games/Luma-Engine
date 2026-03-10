# Scene Camera Control Code Map

This document lists the exact code paths that control the scene/editor camera in the current project.

## 1) Camera State (owner of camera transform)

File: `include/Luma/Layers/TriangleLayer.h`

```cpp
struct EditorCameraState
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
EditorCameraState m_EditorCamera;
bool m_RightMousePressed = false;
bool m_MiddleMousePressed = false;
bool m_LastRightMouseState = false;
bool m_LastMiddleMouseState = false;
bool m_PreviewSceneCameraLens = true;
bool m_LockViewportToCamera = false;
EntityID m_ViewportLensSourceEntity = entt::null;
EntityID m_LockedViewportCameraEntity = entt::null;
EntityID m_LastOrbitSelectionEntity = entt::null;
```

Notes:
- `m_EditorCamera` stores runtime editor camera transform + persistent orbit distance (`focusDistance`).
- Lens source and view transform source are decoupled:
  - lens source: `m_ViewportLensSourceEntity`
  - optional locked view source: `m_LockedViewportCameraEntity` when `m_LockViewportToCamera == true`

## 2) Input Actions and Default Bindings

File: `src/Layers/TriangleLayer.cpp` (`ConfigureEditorInputBindings()`)

```cpp
Input::RegisterContext(kViewportInputContext, 150, false);

Input::RegisterAction(kActionLookHold, ActionValueType::Bool);
Input::RegisterAction(kActionPanHold, ActionValueType::Bool);
Input::RegisterAction(kActionLookX, ActionValueType::Axis1D, 1.0e-4f);
Input::RegisterAction(kActionLookY, ActionValueType::Axis1D, 1.0e-4f);
Input::RegisterAction(kActionMoveForward, ActionValueType::Bool);
Input::RegisterAction(kActionMoveBackward, ActionValueType::Bool);
Input::RegisterAction(kActionMoveLeft, ActionValueType::Bool);
Input::RegisterAction(kActionMoveRight, ActionValueType::Bool);
Input::RegisterAction(kActionMoveUp, ActionValueType::Bool);
Input::RegisterAction(kActionMoveDown, ActionValueType::Bool);
Input::RegisterAction(kActionMoveBoost, ActionValueType::Bool);
Input::RegisterAction(kActionZoom, ActionValueType::Axis1D, 1.0e-4f);

Input::BindAction(kViewportInputContext, kActionLookHold, InputBinding::Mouse(MouseButton::Right));
Input::BindAction(kViewportInputContext, kActionPanHold, InputBinding::Mouse(MouseButton::Middle));
Input::BindAction(kViewportInputContext, kActionLookX, InputBinding::MouseAxis(MouseAxis::DeltaX));
Input::BindAction(kViewportInputContext, kActionLookY, InputBinding::MouseAxis(MouseAxis::DeltaY));
Input::BindAction(kViewportInputContext, kActionMoveForward, InputBinding::Key(KeyCode::W));
Input::BindAction(kViewportInputContext, kActionMoveBackward, InputBinding::Key(KeyCode::S));
Input::BindAction(kViewportInputContext, kActionMoveLeft, InputBinding::Key(KeyCode::A));
Input::BindAction(kViewportInputContext, kActionMoveRight, InputBinding::Key(KeyCode::D));
Input::BindAction(kViewportInputContext, kActionMoveUp, InputBinding::Key(KeyCode::E));
Input::BindAction(kViewportInputContext, kActionMoveDown, InputBinding::Key(KeyCode::Q));
Input::BindAction(kViewportInputContext, kActionMoveBoost, InputBinding::Key(KeyCode::LeftShift));
Input::BindAction(kViewportInputContext, kActionMoveBoost, InputBinding::Key(KeyCode::RightShift));
Input::BindAction(kViewportInputContext, kActionZoom, InputBinding::MouseAxis(MouseAxis::WheelY));
```

## 3) Viewport Input Context Toggle

File: `src/Layers/TriangleLayer.cpp` (`SetViewportInputContextEnabled()`)

```cpp
void TriangleLayer::SetViewportInputContextEnabled(const bool enabled)
{
    if (m_ViewportInputContextEnabled == enabled)
    {
        return;
    }

    Input::SetContextEnabled(kViewportInputContext, enabled);
    m_ViewportInputContextEnabled = enabled;
}
```

## 4) Camera Update Per Frame (main control function)

File: `src/Layers/TriangleLayer.cpp` (`HandleViewportCameraInput()`)

This function currently handles:
- RMB/MMB transitions with first-frame delta suppression (no jump on click)
- action-driven mouse deltas (`kActionLookX` / `kActionLookY`)
- Yaw/pitch rotation
- Persistent orbit distance (`focusDistance`) for stable orbiting
- Orbit pivot selection (selected entity world position fallback)
- Dolly (`wheel` / forward-back), pan (`MMB`), and keyboard offset (`WASD/QE`)

Key rotation block:

```cpp
if (lookHeld)
{
    m_EditorCamera.yaw += lookX * m_EditorCamera.lookSensitivity;
    m_EditorCamera.pitch += lookY * m_EditorCamera.lookSensitivity;
    m_EditorCamera.pitch = std::clamp(m_EditorCamera.pitch, -89.0f, 89.0f);
    if (m_EditorCamera.yaw > 180.0f || m_EditorCamera.yaw < -180.0f)
    {
        m_EditorCamera.yaw = std::remainder(m_EditorCamera.yaw, 360.0f);
    }
    cameraChanged = true;
}
```

Final camera position write:

```cpp
const Vec3 orbitPosition = orbitPivot - forward * m_EditorCamera.focusDistance;
m_EditorCamera.position = { orbitPosition.x, orbitPosition.y, orbitPosition.z };
```

## 5) Render Uses Free/Locked View + Priority Lens Source

File: `src/Layers/TriangleLayer.cpp` (`OnRender()`)

The render path uses:
- Free view transform: `m_EditorCamera.position/yaw/pitch`
- Locked view transform: camera entity transform when `m_LockViewportToCamera == true`
- Lens source from `FindEditorCameraEntity()` priority chain
- Lens behavior:
  - free mode: editor near/far defaults, optional camera FOV preview
  - lock mode: camera FOV + near/far

```cpp
float viewYawDegrees = m_EditorCamera.yaw;
float viewPitchDegrees = m_EditorCamera.pitch;
Vec3 eye { ...editor camera position... };
if (m_LockViewportToCamera && validLockedCamera)
{
    eye = lockedCamera.worldPosition;
    viewPitchDegrees = lockedCamera.worldRotation[0];
    viewYawDegrees = lockedCamera.worldRotation[1];
}

const float yawRadians = viewYawDegrees * (kPi / 180.0f);
const float pitchRadians = viewPitchDegrees * (kPi / 180.0f);
const Vec3 forward = Normalize({
    std::cos(yawRadians) * std::cos(pitchRadians),
    std::sin(pitchRadians),
    std::sin(yawRadians) * std::cos(pitchRadians)
});
const Mat4 view = BuildLookAt(eye, eye + forward, Vec3 { 0.0f, 1.0f, 0.0f });
```

## 6) Startup / Scene Boot Camera Seed

File: `src/Layers/TriangleLayer.cpp` (`SeedDefaultSceneEntities()`)

If scene already has entities:
- Finds best camera entity (same priority chain as lens source)
- Seeds `m_EditorCamera` from that entity transform once

If scene is empty:
- Creates default `Camera` entity
- Sets transform (`position`, `rotation`)
- Seeds `m_EditorCamera`, `m_EditorCameraEntity`, and `focusDistance`

## 7) Camera Entity Lookup

File: `src/Layers/TriangleLayer.cpp` (`FindEditorCameraEntity()`)

```cpp
EntityID TriangleLayer::FindEditorCameraEntity() const
{
    auto isCameraEntity = [&](EntityID e) { ... };

    if (m_LockViewportToCamera && isCameraEntity(m_LockedViewportCameraEntity))
    {
        return m_LockedViewportCameraEntity;
    }
    if (isCameraEntity(m_SelectedEntity))
    {
        return m_SelectedEntity;
    }

    const auto view = registry.view<TransformComponent, CameraComponent>();
    for (const EntityID entity : view)
    {
        if (view.get<CameraComponent>(entity).primary)
        {
            return entity;
        }
    }
    for (const EntityID entity : view) { return entity; }

    return entt::null;
}
```

## Quick Summary

- Transform control is in `m_EditorCamera` (editor runtime state).
- Input control is in `ConfigureEditorInputBindings()` + `HandleViewportCameraInput()`.
- Render consumes free or locked view transform in `OnRender()`.
- Lens source selection is priority-based (locked camera -> selected camera -> primary camera -> first camera).
