#include "Luma/Editor/Panels/Inspector/InspectorJointPanel.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/FixedJointComponent.h"
#include "Luma/Scene/HingeJointComponent.h"
#include "Luma/Scene/JointComponent.h"
#include "Luma/Scene/SliderJointComponent.h"

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
        bool ComboWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::Combo(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Choose ");
            return changed;
        }

        template <typename... Args>
        bool DragFloatWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragFloat(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool DragFloat3WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragFloat3(label, std::forward<Args>(args)...);
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

        void DrawUUIDField(const char* label, UUID& value, const char* tooltip)
        {
            unsigned long long rawValue = static_cast<unsigned long long>(value);
            if (ImGui::InputScalar(label, ImGuiDataType_U64, &rawValue))
            {
                value = static_cast<UUID>(rawValue);
            }
            ShowItemTooltip(tooltip);
        }

        const char* JointProjectionModeLabel(const JointProjectionMode projectionMode)
        {
            switch (projectionMode)
            {
            case JointProjectionMode::None:
                return "None";
            case JointProjectionMode::PositionOnly:
                return "Position Only";
            case JointProjectionMode::PositionAndRotation:
                return "Position + Rotation";
            default:
                return "Unknown";
            }
        }
    }

    void InspectorJointPanel::Draw(const InspectorJointPanelContext& context)
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

        if (registry.all_of<JointComponent>(context.selectedEntity))
        {
            auto& joint = registry.get<JointComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Joint", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Base constraint settings shared by all specific joint types.");
                ImGui::PushID("JointComponent");
                CheckboxWithTooltip("Active", &joint.active);
                ShowItemTooltip("Enable or disable this joint in the simulation.");
                DrawUUIDField("Connected Body A", joint.connectedBodyA, "Entity UUID for first connected body.");
                DrawUUIDField("Connected Body B", joint.connectedBodyB, "Entity UUID for second connected body.");
                CheckboxWithTooltip("Collide Connected Bodies", &joint.collideConnectedBodies);
                ShowItemTooltip("Allow collision contacts between the two connected bodies.");

                CheckboxWithTooltip("Enable Break", &joint.enableBreak);
                ShowItemTooltip("Allow this joint to break when force/torque limits are exceeded.");
                if (joint.enableBreak)
                {
                    DragFloatWithTooltip("Break Force", &joint.breakForce, 1.0f, 0.0f, 10000000.0f);
                    ShowItemTooltip("Maximum force before joint break.");
                    DragFloatWithTooltip("Break Torque", &joint.breakTorque, 1.0f, 0.0f, 10000000.0f);
                    ShowItemTooltip("Maximum torque before joint break.");
                }

                int projectionModeIndex = static_cast<int>(joint.projectionMode);
                const char* projectionItems[] = { "None", "Position Only", "Position + Rotation" };
                if (ComboWithTooltip("Projection", &projectionModeIndex, projectionItems, IM_ARRAYSIZE(projectionItems)))
                {
                    projectionModeIndex = std::clamp(projectionModeIndex, 0, 2);
                    joint.projectionMode = static_cast<JointProjectionMode>(projectionModeIndex);
                }
                ShowItemTooltip("Projection stabilizes drift when constraints violate solver limits.");
                ImGui::TextDisabled("Resolved Projection: %s", JointProjectionModeLabel(joint.projectionMode));

                int positionIterations = static_cast<int>(joint.solverPositionIterations);
                if (InputIntWithTooltip("Solver Pos Iterations", &positionIterations))
                {
                    joint.solverPositionIterations = static_cast<std::uint32_t>(std::max(1, positionIterations));
                }
                ShowItemTooltip("Position solver iteration count for this constraint.");

                int velocityIterations = static_cast<int>(joint.solverVelocityIterations);
                if (InputIntWithTooltip("Solver Vel Iterations", &velocityIterations))
                {
                    joint.solverVelocityIterations = static_cast<std::uint32_t>(std::max(1, velocityIterations));
                }
                ShowItemTooltip("Velocity solver iteration count for this constraint.");

                DragFloatWithTooltip("Projection Linear Tol", &joint.projectionLinearTolerance, 0.001f, 0.0f, 1000.0f);
                ShowItemTooltip("Maximum linear error before projection correction.");
                DragFloatWithTooltip(
                    "Projection Angular Tol",
                    &joint.projectionAngularToleranceDegrees,
                    0.01f,
                    0.0f,
                    360.0f);
                ShowItemTooltip("Maximum angular error (degrees) before projection correction.");
                joint.projectionLinearTolerance = std::max(0.0f, joint.projectionLinearTolerance);
                joint.projectionAngularToleranceDegrees =
                    std::clamp(joint.projectionAngularToleranceDegrees, 0.0f, 360.0f);
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Joint Component"))
            {
                registry.remove<JointComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove shared joint base settings from this entity.");
        }

        if (registry.all_of<FixedJointComponent>(context.selectedEntity))
        {
            auto& fixedJoint = registry.get<FixedJointComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Fixed Joint", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Fixed joint locks all relative translation and rotation.");
                ImGui::PushID("FixedJointComponent");
                CheckboxWithTooltip("Maintain Initial Offset", &fixedJoint.maintainInitialOffset);
                ShowItemTooltip("Preserve initial relative transform as the joint target frame.");
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Fixed Joint Component"))
            {
                registry.remove<FixedJointComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove fixed-joint behavior from this entity.");
        }

        if (registry.all_of<HingeJointComponent>(context.selectedEntity))
        {
            auto& hinge = registry.get<HingeJointComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Hinge Joint", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Revolute joint with one angular axis plus optional limits and motor.");
                ImGui::PushID("HingeJointComponent");
                DragFloat3WithTooltip("Axis", hinge.axis.data(), 0.01f, -1.0f, 1.0f);
                ShowItemTooltip("Local hinge axis for relative rotation.");

                CheckboxWithTooltip("Enable Limits", &hinge.enableLimits);
                ShowItemTooltip("Clamp hinge angle between lower and upper limit.");
                if (hinge.enableLimits)
                {
                    DragFloatWithTooltip("Lower Limit (deg)", &hinge.lowerLimitDegrees, 0.1f, -360.0f, 360.0f);
                    DragFloatWithTooltip("Upper Limit (deg)", &hinge.upperLimitDegrees, 0.1f, -360.0f, 360.0f);
                    ShowItemTooltip("Allowed hinge angle interval in degrees.");
                    if (hinge.lowerLimitDegrees > hinge.upperLimitDegrees)
                    {
                        std::swap(hinge.lowerLimitDegrees, hinge.upperLimitDegrees);
                    }
                }

                CheckboxWithTooltip("Enable Motor", &hinge.enableMotor);
                ShowItemTooltip("Drive hinge rotation using target velocity.");
                if (hinge.enableMotor)
                {
                    DragFloatWithTooltip(
                        "Motor Velocity (deg/s)",
                        &hinge.motorVelocityDegreesPerSecond,
                        0.1f,
                        -20000.0f,
                        20000.0f);
                    DragFloatWithTooltip("Motor Max Force", &hinge.motorMaxForce, 0.1f, 0.0f, 1000000.0f);
                    ShowItemTooltip("Maximum force the hinge motor may apply.");
                    hinge.motorMaxForce = std::max(0.0f, hinge.motorMaxForce);
                }
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Hinge Joint Component"))
            {
                registry.remove<HingeJointComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove hinge-joint behavior from this entity.");
        }

        if (registry.all_of<SliderJointComponent>(context.selectedEntity))
        {
            auto& slider = registry.get<SliderJointComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Slider Joint", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Prismatic joint with one translation axis plus optional limits and motor.");
                ImGui::PushID("SliderJointComponent");
                DragFloat3WithTooltip("Axis", slider.axis.data(), 0.01f, -1.0f, 1.0f);
                ShowItemTooltip("Local movement axis for slider translation.");

                CheckboxWithTooltip("Enable Limits", &slider.enableLimits);
                ShowItemTooltip("Clamp translation between lower and upper limits.");
                if (slider.enableLimits)
                {
                    DragFloatWithTooltip("Lower Limit", &slider.lowerLimit, 0.01f, -100000.0f, 100000.0f);
                    DragFloatWithTooltip("Upper Limit", &slider.upperLimit, 0.01f, -100000.0f, 100000.0f);
                    ShowItemTooltip("Allowed linear travel interval.");
                    if (slider.lowerLimit > slider.upperLimit)
                    {
                        std::swap(slider.lowerLimit, slider.upperLimit);
                    }
                }

                CheckboxWithTooltip("Enable Motor", &slider.enableMotor);
                ShowItemTooltip("Drive slider translation using target speed.");
                if (slider.enableMotor)
                {
                    DragFloatWithTooltip("Motor Speed", &slider.motorSpeed, 0.01f, -100000.0f, 100000.0f);
                    DragFloatWithTooltip("Motor Max Force", &slider.motorMaxForce, 0.1f, 0.0f, 1000000.0f);
                    ShowItemTooltip("Maximum force the slider motor may apply.");
                    slider.motorMaxForce = std::max(0.0f, slider.motorMaxForce);
                }
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Slider Joint Component"))
            {
                registry.remove<SliderJointComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove slider-joint behavior from this entity.");
        }
    }
}
