#include "Luma/Editor/Panels/Inspector/InspectorAdvancedPhysicsPanel.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/CharacterControllerComponent.h"
#include "Luma/Scene/D6JointComponent.h"

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

        const char* JointMotionModeLabel(const JointMotionMode motionMode)
        {
            switch (motionMode)
            {
            case JointMotionMode::Locked:
                return "Locked";
            case JointMotionMode::Limited:
                return "Limited";
            case JointMotionMode::Free:
                return "Free";
            default:
                return "Unknown";
            }
        }

        const char* CharacterMovementModeLabel(const CharacterMovementMode movementMode)
        {
            switch (movementMode)
            {
            case CharacterMovementMode::Walk:
                return "Walk";
            case CharacterMovementMode::Fly:
                return "Fly";
            case CharacterMovementMode::Swim:
                return "Swim";
            default:
                return "Unknown";
            }
        }
    }

    void InspectorAdvancedPhysicsPanel::Draw(const InspectorAdvancedPhysicsPanelContext& context)
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

        if (registry.all_of<D6JointComponent>(context.selectedEntity))
        {
            auto& d6 = registry.get<D6JointComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("D6 Joint", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("General 6-DOF constraint with per-axis motion modes and drives.");
                ImGui::PushID("D6JointComponent");

                const char* motionItems[] = { "Locked", "Limited", "Free" };
                const char* linearLabels[] = { "Linear X", "Linear Y", "Linear Z" };
                for (int axis = 0; axis < 3; ++axis)
                {
                    int motionIndex = static_cast<int>(d6.linearMotion[axis]);
                    if (ComboWithTooltip(linearLabels[axis], &motionIndex, motionItems, IM_ARRAYSIZE(motionItems)))
                    {
                        motionIndex = std::clamp(motionIndex, 0, 2);
                        d6.linearMotion[axis] = static_cast<JointMotionMode>(motionIndex);
                    }
                    ShowItemTooltip("Set linear axis to locked, limited, or free.");
                }

                const char* angularLabels[] = { "Twist", "Swing Y", "Swing Z" };
                for (int axis = 0; axis < 3; ++axis)
                {
                    int motionIndex = static_cast<int>(d6.angularMotion[axis]);
                    if (ComboWithTooltip(angularLabels[axis], &motionIndex, motionItems, IM_ARRAYSIZE(motionItems)))
                    {
                        motionIndex = std::clamp(motionIndex, 0, 2);
                        d6.angularMotion[axis] = static_cast<JointMotionMode>(motionIndex);
                    }
                    ShowItemTooltip("Set angular axis to locked, limited, or free.");
                }

                ImGui::TextDisabled(
                    "Linear: %s / %s / %s",
                    JointMotionModeLabel(d6.linearMotion[0]),
                    JointMotionModeLabel(d6.linearMotion[1]),
                    JointMotionModeLabel(d6.linearMotion[2]));
                ImGui::TextDisabled(
                    "Angular: %s / %s / %s",
                    JointMotionModeLabel(d6.angularMotion[0]),
                    JointMotionModeLabel(d6.angularMotion[1]),
                    JointMotionModeLabel(d6.angularMotion[2]));

                DragFloatWithTooltip("Linear Limit", &d6.linearLimit, 0.01f, 0.0f, 100000.0f);
                ShowItemTooltip("Shared linear limit used when axis mode is Limited.");
                d6.linearLimit = std::max(0.0f, d6.linearLimit);

                DragFloatWithTooltip("Twist Lower (deg)", &d6.twistLowerLimitDegrees, 0.1f, -360.0f, 360.0f);
                DragFloatWithTooltip("Twist Upper (deg)", &d6.twistUpperLimitDegrees, 0.1f, -360.0f, 360.0f);
                DragFloatWithTooltip("Swing Y (deg)", &d6.swingYLimitDegrees, 0.1f, 0.0f, 360.0f);
                DragFloatWithTooltip("Swing Z (deg)", &d6.swingZLimitDegrees, 0.1f, 0.0f, 360.0f);
                ShowItemTooltip("Angular limits applied when axis mode is Limited.");
                if (d6.twistLowerLimitDegrees > d6.twistUpperLimitDegrees)
                {
                    std::swap(d6.twistLowerLimitDegrees, d6.twistUpperLimitDegrees);
                }
                d6.swingYLimitDegrees = std::clamp(d6.swingYLimitDegrees, 0.0f, 360.0f);
                d6.swingZLimitDegrees = std::clamp(d6.swingZLimitDegrees, 0.0f, 360.0f);

                CheckboxWithTooltip("Enable Linear Drive", &d6.enableLinearDrive);
                ShowItemTooltip("Drive linear axes towards target position/velocity.");
                if (d6.enableLinearDrive)
                {
                    DragFloat3WithTooltip("Linear Target Pos", d6.linearDrivePositionTarget.data(), 0.01f);
                    DragFloat3WithTooltip("Linear Target Vel", d6.linearDriveVelocityTarget.data(), 0.01f);
                    DragFloatWithTooltip("Linear Stiffness", &d6.linearDriveStiffness, 0.1f, 0.0f, 1000000.0f);
                    DragFloatWithTooltip("Linear Damping", &d6.linearDriveDamping, 0.1f, 0.0f, 1000000.0f);
                    DragFloatWithTooltip("Linear Force Limit", &d6.linearDriveForceLimit, 0.1f, 0.0f, 100000000.0f);
                    d6.linearDriveStiffness = std::max(0.0f, d6.linearDriveStiffness);
                    d6.linearDriveDamping = std::max(0.0f, d6.linearDriveDamping);
                    d6.linearDriveForceLimit = std::max(0.0f, d6.linearDriveForceLimit);
                }

                CheckboxWithTooltip("Enable Angular Drive", &d6.enableAngularDrive);
                ShowItemTooltip("Drive angular axes towards target position/velocity.");
                if (d6.enableAngularDrive)
                {
                    DragFloat3WithTooltip("Angular Target Pos", d6.angularDrivePositionTarget.data(), 0.01f);
                    DragFloat3WithTooltip("Angular Target Vel", d6.angularDriveVelocityTarget.data(), 0.01f);
                    DragFloatWithTooltip("Angular Stiffness", &d6.angularDriveStiffness, 0.1f, 0.0f, 1000000.0f);
                    DragFloatWithTooltip("Angular Damping", &d6.angularDriveDamping, 0.1f, 0.0f, 1000000.0f);
                    DragFloatWithTooltip("Angular Force Limit", &d6.angularDriveForceLimit, 0.1f, 0.0f, 100000000.0f);
                    d6.angularDriveStiffness = std::max(0.0f, d6.angularDriveStiffness);
                    d6.angularDriveDamping = std::max(0.0f, d6.angularDriveDamping);
                    d6.angularDriveForceLimit = std::max(0.0f, d6.angularDriveForceLimit);
                }
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove D6 Joint Component"))
            {
                registry.remove<D6JointComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove D6-joint behavior from this entity.");
        }

        if (registry.all_of<CharacterControllerComponent>(context.selectedEntity))
        {
            auto& controller = registry.get<CharacterControllerComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Character Controller", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Capsule-based character locomotion and grounding controls.");
                ImGui::PushID("CharacterControllerComponent");
                CheckboxWithTooltip("Active", &controller.active);
                ShowItemTooltip("Enable or disable this character controller.");

                int movementModeIndex = static_cast<int>(controller.movementMode);
                const char* movementModeItems[] = { "Walk", "Fly", "Swim" };
                if (ComboWithTooltip("Movement Mode", &movementModeIndex, movementModeItems, IM_ARRAYSIZE(movementModeItems)))
                {
                    movementModeIndex = std::clamp(movementModeIndex, 0, 2);
                    controller.movementMode = static_cast<CharacterMovementMode>(movementModeIndex);
                }
                ShowItemTooltip("Select movement behavior preset.");
                ImGui::TextDisabled("Resolved Mode: %s", CharacterMovementModeLabel(controller.movementMode));

                DragFloatWithTooltip("Radius", &controller.radius, 0.01f, 0.01f, 1000.0f);
                DragFloatWithTooltip("Height", &controller.height, 0.01f, 0.02f, 1000.0f);
                DragFloatWithTooltip("Step Offset", &controller.stepOffset, 0.01f, 0.0f, 100.0f);
                DragFloatWithTooltip("Slope Limit (deg)", &controller.slopeLimitDegrees, 0.1f, 0.0f, 89.0f);
                DragFloatWithTooltip("Skin Width", &controller.skinWidth, 0.001f, 0.001f, 10.0f);
                DragFloatWithTooltip("Min Move Distance", &controller.minMoveDistance, 0.0001f, 0.0f, 1.0f);
                DragFloatWithTooltip("Gravity Scale", &controller.gravityScale, 0.01f, 0.0f, 100.0f);
                ShowItemTooltip("Tune capsule shape and movement tolerances.");
                controller.radius = std::max(0.01f, controller.radius);
                controller.height = std::max(controller.radius * 2.0f, controller.height);
                controller.skinWidth = std::max(0.001f, controller.skinWidth);
                controller.minMoveDistance = std::max(0.0f, controller.minMoveDistance);
                controller.gravityScale = std::max(0.0f, controller.gravityScale);
                controller.slopeLimitDegrees = std::clamp(controller.slopeLimitDegrees, 0.0f, 89.0f);

                int layer = static_cast<int>(controller.collisionLayer);
                if (InputIntWithTooltip("Collision Layer", &layer))
                {
                    controller.collisionLayer = static_cast<std::uint32_t>(std::max(0, layer));
                }
                int mask = static_cast<int>(controller.collisionMask);
                if (InputIntWithTooltip("Collision Mask", &mask))
                {
                    controller.collisionMask = static_cast<std::uint32_t>(std::max(0, mask));
                }
                ShowItemTooltip("Layer and mask filtering used for character collision queries.");
                ImGui::TextDisabled("Grounded: %s", controller.isGrounded ? "Yes" : "No");
                ShowItemTooltip("Runtime grounding flag from the last simulation step.");
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Character Controller Component"))
            {
                registry.remove<CharacterControllerComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove character-controller behavior from this entity.");
        }
    }
}
