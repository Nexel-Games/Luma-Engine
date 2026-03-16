#include "Luma/Editor/Core/GameplayInputBindingService.h"

#include <iterator>

#include "Luma/Input/Input.h"
#include "Luma/Scene/VehicleInputActions.h"

namespace Luma::Editor
{
    namespace
    {
        constexpr std::string_view kGameplayInputContext = "Gameplay.Default";
        constexpr std::string_view kGameplayActionMoveForward = "Gameplay.MoveForward";
        constexpr std::string_view kGameplayActionMoveBackward = "Gameplay.MoveBackward";
        constexpr std::string_view kGameplayActionMoveLeft = "Gameplay.MoveLeft";
        constexpr std::string_view kGameplayActionMoveRight = "Gameplay.MoveRight";
        constexpr std::string_view kGameplayActionSprint = "Gameplay.Sprint";
        constexpr std::string_view kGameplayActionLookX = "Gameplay.LookX";
        constexpr std::string_view kGameplayActionLookY = "Gameplay.LookY";
        constexpr std::string_view kGameplayActionPrimaryFire = "Gameplay.PrimaryFire";
        constexpr std::string_view kGameplayActionAim = "Gameplay.Aim";
        constexpr InputActionSettingsEntry kDefaultInputActionSettingsEntries[] = {
            InputActionSettingsEntry { "Move Forward", kGameplayInputContext, kGameplayActionMoveForward, false },
            InputActionSettingsEntry { "Move Backward", kGameplayInputContext, kGameplayActionMoveBackward, false },
            InputActionSettingsEntry { "Move Left", kGameplayInputContext, kGameplayActionMoveLeft, false },
            InputActionSettingsEntry { "Move Right", kGameplayInputContext, kGameplayActionMoveRight, false },
            InputActionSettingsEntry { "Sprint", kGameplayInputContext, kGameplayActionSprint, false },
            InputActionSettingsEntry { "Look X", kGameplayInputContext, kGameplayActionLookX, true },
            InputActionSettingsEntry { "Look Y", kGameplayInputContext, kGameplayActionLookY, true },
            InputActionSettingsEntry { "Primary Fire", kGameplayInputContext, kGameplayActionPrimaryFire, false },
            InputActionSettingsEntry { "Aim", kGameplayInputContext, kGameplayActionAim, false },
            InputActionSettingsEntry { "Vehicle Throttle", VehicleInputActions::Context, VehicleInputActions::Throttle, false },
            InputActionSettingsEntry { "Vehicle Steer", VehicleInputActions::Context, VehicleInputActions::Steer, false },
            InputActionSettingsEntry { "Vehicle Brake", VehicleInputActions::Context, VehicleInputActions::Brake, false },
            InputActionSettingsEntry { "Vehicle Handbrake", VehicleInputActions::Context, VehicleInputActions::Handbrake, false },
            InputActionSettingsEntry { "Vehicle Clutch", VehicleInputActions::Context, VehicleInputActions::Clutch, false },
            InputActionSettingsEntry { "Vehicle Gear Up", VehicleInputActions::Context, VehicleInputActions::GearUp, false },
            InputActionSettingsEntry { "Vehicle Gear Down", VehicleInputActions::Context, VehicleInputActions::GearDown, false },
            InputActionSettingsEntry { "Vehicle Reset", VehicleInputActions::Context, VehicleInputActions::Reset, false }
        };
    }

    void GameplayInputBindingService::ConfigureEditorGameplayDefaults() const
    {
        Input::RegisterContext(kGameplayInputContext, 40, true);
        Input::RegisterContext(VehicleInputActions::Context, 35, true);

        Input::RegisterAction(kGameplayActionMoveForward, ActionValueType::Bool);
        Input::RegisterAction(kGameplayActionMoveBackward, ActionValueType::Bool);
        Input::RegisterAction(kGameplayActionMoveLeft, ActionValueType::Bool);
        Input::RegisterAction(kGameplayActionMoveRight, ActionValueType::Bool);
        Input::RegisterAction(kGameplayActionSprint, ActionValueType::Bool);
        Input::RegisterAction(kGameplayActionLookX, ActionValueType::Axis1D, 1.0e-4f);
        Input::RegisterAction(kGameplayActionLookY, ActionValueType::Axis1D, 1.0e-4f);
        Input::RegisterAction(kGameplayActionPrimaryFire, ActionValueType::Bool);
        Input::RegisterAction(kGameplayActionAim, ActionValueType::Bool);
        Input::RegisterAction(VehicleInputActions::Throttle, ActionValueType::Axis1D, 1.0e-4f);
        Input::RegisterAction(VehicleInputActions::Steer, ActionValueType::Axis1D, 1.0e-4f);
        Input::RegisterAction(VehicleInputActions::Brake, ActionValueType::Bool);
        Input::RegisterAction(VehicleInputActions::Handbrake, ActionValueType::Bool);
        Input::RegisterAction(VehicleInputActions::Clutch, ActionValueType::Bool);
        Input::RegisterAction(VehicleInputActions::GearUp, ActionValueType::Bool);
        Input::RegisterAction(VehicleInputActions::GearDown, ActionValueType::Bool);
        Input::RegisterAction(VehicleInputActions::Reset, ActionValueType::Bool);

        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionMoveForward);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionMoveBackward);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionMoveLeft);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionMoveRight);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionSprint);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionLookX);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionLookY);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionPrimaryFire);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionAim);
        Input::ClearActionBindings(VehicleInputActions::Context, VehicleInputActions::Throttle);
        Input::ClearActionBindings(VehicleInputActions::Context, VehicleInputActions::Steer);
        Input::ClearActionBindings(VehicleInputActions::Context, VehicleInputActions::Brake);
        Input::ClearActionBindings(VehicleInputActions::Context, VehicleInputActions::Handbrake);
        Input::ClearActionBindings(VehicleInputActions::Context, VehicleInputActions::Clutch);
        Input::ClearActionBindings(VehicleInputActions::Context, VehicleInputActions::GearUp);
        Input::ClearActionBindings(VehicleInputActions::Context, VehicleInputActions::GearDown);
        Input::ClearActionBindings(VehicleInputActions::Context, VehicleInputActions::Reset);

        Input::BindAction(kGameplayInputContext, kGameplayActionMoveForward, InputBinding::Key(KeyCode::W));
        Input::BindAction(kGameplayInputContext, kGameplayActionMoveBackward, InputBinding::Key(KeyCode::S));
        Input::BindAction(kGameplayInputContext, kGameplayActionMoveLeft, InputBinding::Key(KeyCode::A));
        Input::BindAction(kGameplayInputContext, kGameplayActionMoveRight, InputBinding::Key(KeyCode::D));
        Input::BindAction(kGameplayInputContext, kGameplayActionSprint, InputBinding::Key(KeyCode::LeftShift));
        Input::BindAction(kGameplayInputContext, kGameplayActionSprint, InputBinding::Key(KeyCode::RightShift));
        Input::BindAction(kGameplayInputContext, kGameplayActionLookX, InputBinding::MouseAxis(MouseAxis::DeltaX));
        Input::BindAction(kGameplayInputContext, kGameplayActionLookY, InputBinding::MouseAxis(MouseAxis::DeltaY));
        Input::BindAction(kGameplayInputContext, kGameplayActionPrimaryFire, InputBinding::Mouse(MouseButton::Left));
        Input::BindAction(kGameplayInputContext, kGameplayActionAim, InputBinding::Mouse(MouseButton::Right));

        Input::BindAction(VehicleInputActions::Context, VehicleInputActions::Throttle, InputBinding::Key(KeyCode::W, InputTrigger::Down, 1.0f));
        Input::BindAction(VehicleInputActions::Context, VehicleInputActions::Throttle, InputBinding::Key(KeyCode::S, InputTrigger::Down, -1.0f));
        Input::BindAction(VehicleInputActions::Context, VehicleInputActions::Steer, InputBinding::Key(KeyCode::D, InputTrigger::Down, 1.0f));
        Input::BindAction(VehicleInputActions::Context, VehicleInputActions::Steer, InputBinding::Key(KeyCode::A, InputTrigger::Down, -1.0f));
        Input::BindAction(VehicleInputActions::Context, VehicleInputActions::Brake, InputBinding::Key(KeyCode::Space));
        Input::BindAction(VehicleInputActions::Context, VehicleInputActions::Handbrake, InputBinding::Key(KeyCode::LeftShift));
        Input::BindAction(VehicleInputActions::Context, VehicleInputActions::Handbrake, InputBinding::Key(KeyCode::RightShift));
        Input::BindAction(VehicleInputActions::Context, VehicleInputActions::Clutch, InputBinding::Key(KeyCode::C));
        Input::BindAction(VehicleInputActions::Context, VehicleInputActions::GearUp, InputBinding::Key(KeyCode::E, InputTrigger::Pressed));
        Input::BindAction(VehicleInputActions::Context, VehicleInputActions::GearDown, InputBinding::Key(KeyCode::Q, InputTrigger::Pressed));
        Input::BindAction(VehicleInputActions::Context, VehicleInputActions::Reset, InputBinding::Key(KeyCode::R, InputTrigger::Pressed));
    }

    const char* GameplayInputBindingService::GetGameplayInputContextName()
    {
        return kGameplayInputContext.data();
    }

    const InputActionSettingsEntry* GameplayInputBindingService::GetDefaultInputActionSettingsEntries(std::size_t& outCount)
    {
        outCount = std::size(kDefaultInputActionSettingsEntries);
        return kDefaultInputActionSettingsEntries;
    }

    bool GameplayInputBindingService::IsVehicleInputAction(const std::string_view actionName)
    {
        return actionName == VehicleInputActions::Throttle ||
            actionName == VehicleInputActions::Steer ||
            actionName == VehicleInputActions::Brake ||
            actionName == VehicleInputActions::Handbrake ||
            actionName == VehicleInputActions::Clutch ||
            actionName == VehicleInputActions::GearUp ||
            actionName == VehicleInputActions::GearDown ||
            actionName == VehicleInputActions::Reset;
    }

    void GameplayInputBindingService::ResetVehicleInputBindings(const std::string_view contextName)
    {
        const std::string_view targetContext = contextName.empty() ? VehicleInputActions::Context : contextName;
        Input::RegisterContext(targetContext, 35, true);

        Input::ClearActionBindings(targetContext, VehicleInputActions::Throttle);
        Input::ClearActionBindings(targetContext, VehicleInputActions::Steer);
        Input::ClearActionBindings(targetContext, VehicleInputActions::Brake);
        Input::ClearActionBindings(targetContext, VehicleInputActions::Handbrake);
        Input::ClearActionBindings(targetContext, VehicleInputActions::Clutch);
        Input::ClearActionBindings(targetContext, VehicleInputActions::GearUp);
        Input::ClearActionBindings(targetContext, VehicleInputActions::GearDown);
        Input::ClearActionBindings(targetContext, VehicleInputActions::Reset);

        for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::Throttle))
        {
            Input::BindAction(targetContext, VehicleInputActions::Throttle, binding);
        }
        for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::Steer))
        {
            Input::BindAction(targetContext, VehicleInputActions::Steer, binding);
        }
        for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::Brake))
        {
            Input::BindAction(targetContext, VehicleInputActions::Brake, binding);
        }
        for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::Handbrake))
        {
            Input::BindAction(targetContext, VehicleInputActions::Handbrake, binding);
        }
        for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::Clutch))
        {
            Input::BindAction(targetContext, VehicleInputActions::Clutch, binding);
        }
        for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::GearUp))
        {
            Input::BindAction(targetContext, VehicleInputActions::GearUp, binding);
        }
        for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::GearDown))
        {
            Input::BindAction(targetContext, VehicleInputActions::GearDown, binding);
        }
        for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::Reset))
        {
            Input::BindAction(targetContext, VehicleInputActions::Reset, binding);
        }
    }
}
