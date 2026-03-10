#include "Luma/Editor/Panels/Inspector/InspectorVehiclePhysicsPanel.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/VehicleComponent.h"
#include "Luma/Scene/WheelColliderComponent.h"

namespace Luma::Editor
{
    namespace
    {
        void ShowItemTooltip(const std::string_view tooltip)
        {
            UI::Tooltip::Show(tooltip);
        }

        void ShowItemTooltipFromLabel(const char* label, const char* prefix = nullptr)
        {
            UI::Tooltip::ShowForItemLabel(label, prefix == nullptr ? std::string_view {} : std::string_view(prefix));
        }

        template <typename... Args>
        bool CheckboxWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::Checkbox(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Toggle ");
            return changed;
        }

        template <typename... Args>
        bool ButtonWithTooltip(const char* label, Args&&... args)
        {
            const bool pressed = ImGui::Button(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return pressed;
        }

        template <typename... Args>
        bool DragFloatWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragFloat(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool InputIntWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::InputInt(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }

        template <typename... Args>
        bool InputTextWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::InputText(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }

        void DrawUUIDField(const char* label, UUID& value, const char* tooltip)
        {
            unsigned long long rawValue = static_cast<unsigned long long>(value);
            if (ImGui::InputScalar(label, ImGuiDataType_U64, &rawValue))
            {
                value = static_cast<UUID>(rawValue);
            }
            ShowItemTooltip(tooltip);
        }
    }

    void InspectorVehiclePhysicsPanel::Draw(const InspectorVehiclePhysicsPanelContext& context)
    {
        if (context.scene == nullptr || context.selectedEntity == entt::null)
        {
            return;
        }

        auto& registry = context.scene->GetRegistry();
        if (!registry.valid(context.selectedEntity))
        {
            return;
        }

        if (registry.all_of<WheelColliderComponent>(context.selectedEntity))
        {
            auto& wheel = registry.get<WheelColliderComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Wheel Collider", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Wheel contact, tire friction, and suspension settings.");
                ImGui::PushID("WheelColliderComponent");
                CheckboxWithTooltip("Active", &wheel.active);
                DragFloatWithTooltip("Radius", &wheel.radius, 0.001f, 0.01f, 100.0f);
                DragFloatWithTooltip("Width", &wheel.width, 0.001f, 0.01f, 100.0f);
                DragFloatWithTooltip("Wheel Mass", &wheel.wheelMass, 0.1f, 0.01f, 10000.0f);
                DragFloatWithTooltip("Suspension Stiffness", &wheel.suspensionStiffness, 1.0f, 0.0f, 1000000.0f);
                DragFloatWithTooltip("Suspension Damping", &wheel.suspensionDamping, 1.0f, 0.0f, 1000000.0f);
                DragFloatWithTooltip("Suspension Travel", &wheel.suspensionTravel, 0.001f, 0.0f, 100.0f);
                DragFloatWithTooltip("Tire Friction", &wheel.tireFriction, 0.01f, 0.0f, 100.0f);
                ShowItemTooltip("Tune tire grip and spring-damper response.");
                wheel.radius = std::max(0.01f, wheel.radius);
                wheel.width = std::max(0.01f, wheel.width);
                wheel.wheelMass = std::max(0.01f, wheel.wheelMass);
                wheel.suspensionStiffness = std::max(0.0f, wheel.suspensionStiffness);
                wheel.suspensionDamping = std::max(0.0f, wheel.suspensionDamping);
                wheel.suspensionTravel = std::max(0.0f, wheel.suspensionTravel);
                wheel.tireFriction = std::max(0.0f, wheel.tireFriction);
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Wheel Collider Component"))
            {
                registry.remove<WheelColliderComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove wheel-collider behavior from this entity.");
        }

        if (registry.all_of<VehicleComponent>(context.selectedEntity))
        {
            auto& vehicle = registry.get<VehicleComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Vehicle", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Vehicle chassis, drivetrain, steering, and wheel setup.");
                ImGui::PushID("VehicleComponent");
                CheckboxWithTooltip("Active", &vehicle.active);
                DrawUUIDField("Chassis Rigidbody UUID", vehicle.chassisRigidBody, "Entity UUID for vehicle chassis rigid body.");

                int wheelCount = static_cast<int>(vehicle.wheelEntities.size());
                if (InputIntWithTooltip("Wheel Count", &wheelCount))
                {
                    wheelCount = std::clamp(wheelCount, 0, 32);
                    vehicle.wheelEntities.resize(static_cast<std::size_t>(wheelCount), 0);
                }
                ShowItemTooltip("Number of wheel entity UUID references.");
                for (std::size_t index = 0; index < vehicle.wheelEntities.size(); ++index)
                {
                    std::array<char, 48> label {};
                    std::snprintf(label.data(), label.size(), "Wheel %u UUID", static_cast<unsigned int>(index));
                    DrawUUIDField(label.data(), vehicle.wheelEntities[index], "Entity UUID for this wheel.");
                }

                DragFloatWithTooltip("Engine Torque", &vehicle.engineTorque, 1.0f, 0.0f, 1000000.0f);
                DragFloatWithTooltip("Max RPM", &vehicle.maxRPM, 1.0f, 1.0f, 100000.0f);
                DragFloatWithTooltip("Gear Ratio", &vehicle.gearRatio, 0.01f, 0.01f, 100.0f);
                DragFloatWithTooltip("Differential Ratio", &vehicle.differentialRatio, 0.01f, 0.01f, 100.0f);
                DragFloatWithTooltip("Tire Friction Scale", &vehicle.tireFrictionScale, 0.01f, 0.0f, 100.0f);
                ShowItemTooltip("Powertrain and tire grip scaling.");

                DragFloatWithTooltip("Suspension Stiffness", &vehicle.suspensionStiffness, 1.0f, 0.0f, 1000000.0f);
                DragFloatWithTooltip("Suspension Damping", &vehicle.suspensionDamping, 1.0f, 0.0f, 1000000.0f);
                DragFloatWithTooltip("Suspension Travel", &vehicle.suspensionTravel, 0.001f, 0.0f, 100.0f);

                DragFloatWithTooltip("Max Steer Angle (deg)", &vehicle.maxSteerAngleDegrees, 0.1f, 0.0f, 90.0f);
                DragFloatWithTooltip("Steer Sensitivity", &vehicle.steerSensitivity, 0.01f, 0.0f, 10.0f);

                CheckboxWithTooltip("ABS", &vehicle.enableABS);
                CheckboxWithTooltip("TCS", &vehicle.enableTCS);
                ShowItemTooltip("Enable anti-lock braking and traction control toggles.");

                std::array<char, 128> inputMapBuffer {};
                std::snprintf(inputMapBuffer.data(), inputMapBuffer.size(), "%s", vehicle.inputMap.c_str());
                if (InputTextWithTooltip("Input Map", inputMapBuffer.data(), inputMapBuffer.size()))
                {
                    vehicle.inputMap = inputMapBuffer.data();
                }
                ShowItemTooltip("Input action-map name used for vehicle controls.");
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Vehicle Component"))
            {
                registry.remove<VehicleComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove vehicle behavior from this entity.");
        }
    }
}
