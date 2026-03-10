#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

struct GLFWwindow;

namespace Luma
{
    enum class KeyCode : std::uint16_t
    {
        Unknown = 0,
        W,
        A,
        S,
        D,
        Q,
        E,
        LeftShift,
        RightShift,
        Enter,
        Escape,
        Count
    };

    enum class MouseButton : std::uint8_t
    {
        Left,
        Right,
        Middle,
        Button4,
        Button5,
        Count
    };

    enum class MouseAxis : std::uint8_t
    {
        DeltaX = 0,
        DeltaY,
        WheelX,
        WheelY
    };

    enum class CursorMode : std::uint8_t
    {
        Normal,
        Disabled
    };

    struct MouseDelta
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    enum class InputDeviceType : std::uint8_t
    {
        Keyboard,
        Mouse
    };

    enum class InputControlType : std::uint8_t
    {
        Button,
        Axis1D
    };

    enum class InputTrigger : std::uint8_t
    {
        Down,
        Pressed,
        Released
    };

    enum class ActionValueType : std::uint8_t
    {
        Bool,
        Axis1D
    };

    struct InputBinding
    {
        InputDeviceType device = InputDeviceType::Keyboard;
        InputControlType control = InputControlType::Button;
        std::int32_t code = 0;
        bool useRawCode = false;
        InputTrigger trigger = InputTrigger::Down;
        float scale = 1.0f;
        float deadZone = 0.0f;

        static InputBinding Key(
            KeyCode key,
            InputTrigger trigger = InputTrigger::Down,
            float scale = 1.0f)
        {
            return {
                InputDeviceType::Keyboard,
                InputControlType::Button,
                static_cast<std::int32_t>(key),
                false,
                trigger,
                scale,
                0.0f
            };
        }

        static InputBinding Mouse(
            MouseButton button,
            InputTrigger trigger = InputTrigger::Down,
            float scale = 1.0f)
        {
            return {
                InputDeviceType::Mouse,
                InputControlType::Button,
                static_cast<std::int32_t>(button),
                false,
                trigger,
                scale,
                0.0f
            };
        }

        static InputBinding MouseAxis(
            MouseAxis axis,
            float scale = 1.0f,
            float deadZone = 0.0f)
        {
            return {
                InputDeviceType::Mouse,
                InputControlType::Axis1D,
                static_cast<std::int32_t>(axis),
                false,
                InputTrigger::Down,
                scale,
                deadZone
            };
        }
    };

    struct InputActionDesc
    {
        std::string name;
        ActionValueType valueType = ActionValueType::Bool;
        float activationThreshold = 0.5f;
    };

    struct InputContextDesc
    {
        std::string name;
        int priority = 0;
        bool enabled = true;
    };

    struct RawInputState
    {
        bool initialized = false;
        MouseDelta mouseDelta {};
        float mouseWheelX = 0.0f;
        float mouseWheelY = 0.0f;
        double mouseX = 0.0;
        double mouseY = 0.0;
    };

    class Input
    {
    public:
        static void Initialize(GLFWwindow* window);
        static void Shutdown();
        static void BeginFrame();

        static RawInputState GetRawState();
        static bool IsRawKeyDown(int key);
        static bool WasRawKeyPressed(int key);
        static bool WasRawKeyReleased(int key);
        static bool IsRawMouseButtonDown(int button);
        static bool WasRawMouseButtonPressed(int button);
        static bool WasRawMouseButtonReleased(int button);

        static bool IsKeyDown(KeyCode key);
        static bool WasKeyPressed(KeyCode key);
        static bool WasKeyReleased(KeyCode key);
        static bool IsMouseButtonDown(MouseButton button);
        static bool WasMouseButtonPressed(MouseButton button);
        static bool WasMouseButtonReleased(MouseButton button);
        static MouseDelta GetMouseDelta();
        static float GetMouseWheelDelta();
        static float GetMouseWheelDeltaX();
        static std::vector<std::string> ConsumeDroppedFiles();

        static bool RegisterAction(const InputActionDesc& action);
        static bool RegisterAction(
            std::string_view actionName,
            ActionValueType valueType = ActionValueType::Bool,
            float activationThreshold = 0.5f);
        static bool UnregisterAction(std::string_view actionName);
        static bool RegisterContext(const InputContextDesc& context);
        static bool RegisterContext(std::string_view contextName, int priority = 0, bool enabled = true);
        static bool RemoveContext(std::string_view contextName);
        static bool SetContextEnabled(std::string_view contextName, bool enabled);
        static bool SetContextPriority(std::string_view contextName, int priority);
        static bool BindAction(std::string_view contextName, std::string_view actionName, const InputBinding& binding);
        static bool RebindAction(
            std::string_view contextName,
            std::string_view actionName,
            std::size_t bindingIndex,
            const InputBinding& newBinding);
        static bool ClearActionBindings(std::string_view contextName, std::string_view actionName);
        static std::vector<InputBinding> GetActionBindings(
            std::string_view contextName,
            std::string_view actionName);
        static float GetActionValue(std::string_view actionName);
        static bool IsActionActive(std::string_view actionName);
        static bool WasActionStarted(std::string_view actionName);
        static bool WasActionCompleted(std::string_view actionName);
        static bool PollNextBinding(InputBinding& outBinding, bool includeAxes = true);
        static bool IsDeviceAvailable(InputDeviceType device);

        static void SetCursorMode(CursorMode mode);
        static CursorMode GetCursorMode();
        static bool IsInitialized();

    private:
        static int ToGlfwKey(KeyCode key);
        static int ToGlfwMouseButton(MouseButton button);
        static void ScrollCallback(GLFWwindow* window, double xoffset, double yoffset);
        static void DropCallback(GLFWwindow* window, int pathCount, const char** paths);
    };
}
