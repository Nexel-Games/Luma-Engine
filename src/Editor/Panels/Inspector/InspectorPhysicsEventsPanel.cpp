#include "Luma/Editor/Panels/Inspector/InspectorPhysicsEventsPanel.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/PhysicsEventsComponent.h"
#include "Luma/Scene/RagdollComponent.h"

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
        bool InputTextWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::InputText(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }

        template <typename... Args>
        bool SliderFloatWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::SliderFloat(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }
    }

    void InspectorPhysicsEventsPanel::Draw(const InspectorPhysicsEventsPanelContext& context)
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

        if (registry.all_of<PhysicsEventsComponent>(context.selectedEntity))
        {
            auto& events = registry.get<PhysicsEventsComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Physics Events", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Configure which collision and trigger callbacks are emitted for this entity.");
                ImGui::PushID("PhysicsEventsComponent");
                CheckboxWithTooltip("On Collision Enter", &events.onCollisionEnter);
                CheckboxWithTooltip("On Collision Stay", &events.onCollisionStay);
                CheckboxWithTooltip("On Collision Exit", &events.onCollisionExit);
                CheckboxWithTooltip("On Trigger Enter", &events.onTriggerEnter);
                CheckboxWithTooltip("On Trigger Stay", &events.onTriggerStay);
                CheckboxWithTooltip("On Trigger Exit", &events.onTriggerExit);
                DragFloatWithTooltip("Contact Impulse Threshold", &events.contactImpulseThreshold, 0.01f, 0.0f, 1000000.0f);
                ShowItemTooltip("Minimum impulse magnitude required before collision callbacks are emitted.");
                events.contactImpulseThreshold = std::max(0.0f, events.contactImpulseThreshold);
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Physics Events Component"))
            {
                registry.remove<PhysicsEventsComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove physics event routing from this entity.");
        }

        if (registry.all_of<RagdollComponent>(context.selectedEntity))
        {
            auto& ragdoll = registry.get<RagdollComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Ragdoll", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Bind skeleton and physics asset for animation-to-physics ragdoll blending.");
                ImGui::PushID("RagdollComponent");
                CheckboxWithTooltip("Active", &ragdoll.active);
                std::array<char, 512> skeletalMeshBuffer {};
                std::snprintf(skeletalMeshBuffer.data(), skeletalMeshBuffer.size(), "%s", ragdoll.skeletalMeshAsset.c_str());
                if (InputTextWithTooltip("Skeletal Mesh", skeletalMeshBuffer.data(), skeletalMeshBuffer.size()))
                {
                    ragdoll.skeletalMeshAsset = skeletalMeshBuffer.data();
                }

                std::array<char, 512> physicsAssetBuffer {};
                std::snprintf(physicsAssetBuffer.data(), physicsAssetBuffer.size(), "%s", ragdoll.physicsAsset.c_str());
                if (InputTextWithTooltip("Physics Asset", physicsAssetBuffer.data(), physicsAssetBuffer.size()))
                {
                    ragdoll.physicsAsset = physicsAssetBuffer.data();
                }

                SliderFloatWithTooltip("Animation/Physics Blend", &ragdoll.animationPhysicsBlend, 0.0f, 1.0f);
                CheckboxWithTooltip("Start Simulated", &ragdoll.startSimulated);
                ShowItemTooltip("Blend=0 uses animation only, Blend=1 uses full physics ragdoll.");
                ragdoll.animationPhysicsBlend = std::clamp(ragdoll.animationPhysicsBlend, 0.0f, 1.0f);
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Ragdoll Component"))
            {
                registry.remove<RagdollComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove ragdoll behavior from this entity.");
        }
    }
}
