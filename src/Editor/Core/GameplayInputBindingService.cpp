#include "Luma/Editor/Core/GameplayInputBindingService.h"

#include "Luma/Input/Input.h"

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
    }

    void GameplayInputBindingService::ConfigureEditorGameplayDefaults() const
    {
        Input::RegisterContext(kGameplayInputContext, 40, true);

        Input::RegisterAction(kGameplayActionMoveForward, ActionValueType::Bool);
        Input::RegisterAction(kGameplayActionMoveBackward, ActionValueType::Bool);
        Input::RegisterAction(kGameplayActionMoveLeft, ActionValueType::Bool);
        Input::RegisterAction(kGameplayActionMoveRight, ActionValueType::Bool);
        Input::RegisterAction(kGameplayActionSprint, ActionValueType::Bool);
        Input::RegisterAction(kGameplayActionLookX, ActionValueType::Axis1D, 1.0e-4f);
        Input::RegisterAction(kGameplayActionLookY, ActionValueType::Axis1D, 1.0e-4f);
        Input::RegisterAction(kGameplayActionPrimaryFire, ActionValueType::Bool);
        Input::RegisterAction(kGameplayActionAim, ActionValueType::Bool);

        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionMoveForward);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionMoveBackward);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionMoveLeft);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionMoveRight);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionSprint);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionLookX);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionLookY);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionPrimaryFire);
        Input::ClearActionBindings(kGameplayInputContext, kGameplayActionAim);

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
    }
}
