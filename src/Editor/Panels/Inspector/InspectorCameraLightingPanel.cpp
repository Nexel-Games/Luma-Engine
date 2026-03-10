#include "Luma/Editor/Panels/Inspector/InspectorCameraLightingPanel.h"

#include <algorithm>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/CameraComponent.h"
#include "Luma/Scene/DirectionalLightComponent.h"
#include "Luma/Scene/PointLightComponent.h"
#include "Luma/Scene/SpotLightComponent.h"

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
        bool ColorEdit3WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::ColorEdit3(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }
    }

    void InspectorCameraLightingPanel::Draw(const InspectorCameraLightingPanelContext& context)
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

        if (registry.all_of<CameraComponent>(context.selectedEntity))
        {
            auto& camera = registry.get<CameraComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Configure camera lens and clipping properties.");
                ImGui::PushID("CameraComponent");
                CheckboxWithTooltip("Primary", &camera.primary);
                DragFloatWithTooltip("FOV", &camera.fovDegrees, 0.1f, 10.0f, 170.0f);
                DragFloatWithTooltip("Near Clip", &camera.nearClip, 0.01f, 0.001f, 100.0f);
                DragFloatWithTooltip("Far Clip", &camera.farClip, 1.0f, 1.0f, 100000.0f);
                if (camera.farClip < camera.nearClip + 0.01f)
                {
                    camera.farClip = camera.nearClip + 0.01f;
                }
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Camera Component"))
            {
                registry.remove<CameraComponent>(context.selectedEntity);
            }
        }

        if (registry.all_of<DirectionalLightComponent>(context.selectedEntity))
        {
            auto& directional = registry.get<DirectionalLightComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Configure directional light intensity, color, and shadows.");
                ImGui::PushID("DirectionalLightComponent");
                CheckboxWithTooltip("Active", &directional.active);
                ColorEdit3WithTooltip("Color", directional.color.data());
                DragFloatWithTooltip("Intensity", &directional.intensity, 0.01f, 0.0f, 64.0f);
                CheckboxWithTooltip("Cast Shadows", &directional.castShadows);
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Directional Light Component"))
            {
                registry.remove<DirectionalLightComponent>(context.selectedEntity);
            }
        }

        if (registry.all_of<PointLightComponent>(context.selectedEntity))
        {
            auto& pointLight = registry.get<PointLightComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Configure omni-directional local lighting.");
                ImGui::PushID("PointLightComponent");
                CheckboxWithTooltip("Active", &pointLight.active);
                ColorEdit3WithTooltip("Color", pointLight.color.data());
                DragFloatWithTooltip("Intensity", &pointLight.intensity, 0.05f, 0.0f, 256.0f);
                DragFloatWithTooltip("Range", &pointLight.range, 0.05f, 0.05f, 1000.0f);
                DragFloatWithTooltip(
                    "Volumetric Scatter",
                    &pointLight.volumetricScatteringIntensity,
                    0.01f,
                    0.0f,
                    16.0f);
                CheckboxWithTooltip("Cast Shadows", &pointLight.castShadows);
                pointLight.range = std::max(pointLight.range, 0.05f);
                pointLight.intensity = std::max(pointLight.intensity, 0.0f);
                pointLight.volumetricScatteringIntensity = std::max(pointLight.volumetricScatteringIntensity, 0.0f);
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Point Light Component"))
            {
                registry.remove<PointLightComponent>(context.selectedEntity);
            }
        }

        if (registry.all_of<SpotLightComponent>(context.selectedEntity))
        {
            auto& spotLight = registry.get<SpotLightComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Spot Light", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Configure focused cone lighting.");
                ImGui::PushID("SpotLightComponent");
                CheckboxWithTooltip("Active", &spotLight.active);
                ColorEdit3WithTooltip("Color", spotLight.color.data());
                DragFloatWithTooltip("Intensity", &spotLight.intensity, 0.05f, 0.0f, 256.0f);
                DragFloatWithTooltip("Range", &spotLight.range, 0.05f, 0.05f, 1000.0f);
                DragFloatWithTooltip("Inner Cone", &spotLight.innerConeAngle, 0.1f, 0.0f, 89.0f);
                DragFloatWithTooltip("Outer Cone", &spotLight.outerConeAngle, 0.1f, 0.1f, 89.9f);
                DragFloatWithTooltip(
                    "Volumetric Scatter",
                    &spotLight.volumetricScatteringIntensity,
                    0.01f,
                    0.0f,
                    16.0f);
                CheckboxWithTooltip("Cast Shadows", &spotLight.castShadows);
                spotLight.range = std::max(spotLight.range, 0.05f);
                spotLight.intensity = std::max(spotLight.intensity, 0.0f);
                spotLight.innerConeAngle = std::clamp(spotLight.innerConeAngle, 0.0f, 89.0f);
                spotLight.outerConeAngle = std::clamp(spotLight.outerConeAngle, spotLight.innerConeAngle + 0.1f, 89.9f);
                spotLight.volumetricScatteringIntensity = std::max(spotLight.volumetricScatteringIntensity, 0.0f);
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Spot Light Component"))
            {
                registry.remove<SpotLightComponent>(context.selectedEntity);
            }
        }
    }
}
