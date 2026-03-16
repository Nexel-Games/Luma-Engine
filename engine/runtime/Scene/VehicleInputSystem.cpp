#include "Luma/Scene/VehicleInputSystem.h"

#include <algorithm>

#include "Luma/Input/Input.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/TransformComponent.h"
#include "Luma/Scene/VehicleComponent.h"
#include "Luma/Scene/VehicleInputActions.h"
#include "Luma/Scene/VehicleInputComponent.h"

namespace Luma
{
    namespace
    {
        void CloneDefaultVehicleBindingsIfNeeded(const std::string_view contextName)
        {
            if (contextName.empty() || contextName == VehicleInputActions::Context)
            {
                return;
            }

            const bool hasBindings =
                !Input::GetActionBindings(contextName, VehicleInputActions::Throttle).empty() ||
                !Input::GetActionBindings(contextName, VehicleInputActions::Steer).empty() ||
                !Input::GetActionBindings(contextName, VehicleInputActions::Brake).empty() ||
                !Input::GetActionBindings(contextName, VehicleInputActions::Handbrake).empty() ||
                !Input::GetActionBindings(contextName, VehicleInputActions::Clutch).empty() ||
                !Input::GetActionBindings(contextName, VehicleInputActions::GearUp).empty() ||
                !Input::GetActionBindings(contextName, VehicleInputActions::GearDown).empty() ||
                !Input::GetActionBindings(contextName, VehicleInputActions::Reset).empty();
            if (hasBindings)
            {
                return;
            }

            Input::RegisterContext(contextName, 35, true);
            for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::Throttle))
            {
                Input::BindAction(contextName, VehicleInputActions::Throttle, binding);
            }
            for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::Steer))
            {
                Input::BindAction(contextName, VehicleInputActions::Steer, binding);
            }
            for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::Brake))
            {
                Input::BindAction(contextName, VehicleInputActions::Brake, binding);
            }
            for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::Handbrake))
            {
                Input::BindAction(contextName, VehicleInputActions::Handbrake, binding);
            }
            for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::Clutch))
            {
                Input::BindAction(contextName, VehicleInputActions::Clutch, binding);
            }
            for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::GearUp))
            {
                Input::BindAction(contextName, VehicleInputActions::GearUp, binding);
            }
            for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::GearDown))
            {
                Input::BindAction(contextName, VehicleInputActions::GearDown, binding);
            }
            for (const auto& binding : Input::GetActionBindings(VehicleInputActions::Context, VehicleInputActions::Reset))
            {
                Input::BindAction(contextName, VehicleInputActions::Reset, binding);
            }
        }
    }

    void VehicleInputSystem::UpdatePlayerInputs(Scene& scene) const
    {
        if (!Input::IsInitialized())
        {
            return;
        }

        auto& registry = scene.GetRegistry();
        auto vehicleView = registry.view<TransformComponent, VehicleComponent>();
        for (const EntityID entity : vehicleView)
        {
            auto& vehicle = vehicleView.get<VehicleComponent>(entity);
            if (!vehicle.active || !vehicle.simulationEnabled || vehicle.inputSource != VehicleInputSource::Player)
            {
                continue;
            }

            auto* input = registry.try_get<VehicleInputComponent>(entity);
            if (input == nullptr)
            {
                input = &registry.emplace<VehicleInputComponent>(entity);
            }
            if (!input->active)
            {
                continue;
            }

            const std::string_view inputContext = vehicle.inputMap.empty()
                ? VehicleInputActions::Context
                : std::string_view(vehicle.inputMap);
            CloneDefaultVehicleBindingsIfNeeded(inputContext);

            input->throttle = std::clamp(Input::GetActionValue(inputContext, VehicleInputActions::Throttle), -1.0f, 1.0f);
            input->steering = std::clamp(Input::GetActionValue(inputContext, VehicleInputActions::Steer), -1.0f, 1.0f);
            input->brake = std::clamp(Input::GetActionValue(inputContext, VehicleInputActions::Brake), 0.0f, 1.0f);
            input->handbrake = std::clamp(Input::GetActionValue(inputContext, VehicleInputActions::Handbrake), 0.0f, 1.0f);
            input->clutch = std::clamp(Input::GetActionValue(inputContext, VehicleInputActions::Clutch), 0.0f, 1.0f);
            input->gearUpRequested = Input::WasActionStarted(inputContext, VehicleInputActions::GearUp);
            input->gearDownRequested = Input::WasActionStarted(inputContext, VehicleInputActions::GearDown);
            input->resetRequested = Input::WasActionStarted(inputContext, VehicleInputActions::Reset);
        }
    }
}
