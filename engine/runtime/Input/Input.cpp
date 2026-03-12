#include "Luma/Input/Input.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <GLFW/glfw3.h>

namespace Luma
{
    namespace
    {
        constexpr int kRawKeyCount = GLFW_KEY_LAST + 1;
        constexpr int kRawMouseButtonCount = GLFW_MOUSE_BUTTON_LAST + 1;
        constexpr int kFirstPolledGlfwKey = GLFW_KEY_SPACE;
        constexpr float kMinActionThreshold = 1.0e-5f;

        struct ActionRuntimeState
        {
            InputActionDesc desc {};
            float value = 0.0f;
            float previousValue = 0.0f;
            bool active = false;
            bool started = false;
            bool completed = false;
        };

        struct ContextRuntimeState
        {
            struct EvaluatedActionState
            {
                float value = 0.0f;
                float previousValue = 0.0f;
                bool active = false;
                bool started = false;
                bool completed = false;
            };

            InputContextDesc desc {};
            std::size_t registrationOrder = 0;
            std::unordered_map<std::string, std::vector<InputBinding>> actionBindings {};
            std::unordered_map<std::string, EvaluatedActionState> evaluatedActions {};
        };

        GLFWwindow* g_Window = nullptr;
        GLFWscrollfun g_PreviousScrollCallback = nullptr;
        GLFWdropfun g_PreviousDropCallback = nullptr;
        std::vector<std::string> g_DroppedFiles {};

        bool g_Initialized = false;
        bool g_FirstMouseSample = true;
        double g_LastMouseX = 0.0;
        double g_LastMouseY = 0.0;
        double g_MouseX = 0.0;
        double g_MouseY = 0.0;
        MouseDelta g_MouseDelta {};
        float g_FrameWheelDeltaX = 0.0f;
        float g_FrameWheelDeltaY = 0.0f;
        float g_AccumulatedWheelDeltaX = 0.0f;
        float g_AccumulatedWheelDeltaY = 0.0f;
        CursorMode g_CursorMode = CursorMode::Normal;

        std::array<std::uint8_t, kRawKeyCount> g_KeyStateCurrent {};
        std::array<std::uint8_t, kRawKeyCount> g_KeyStatePrevious {};
        std::array<std::uint8_t, kRawMouseButtonCount> g_MouseStateCurrent {};
        std::array<std::uint8_t, kRawMouseButtonCount> g_MouseStatePrevious {};

        std::unordered_map<std::string, ActionRuntimeState> g_Actions {};
        std::unordered_map<std::string, ContextRuntimeState> g_Contexts {};
        std::size_t g_ContextRegistrationCounter = 0;

        bool IsValidRawKey(const int key)
        {
            return key >= 0 && key < kRawKeyCount;
        }

        bool IsValidRawMouseButton(const int button)
        {
            return button >= 0 && button < kRawMouseButtonCount;
        }

        int ToGlfwKeyCodeFromEnum(const KeyCode key)
        {
            switch (key)
            {
            case KeyCode::Space:
                return GLFW_KEY_SPACE;
            case KeyCode::C:
                return GLFW_KEY_C;
            case KeyCode::R:
                return GLFW_KEY_R;
            case KeyCode::W:
                return GLFW_KEY_W;
            case KeyCode::A:
                return GLFW_KEY_A;
            case KeyCode::S:
                return GLFW_KEY_S;
            case KeyCode::D:
                return GLFW_KEY_D;
            case KeyCode::Q:
                return GLFW_KEY_Q;
            case KeyCode::E:
                return GLFW_KEY_E;
            case KeyCode::LeftShift:
                return GLFW_KEY_LEFT_SHIFT;
            case KeyCode::RightShift:
                return GLFW_KEY_RIGHT_SHIFT;
            case KeyCode::Enter:
                return GLFW_KEY_ENTER;
            case KeyCode::Escape:
                return GLFW_KEY_ESCAPE;
            case KeyCode::Unknown:
            case KeyCode::Count:
            default:
                return GLFW_KEY_UNKNOWN;
            }
        }

        int ToGlfwMouseButtonFromEnum(const MouseButton button)
        {
            switch (button)
            {
            case MouseButton::Left:
                return GLFW_MOUSE_BUTTON_LEFT;
            case MouseButton::Right:
                return GLFW_MOUSE_BUTTON_RIGHT;
            case MouseButton::Middle:
                return GLFW_MOUSE_BUTTON_MIDDLE;
            case MouseButton::Button4:
                return GLFW_MOUSE_BUTTON_4;
            case MouseButton::Button5:
                return GLFW_MOUSE_BUTTON_5;
            case MouseButton::Count:
            default:
                return -1;
            }
        }

        const ActionRuntimeState* FindAction(std::string_view actionName)
        {
            const auto action = g_Actions.find(std::string(actionName));
            if (action == g_Actions.end())
            {
                return nullptr;
            }

            return &action->second;
        }

        const ContextRuntimeState* FindContext(std::string_view contextName)
        {
            const auto context = g_Contexts.find(std::string(contextName));
            if (context == g_Contexts.end())
            {
                return nullptr;
            }

            return &context->second;
        }

        const ContextRuntimeState::EvaluatedActionState* FindContextAction(
            std::string_view contextName,
            std::string_view actionName)
        {
            const ContextRuntimeState* context = FindContext(contextName);
            if (context == nullptr)
            {
                return nullptr;
            }

            const auto action = context->evaluatedActions.find(std::string(actionName));
            if (action == context->evaluatedActions.end())
            {
                return nullptr;
            }

            return &action->second;
        }

        ActionRuntimeState& EnsureAction(std::string_view actionName)
        {
            const std::string key(actionName);
            auto action = g_Actions.find(key);
            if (action == g_Actions.end())
            {
                ActionRuntimeState created {};
                created.desc.name = key;
                action = g_Actions.emplace(key, std::move(created)).first;
            }

            return action->second;
        }

        float ResolveButtonSignal(const bool down, const bool pressed, const bool released, const InputBinding& binding)
        {
            bool active = false;
            switch (binding.trigger)
            {
            case InputTrigger::Down:
                active = down;
                break;
            case InputTrigger::Pressed:
                active = pressed;
                break;
            case InputTrigger::Released:
                active = released;
                break;
            default:
                active = false;
                break;
            }

            return active ? binding.scale : 0.0f;
        }

        int ResolveKeyboardCode(const InputBinding& binding)
        {
            if (binding.useRawCode)
            {
                return binding.code;
            }

            if (binding.code < 0 || binding.code >= static_cast<int>(KeyCode::Count))
            {
                return GLFW_KEY_UNKNOWN;
            }

            return ToGlfwKeyCodeFromEnum(static_cast<KeyCode>(binding.code));
        }

        int ResolveMouseButtonCode(const InputBinding& binding)
        {
            if (binding.useRawCode)
            {
                return binding.code;
            }

            if (binding.code < 0 || binding.code >= static_cast<int>(MouseButton::Count))
            {
                return -1;
            }

            return ToGlfwMouseButtonFromEnum(static_cast<MouseButton>(binding.code));
        }

        float ResolveBindingValue(const InputBinding& binding)
        {
            if (binding.control == InputControlType::Button)
            {
                if (binding.device == InputDeviceType::Keyboard)
                {
                    const int rawKey = ResolveKeyboardCode(binding);
                    if (!IsValidRawKey(rawKey))
                    {
                        return 0.0f;
                    }

                    return ResolveButtonSignal(
                        g_KeyStateCurrent[static_cast<std::size_t>(rawKey)] != 0,
                        g_KeyStateCurrent[static_cast<std::size_t>(rawKey)] != 0 &&
                            g_KeyStatePrevious[static_cast<std::size_t>(rawKey)] == 0,
                        g_KeyStateCurrent[static_cast<std::size_t>(rawKey)] == 0 &&
                            g_KeyStatePrevious[static_cast<std::size_t>(rawKey)] != 0,
                        binding);
                }

                if (binding.device == InputDeviceType::Mouse)
                {
                    const int rawButton = ResolveMouseButtonCode(binding);
                    if (!IsValidRawMouseButton(rawButton))
                    {
                        return 0.0f;
                    }

                    return ResolveButtonSignal(
                        g_MouseStateCurrent[static_cast<std::size_t>(rawButton)] != 0,
                        g_MouseStateCurrent[static_cast<std::size_t>(rawButton)] != 0 &&
                            g_MouseStatePrevious[static_cast<std::size_t>(rawButton)] == 0,
                        g_MouseStateCurrent[static_cast<std::size_t>(rawButton)] == 0 &&
                            g_MouseStatePrevious[static_cast<std::size_t>(rawButton)] != 0,
                        binding);
                }

                return 0.0f;
            }

            if (binding.control == InputControlType::Axis1D && binding.device == InputDeviceType::Mouse)
            {
                float axis = 0.0f;
                switch (static_cast<MouseAxis>(binding.code))
                {
                case MouseAxis::DeltaX:
                    axis = g_MouseDelta.x;
                    break;
                case MouseAxis::DeltaY:
                    axis = g_MouseDelta.y;
                    break;
                case MouseAxis::WheelX:
                    axis = g_FrameWheelDeltaX;
                    break;
                case MouseAxis::WheelY:
                    axis = g_FrameWheelDeltaY;
                    break;
                default:
                    axis = 0.0f;
                    break;
                }

                if (std::abs(axis) < std::max(binding.deadZone, 0.0f))
                {
                    axis = 0.0f;
                }

                return axis * binding.scale;
            }

            return 0.0f;
        }

        void EvaluateActionStates()
        {
            for (auto& actionEntry : g_Actions)
            {
                ActionRuntimeState& action = actionEntry.second;
                action.previousValue = action.value;
                action.value = 0.0f;
                action.active = false;
                action.started = false;
                action.completed = false;
            }

            for (auto& contextEntry : g_Contexts)
            {
                ContextRuntimeState& context = contextEntry.second;
                for (auto& actionEntry : context.evaluatedActions)
                {
                    auto& action = actionEntry.second;
                    action.previousValue = action.value;
                    action.value = 0.0f;
                    action.active = false;
                    action.started = false;
                    action.completed = false;
                }
            }

            std::vector<ContextRuntimeState*> orderedContexts;
            orderedContexts.reserve(g_Contexts.size());
            for (auto& contextEntry : g_Contexts)
            {
                ContextRuntimeState& context = contextEntry.second;
                if (context.desc.enabled)
                {
                    orderedContexts.push_back(&context);
                }
            }

            std::sort(
                orderedContexts.begin(),
                orderedContexts.end(),
                [](const ContextRuntimeState* lhs, const ContextRuntimeState* rhs)
                {
                    if (lhs->desc.priority != rhs->desc.priority)
                    {
                        return lhs->desc.priority > rhs->desc.priority;
                    }
                    return lhs->registrationOrder < rhs->registrationOrder;
                });

            std::unordered_set<std::string> claimedActions;
            for (ContextRuntimeState* context : orderedContexts)
            {
                for (const auto& actionBindings : context->actionBindings)
                {
                    const std::string& actionName = actionBindings.first;
                    float contextValue = 0.0f;
                    for (const InputBinding& binding : actionBindings.second)
                    {
                        contextValue += ResolveBindingValue(binding);
                    }

                    ContextRuntimeState::EvaluatedActionState& evaluatedAction = context->evaluatedActions[actionName];
                    evaluatedAction.value = contextValue;

                    if (claimedActions.contains(actionName))
                    {
                        continue;
                    }

                    auto action = g_Actions.find(actionName);
                    if (action == g_Actions.end())
                    {
                        continue;
                    }

                    action->second.value = contextValue;
                    claimedActions.insert(actionName);
                }
            }

            for (auto& actionEntry : g_Actions)
            {
                ActionRuntimeState& action = actionEntry.second;
                const float threshold = std::max(action.desc.activationThreshold, kMinActionThreshold);
                const bool wasActive = std::abs(action.previousValue) >= threshold;
                const bool isActive = std::abs(action.value) >= threshold;
                action.active = isActive;
                action.started = !wasActive && isActive;
                action.completed = wasActive && !isActive;

                if (action.desc.valueType == ActionValueType::Bool)
                {
                    action.value = isActive ? (action.value < 0.0f ? -1.0f : 1.0f) : 0.0f;
                }
            }

            for (auto& contextEntry : g_Contexts)
            {
                ContextRuntimeState& context = contextEntry.second;
                for (auto& evaluatedEntry : context.evaluatedActions)
                {
                    auto action = g_Actions.find(evaluatedEntry.first);
                    if (action == g_Actions.end())
                    {
                        continue;
                    }

                    auto& evaluatedAction = evaluatedEntry.second;
                    const float threshold = std::max(action->second.desc.activationThreshold, kMinActionThreshold);
                    const bool wasActive = std::abs(evaluatedAction.previousValue) >= threshold;
                    const bool isActive = std::abs(evaluatedAction.value) >= threshold;
                    evaluatedAction.active = isActive;
                    evaluatedAction.started = !wasActive && isActive;
                    evaluatedAction.completed = wasActive && !isActive;

                    if (action->second.desc.valueType == ActionValueType::Bool)
                    {
                        evaluatedAction.value = isActive ? (evaluatedAction.value < 0.0f ? -1.0f : 1.0f) : 0.0f;
                    }
                }
            }
        }
    }

    void Input::Initialize(GLFWwindow* window)
    {
        if (g_Initialized)
        {
            Shutdown();
        }

        if (window == nullptr)
        {
            return;
        }

        g_Window = window;
        g_AccumulatedWheelDeltaX = 0.0f;
        g_AccumulatedWheelDeltaY = 0.0f;
        g_FrameWheelDeltaX = 0.0f;
        g_FrameWheelDeltaY = 0.0f;
        g_MouseDelta = {};
        g_FirstMouseSample = true;
        g_CursorMode = CursorMode::Normal;
        g_KeyStateCurrent.fill(0);
        g_KeyStatePrevious.fill(0);
        g_MouseStateCurrent.fill(0);
        g_MouseStatePrevious.fill(0);

        glfwGetCursorPos(g_Window, &g_MouseX, &g_MouseY);
        g_LastMouseX = g_MouseX;
        g_LastMouseY = g_MouseY;

        g_PreviousScrollCallback = glfwSetScrollCallback(g_Window, ScrollCallback);
        g_PreviousDropCallback = glfwSetDropCallback(g_Window, DropCallback);
        g_Initialized = true;
    }

    void Input::Shutdown()
    {
        if (!g_Initialized)
        {
            return;
        }

        if (g_Window != nullptr)
        {
            glfwSetScrollCallback(g_Window, g_PreviousScrollCallback);
            glfwSetDropCallback(g_Window, g_PreviousDropCallback);
        }

        g_PreviousScrollCallback = nullptr;
        g_PreviousDropCallback = nullptr;
        g_Window = nullptr;
        g_Initialized = false;
        g_MouseDelta = {};
        g_FrameWheelDeltaX = 0.0f;
        g_FrameWheelDeltaY = 0.0f;
        g_AccumulatedWheelDeltaX = 0.0f;
        g_AccumulatedWheelDeltaY = 0.0f;
        g_FirstMouseSample = true;
        g_KeyStateCurrent.fill(0);
        g_KeyStatePrevious.fill(0);
        g_MouseStateCurrent.fill(0);
        g_MouseStatePrevious.fill(0);
        g_Actions.clear();
        g_Contexts.clear();
        g_ContextRegistrationCounter = 0;
        g_DroppedFiles.clear();
    }

    void Input::BeginFrame()
    {
        if (!g_Initialized || g_Window == nullptr)
        {
            g_MouseDelta = {};
            g_FrameWheelDeltaX = 0.0f;
            g_FrameWheelDeltaY = 0.0f;
            EvaluateActionStates();
            return;
        }

        g_KeyStatePrevious = g_KeyStateCurrent;
        g_KeyStateCurrent.fill(0);
        for (int key = kFirstPolledGlfwKey; key < kRawKeyCount; ++key)
        {
            const int state = glfwGetKey(g_Window, key);
            g_KeyStateCurrent[static_cast<std::size_t>(key)] =
                (state == GLFW_PRESS || state == GLFW_REPEAT) ? 1 : 0;
        }

        g_MouseStatePrevious = g_MouseStateCurrent;
        g_MouseStateCurrent.fill(0);
        for (int button = 0; button < kRawMouseButtonCount; ++button)
        {
            g_MouseStateCurrent[static_cast<std::size_t>(button)] =
                glfwGetMouseButton(g_Window, button) == GLFW_PRESS ? 1 : 0;
        }

        glfwGetCursorPos(g_Window, &g_MouseX, &g_MouseY);
        if (g_FirstMouseSample)
        {
            g_MouseDelta = {};
            g_FirstMouseSample = false;
        }
        else
        {
            g_MouseDelta.x = static_cast<float>(g_MouseX - g_LastMouseX);
            g_MouseDelta.y = static_cast<float>(g_MouseY - g_LastMouseY);
        }

        g_LastMouseX = g_MouseX;
        g_LastMouseY = g_MouseY;

        g_FrameWheelDeltaX = g_AccumulatedWheelDeltaX;
        g_FrameWheelDeltaY = g_AccumulatedWheelDeltaY;
        g_AccumulatedWheelDeltaX = 0.0f;
        g_AccumulatedWheelDeltaY = 0.0f;

        EvaluateActionStates();
    }

    RawInputState Input::GetRawState()
    {
        RawInputState state;
        state.initialized = g_Initialized;
        state.mouseDelta = g_MouseDelta;
        state.mouseWheelX = g_FrameWheelDeltaX;
        state.mouseWheelY = g_FrameWheelDeltaY;
        state.mouseX = g_MouseX;
        state.mouseY = g_MouseY;
        return state;
    }

    bool Input::IsRawKeyDown(const int key)
    {
        return g_Initialized && IsValidRawKey(key) &&
               g_KeyStateCurrent[static_cast<std::size_t>(key)] != 0;
    }

    bool Input::WasRawKeyPressed(const int key)
    {
        return g_Initialized && IsValidRawKey(key) &&
               g_KeyStateCurrent[static_cast<std::size_t>(key)] != 0 &&
               g_KeyStatePrevious[static_cast<std::size_t>(key)] == 0;
    }

    bool Input::WasRawKeyReleased(const int key)
    {
        return g_Initialized && IsValidRawKey(key) &&
               g_KeyStateCurrent[static_cast<std::size_t>(key)] == 0 &&
               g_KeyStatePrevious[static_cast<std::size_t>(key)] != 0;
    }

    bool Input::IsRawMouseButtonDown(const int button)
    {
        return g_Initialized && IsValidRawMouseButton(button) &&
               g_MouseStateCurrent[static_cast<std::size_t>(button)] != 0;
    }

    bool Input::WasRawMouseButtonPressed(const int button)
    {
        return g_Initialized && IsValidRawMouseButton(button) &&
               g_MouseStateCurrent[static_cast<std::size_t>(button)] != 0 &&
               g_MouseStatePrevious[static_cast<std::size_t>(button)] == 0;
    }

    bool Input::WasRawMouseButtonReleased(const int button)
    {
        return g_Initialized && IsValidRawMouseButton(button) &&
               g_MouseStateCurrent[static_cast<std::size_t>(button)] == 0 &&
               g_MouseStatePrevious[static_cast<std::size_t>(button)] != 0;
    }

    bool Input::IsKeyDown(const KeyCode key)
    {
        const int rawKey = ToGlfwKey(key);
        return IsRawKeyDown(rawKey);
    }

    bool Input::WasKeyPressed(const KeyCode key)
    {
        const int rawKey = ToGlfwKey(key);
        return WasRawKeyPressed(rawKey);
    }

    bool Input::WasKeyReleased(const KeyCode key)
    {
        const int rawKey = ToGlfwKey(key);
        return WasRawKeyReleased(rawKey);
    }

    bool Input::IsMouseButtonDown(const MouseButton button)
    {
        const int rawButton = ToGlfwMouseButton(button);
        return IsRawMouseButtonDown(rawButton);
    }

    bool Input::WasMouseButtonPressed(const MouseButton button)
    {
        const int rawButton = ToGlfwMouseButton(button);
        return WasRawMouseButtonPressed(rawButton);
    }

    bool Input::WasMouseButtonReleased(const MouseButton button)
    {
        const int rawButton = ToGlfwMouseButton(button);
        return WasRawMouseButtonReleased(rawButton);
    }

    MouseDelta Input::GetMouseDelta()
    {
        return g_MouseDelta;
    }

    float Input::GetMouseWheelDelta()
    {
        return g_FrameWheelDeltaY;
    }

    float Input::GetMouseWheelDeltaX()
    {
        return g_FrameWheelDeltaX;
    }

    std::vector<std::string> Input::ConsumeDroppedFiles()
    {
        if (!g_Initialized)
        {
            return {};
        }

        std::vector<std::string> droppedFiles = std::move(g_DroppedFiles);
        g_DroppedFiles.clear();
        return droppedFiles;
    }

    bool Input::RegisterAction(const InputActionDesc& action)
    {
        if (action.name.empty())
        {
            return false;
        }

        ActionRuntimeState& runtime = EnsureAction(action.name);
        runtime.desc = action;
        runtime.desc.name = action.name;
        runtime.desc.activationThreshold = std::max(runtime.desc.activationThreshold, kMinActionThreshold);
        return true;
    }

    bool Input::RegisterAction(
        const std::string_view actionName,
        const ActionValueType valueType,
        const float activationThreshold)
    {
        if (actionName.empty())
        {
            return false;
        }

        InputActionDesc desc;
        desc.name = std::string(actionName);
        desc.valueType = valueType;
        desc.activationThreshold = activationThreshold;
        return RegisterAction(desc);
    }

    bool Input::UnregisterAction(const std::string_view actionName)
    {
        if (actionName.empty())
        {
            return false;
        }

        return g_Actions.erase(std::string(actionName)) > 0;
    }

    bool Input::RegisterContext(const InputContextDesc& context)
    {
        if (context.name.empty())
        {
            return false;
        }

        const std::string key = context.name;
        auto existing = g_Contexts.find(key);
        if (existing == g_Contexts.end())
        {
            ContextRuntimeState runtime {};
            runtime.desc = context;
            runtime.desc.name = key;
            runtime.registrationOrder = g_ContextRegistrationCounter++;
            g_Contexts.emplace(key, std::move(runtime));
            return true;
        }

        existing->second.desc.priority = context.priority;
        existing->second.desc.enabled = context.enabled;
        return true;
    }

    bool Input::RegisterContext(const std::string_view contextName, const int priority, const bool enabled)
    {
        if (contextName.empty())
        {
            return false;
        }

        InputContextDesc desc;
        desc.name = std::string(contextName);
        desc.priority = priority;
        desc.enabled = enabled;
        return RegisterContext(desc);
    }

    bool Input::RemoveContext(const std::string_view contextName)
    {
        if (contextName.empty())
        {
            return false;
        }

        return g_Contexts.erase(std::string(contextName)) > 0;
    }

    bool Input::SetContextEnabled(const std::string_view contextName, const bool enabled)
    {
        auto context = g_Contexts.find(std::string(contextName));
        if (context == g_Contexts.end())
        {
            return false;
        }

        context->second.desc.enabled = enabled;
        return true;
    }

    bool Input::SetContextPriority(const std::string_view contextName, const int priority)
    {
        auto context = g_Contexts.find(std::string(contextName));
        if (context == g_Contexts.end())
        {
            return false;
        }

        context->second.desc.priority = priority;
        return true;
    }

    bool Input::BindAction(
        const std::string_view contextName,
        const std::string_view actionName,
        const InputBinding& binding)
    {
        if (contextName.empty() || actionName.empty())
        {
            return false;
        }

        if (!g_Contexts.contains(std::string(contextName)))
        {
            RegisterContext(contextName);
        }
        EnsureAction(actionName);

        auto context = g_Contexts.find(std::string(contextName));
        if (context == g_Contexts.end())
        {
            return false;
        }

        context->second.actionBindings[std::string(actionName)].push_back(binding);
        return true;
    }

    bool Input::RebindAction(
        const std::string_view contextName,
        const std::string_view actionName,
        const std::size_t bindingIndex,
        const InputBinding& newBinding)
    {
        auto context = g_Contexts.find(std::string(contextName));
        if (context == g_Contexts.end())
        {
            return false;
        }

        auto actionBindings = context->second.actionBindings.find(std::string(actionName));
        if (actionBindings == context->second.actionBindings.end())
        {
            return false;
        }

        if (bindingIndex >= actionBindings->second.size())
        {
            return false;
        }

        actionBindings->second[bindingIndex] = newBinding;
        return true;
    }

    bool Input::ClearActionBindings(const std::string_view contextName, const std::string_view actionName)
    {
        auto context = g_Contexts.find(std::string(contextName));
        if (context == g_Contexts.end())
        {
            return false;
        }

        return context->second.actionBindings.erase(std::string(actionName)) > 0;
    }

    std::vector<InputBinding> Input::GetActionBindings(
        const std::string_view contextName,
        const std::string_view actionName)
    {
        const auto context = g_Contexts.find(std::string(contextName));
        if (context == g_Contexts.end())
        {
            return {};
        }

        const auto actionBindings = context->second.actionBindings.find(std::string(actionName));
        if (actionBindings == context->second.actionBindings.end())
        {
            return {};
        }

        return actionBindings->second;
    }

    std::vector<InputContextDesc> Input::GetRegisteredContexts()
    {
        std::vector<InputContextDesc> contexts;
        contexts.reserve(g_Contexts.size());
        for (const auto& [_, context] : g_Contexts)
        {
            contexts.push_back(context.desc);
        }

        std::sort(
            contexts.begin(),
            contexts.end(),
            [](const InputContextDesc& lhs, const InputContextDesc& rhs)
            {
                if (lhs.priority != rhs.priority)
                {
                    return lhs.priority > rhs.priority;
                }
                return lhs.name < rhs.name;
            });

        return contexts;
    }

    float Input::GetActionValue(const std::string_view actionName)
    {
        const ActionRuntimeState* action = FindAction(actionName);
        return action != nullptr ? action->value : 0.0f;
    }

    float Input::GetActionValue(const std::string_view contextName, const std::string_view actionName)
    {
        const ContextRuntimeState::EvaluatedActionState* action = FindContextAction(contextName, actionName);
        return action != nullptr ? action->value : 0.0f;
    }

    bool Input::IsActionActive(const std::string_view actionName)
    {
        const ActionRuntimeState* action = FindAction(actionName);
        return action != nullptr && action->active;
    }

    bool Input::IsActionActive(const std::string_view contextName, const std::string_view actionName)
    {
        const ContextRuntimeState::EvaluatedActionState* action = FindContextAction(contextName, actionName);
        return action != nullptr && action->active;
    }

    bool Input::WasActionStarted(const std::string_view actionName)
    {
        const ActionRuntimeState* action = FindAction(actionName);
        return action != nullptr && action->started;
    }

    bool Input::WasActionStarted(const std::string_view contextName, const std::string_view actionName)
    {
        const ContextRuntimeState::EvaluatedActionState* action = FindContextAction(contextName, actionName);
        return action != nullptr && action->started;
    }

    bool Input::WasActionCompleted(const std::string_view actionName)
    {
        const ActionRuntimeState* action = FindAction(actionName);
        return action != nullptr && action->completed;
    }

    bool Input::WasActionCompleted(const std::string_view contextName, const std::string_view actionName)
    {
        const ContextRuntimeState::EvaluatedActionState* action = FindContextAction(contextName, actionName);
        return action != nullptr && action->completed;
    }

    bool Input::PollNextBinding(InputBinding& outBinding, const bool includeAxes)
    {
        if (!g_Initialized)
        {
            return false;
        }

        for (int key = 0; key < kRawKeyCount; ++key)
        {
            if (WasRawKeyPressed(key))
            {
                outBinding.device = InputDeviceType::Keyboard;
                outBinding.control = InputControlType::Button;
                outBinding.code = key;
                outBinding.useRawCode = true;
                outBinding.trigger = InputTrigger::Down;
                outBinding.scale = 1.0f;
                outBinding.deadZone = 0.0f;
                return true;
            }
        }

        for (int button = 0; button < kRawMouseButtonCount; ++button)
        {
            if (WasRawMouseButtonPressed(button))
            {
                outBinding.device = InputDeviceType::Mouse;
                outBinding.control = InputControlType::Button;
                outBinding.code = button;
                outBinding.useRawCode = true;
                outBinding.trigger = InputTrigger::Down;
                outBinding.scale = 1.0f;
                outBinding.deadZone = 0.0f;
                return true;
            }
        }

        if (!includeAxes)
        {
            return false;
        }

        if (std::abs(g_FrameWheelDeltaY) > kMinActionThreshold)
        {
            outBinding = InputBinding::MouseAxis(MouseAxis::WheelY);
            return true;
        }

        if (std::abs(g_FrameWheelDeltaX) > kMinActionThreshold)
        {
            outBinding = InputBinding::MouseAxis(MouseAxis::WheelX);
            return true;
        }

        if (std::abs(g_MouseDelta.x) > kMinActionThreshold)
        {
            outBinding = InputBinding::MouseAxis(MouseAxis::DeltaX);
            return true;
        }

        if (std::abs(g_MouseDelta.y) > kMinActionThreshold)
        {
            outBinding = InputBinding::MouseAxis(MouseAxis::DeltaY);
            return true;
        }

        return false;
    }

    bool Input::IsDeviceAvailable(const InputDeviceType device)
    {
        if (!g_Initialized || g_Window == nullptr)
        {
            return false;
        }

        switch (device)
        {
        case InputDeviceType::Keyboard:
        case InputDeviceType::Mouse:
            return true;
        default:
            return false;
        }
    }

    void Input::SetCursorMode(const CursorMode mode)
    {
        if (!g_Initialized || g_Window == nullptr)
        {
            return;
        }

        if (g_CursorMode == mode)
        {
            return;
        }

        const int glfwMode = mode == CursorMode::Disabled
                                 ? GLFW_CURSOR_DISABLED
                                 : GLFW_CURSOR_NORMAL;
        glfwSetInputMode(g_Window, GLFW_CURSOR, glfwMode);
        g_CursorMode = mode;
        g_FirstMouseSample = true;
    }

    CursorMode Input::GetCursorMode()
    {
        return g_CursorMode;
    }

    bool Input::IsInitialized()
    {
        return g_Initialized;
    }

    int Input::ToGlfwKey(const KeyCode key)
    {
        return ToGlfwKeyCodeFromEnum(key);
    }

    int Input::ToGlfwMouseButton(const MouseButton button)
    {
        return ToGlfwMouseButtonFromEnum(button);
    }

    void Input::ScrollCallback(GLFWwindow* window, const double xoffset, const double yoffset)
    {
        g_AccumulatedWheelDeltaX += static_cast<float>(xoffset);
        g_AccumulatedWheelDeltaY += static_cast<float>(yoffset);

        if (g_PreviousScrollCallback != nullptr)
        {
            g_PreviousScrollCallback(window, xoffset, yoffset);
        }
    }

    void Input::DropCallback(GLFWwindow* window, const int pathCount, const char** paths)
    {
        if (paths != nullptr && pathCount > 0)
        {
            g_DroppedFiles.reserve(g_DroppedFiles.size() + static_cast<std::size_t>(pathCount));
            for (int i = 0; i < pathCount; ++i)
            {
                if (paths[i] != nullptr && paths[i][0] != '\0')
                {
                    g_DroppedFiles.emplace_back(paths[i]);
                }
            }
        }

        if (g_PreviousDropCallback != nullptr)
        {
            g_PreviousDropCallback(window, pathCount, paths);
        }
    }
}
