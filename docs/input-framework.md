# Input Framework API

The `Luma::Input` system now supports:

- Raw input polling (keyboard, mouse buttons, mouse delta, mouse wheel)
- Action mapping (`InputActionDesc`)
- Context layering with priority (`InputContextDesc`)
- Device abstraction (`InputBinding` for keyboard/mouse button/axis)
- Runtime rebinding (`RebindAction`, `PollNextBinding`)

## Raw Input

```cpp
Input::BeginFrame();

bool wDown = Input::IsKeyDown(KeyCode::W);
bool rightPressed = Input::WasMouseButtonPressed(MouseButton::Right);
MouseDelta delta = Input::GetMouseDelta();
float wheel = Input::GetMouseWheelDelta();
```

You can also query raw GLFW codes directly:

```cpp
bool keyDown = Input::IsRawKeyDown(GLFW_KEY_F);
bool mousePressed = Input::WasRawMouseButtonPressed(GLFW_MOUSE_BUTTON_4);
```

## Actions and Contexts

```cpp
Input::RegisterContext("Editor.Viewport", 150, true);
Input::RegisterAction("Editor.MoveForward", ActionValueType::Bool);
Input::RegisterAction("Editor.LookX", ActionValueType::Axis1D, 1.0e-4f);

Input::BindAction("Editor.Viewport", "Editor.MoveForward", InputBinding::Key(KeyCode::W));
Input::BindAction("Editor.Viewport", "Editor.LookX", InputBinding::MouseAxis(MouseAxis::DeltaX));
```

Query action state:

```cpp
float lookX = Input::GetActionValue("Editor.LookX");
bool moving = Input::IsActionActive("Editor.MoveForward");
bool started = Input::WasActionStarted("Editor.MoveForward");
bool ended = Input::WasActionCompleted("Editor.MoveForward");
```

## Layered Context Behavior

- Contexts are evaluated by `priority` (higher first).
- If two active contexts bind the same action, the higher-priority context owns it.
- Enable/disable contexts at runtime:

```cpp
Input::SetContextEnabled("Editor.Viewport", false);
```

## Rebinding

Replace an existing binding:

```cpp
Input::RebindAction("Editor.Viewport", "Editor.MoveForward", 0, InputBinding::Key(KeyCode::D));
```

Capture the next user input for rebinding:

```cpp
InputBinding captured;
if (Input::PollNextBinding(captured, true))
{
    Input::RebindAction("Editor.Viewport", "Editor.LookX", 0, captured);
}
```
