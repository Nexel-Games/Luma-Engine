#include "Luma/Editor/Panels/Inspector/InspectorCameraLightingPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Core/App/Project.h"
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
        bool DragIntWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragInt(label, std::forward<Args>(args)...);
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

        template <typename... Args>
        bool ColorEdit4WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::ColorEdit4(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }

        template <typename... Args>
        bool SliderIntWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::SliderInt(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool SliderFloatWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::SliderFloat(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        bool InputUIntWithTooltip(const char* label, std::uint32_t* value)
        {
            const bool changed = ImGui::InputScalar(label, ImGuiDataType_U32, value);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }

        std::string BuildLayerMaskPreview(const std::uint32_t mask)
        {
            if (mask == 0xFFFFFFFFu)
            {
                return "Everything";
            }

            const auto& projectConfig = Project::GetConfig();
            if (projectConfig.layers.empty())
            {
                return mask == 0u ? "Nothing" : "Custom";
            }

            std::string preview;
            for (std::size_t layerIndex = 0; layerIndex < projectConfig.layers.size() && layerIndex < 32u; ++layerIndex)
            {
                const std::uint32_t bit = 1u << static_cast<std::uint32_t>(layerIndex);
                if ((mask & bit) == 0u)
                {
                    continue;
                }

                if (!preview.empty())
                {
                    preview += ", ";
                }
                preview += projectConfig.layers[layerIndex];
                if (preview.size() > 28u)
                {
                    preview += "...";
                    break;
                }
            }

            return preview.empty() ? "Nothing" : preview;
        }

        bool LayerMaskSelectorWithTooltip(const char* label, std::uint32_t* mask)
        {
            if (mask == nullptr)
            {
                return false;
            }

            bool changed = false;
            const std::string preview = BuildLayerMaskPreview(*mask);
            if (ImGui::BeginCombo(label, preview.c_str()))
            {
                const auto& projectConfig = Project::GetConfig();
                const std::size_t layerCount = std::min<std::size_t>(projectConfig.layers.size(), 32u);
                const std::uint32_t knownLayerMask =
                    layerCount >= 32u ? 0xFFFFFFFFu : ((1u << static_cast<std::uint32_t>(layerCount)) - 1u);

                bool everything = *mask == 0xFFFFFFFFu;
                if (ImGui::Checkbox("Everything", &everything))
                {
                    *mask = everything ? 0xFFFFFFFFu : 0u;
                    changed = true;
                }

                bool nothing = *mask == 0u;
                if (ImGui::Checkbox("Nothing", &nothing))
                {
                    if (nothing)
                    {
                        *mask = 0u;
                    }
                    else if (*mask == 0u)
                    {
                        *mask = 0xFFFFFFFFu;
                    }
                    changed = true;
                }

                ImGui::Separator();
                for (std::size_t layerIndex = 0; layerIndex < layerCount; ++layerIndex)
                {
                    const std::uint32_t bit = 1u << static_cast<std::uint32_t>(layerIndex);
                    bool enabled = (*mask & bit) != 0u;
                    const std::string itemLabel =
                        projectConfig.layers[layerIndex] + "##LayerMask" + std::to_string(layerIndex);
                    if (ImGui::Checkbox(itemLabel.c_str(), &enabled))
                    {
                        if (enabled)
                        {
                            *mask |= bit;
                        }
                        else
                        {
                            *mask &= ~bit;
                        }
                        *mask &= knownLayerMask;
                        changed = true;
                    }
                }

                ImGui::EndCombo();
            }
            ShowItemTooltipFromLabel(label, "Select ");
            return changed;
        }

        void SyncFovFromLens(CameraComponent& camera)
        {
            const float sensorHeight = std::max(camera.sensorHeight, 1.0e-3f);
            const float focalLength = std::max(camera.focalLength, 1.0e-3f);
            constexpr float kPi = 3.14159265359f;
            camera.fovDegrees = std::clamp(
                2.0f * std::atan(sensorHeight / (2.0f * focalLength)) * (180.0f / kPi),
                1.0f,
                170.0f);
        }

        void SyncLensFromFov(CameraComponent& camera)
        {
            const float sensorHeight = std::max(camera.sensorHeight, 1.0e-3f);
            const float fovDegrees = std::clamp(camera.fovDegrees, 1.0f, 170.0f);
            constexpr float kPi = 3.14159265359f;
            const float halfFovRadians = (fovDegrees * (kPi / 180.0f)) * 0.5f;
            const float tangent = std::max(std::tan(halfFovRadians), 1.0e-4f);
            camera.focalLength = std::max(sensorHeight / (2.0f * tangent), 1.0e-3f);
        }

        void ClampCamera(CameraComponent& camera)
        {
            camera.fovDegrees = std::clamp(camera.fovDegrees, 1.0f, 170.0f);
            camera.orthographicSize = std::max(camera.orthographicSize, 0.01f);
            camera.nearClip = std::max(camera.nearClip, 0.001f);
            camera.farClip = std::max(camera.farClip, camera.nearClip + 0.01f);
            camera.sensorWidth = std::max(camera.sensorWidth, 1.0e-3f);
            camera.sensorHeight = std::max(camera.sensorHeight, 1.0e-3f);
            camera.focalLength = std::max(camera.focalLength, 1.0e-3f);
            camera.aspectRatio = std::max(camera.aspectRatio, 1.0e-3f);
            for (float& channel : camera.clearColor)
            {
                channel = std::clamp(channel, 0.0f, 1.0f);
            }
        }

        void DrawSectionLabel(const char* label, const char* tooltip)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", label);
            if (tooltip != nullptr)
            {
                ShowItemTooltip(tooltip);
            }
        }

        std::array<float, 3> KelvinToRgb(float kelvin)
        {
            const float temperature = std::clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;
            float red = 255.0f;
            float green = 255.0f;
            float blue = 255.0f;

            if (temperature > 66.0f)
            {
                red = 329.698727446f * std::pow(temperature - 60.0f, -0.1332047592f);
                green = 288.1221695283f * std::pow(temperature - 60.0f, -0.0755148492f);
            }
            else
            {
                green = 99.4708025861f * std::log(std::max(temperature, 1.0f)) - 161.1195681661f;
            }

            if (temperature < 66.0f)
            {
                if (temperature <= 19.0f)
                {
                    blue = 0.0f;
                }
                else
                {
                    blue = 138.5177312231f * std::log(std::max(temperature - 10.0f, 1.0f)) - 305.0447927307f;
                }
            }

            return {
                std::clamp(red / 255.0f, 0.0f, 1.0f),
                std::clamp(green / 255.0f, 0.0f, 1.0f),
                std::clamp(blue / 255.0f, 0.0f, 1.0f)
            };
        }

        std::uint32_t NormalizeShadowResolution(std::uint32_t resolution)
        {
            resolution = std::clamp<std::uint32_t>(resolution, 128u, 2048u);
            std::uint32_t normalized = 128u;
            while (normalized < resolution && normalized < 2048u)
            {
                normalized <<= 1u;
            }

            const std::uint32_t lower = normalized >> 1u;
            if (lower >= 128u && normalized - resolution > resolution - lower)
            {
                normalized = lower;
            }

            return std::clamp<std::uint32_t>(normalized, 128u, 2048u);
        }

        bool ShadowResolutionComboWithTooltip(const char* label, std::uint32_t* value)
        {
            if (value == nullptr)
            {
                return false;
            }

            constexpr std::array<std::uint32_t, 5> kResolutions { 128u, 256u, 512u, 1024u, 2048u };
            const std::uint32_t normalizedValue = NormalizeShadowResolution(*value);
            std::size_t currentIndex = 0;
            for (std::size_t index = 0; index < kResolutions.size(); ++index)
            {
                if (kResolutions[index] == normalizedValue)
                {
                    currentIndex = index;
                    break;
                }
            }

            bool changed = false;
            const std::string preview = std::to_string(kResolutions[currentIndex]);
            if (ImGui::BeginCombo(label, preview.c_str()))
            {
                for (std::size_t index = 0; index < kResolutions.size(); ++index)
                {
                    const bool selected = index == currentIndex;
                    const std::string itemLabel = std::to_string(kResolutions[index]);
                    if (ImGui::Selectable(itemLabel.c_str(), selected))
                    {
                        *value = kResolutions[index];
                        changed = true;
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ShowItemTooltipFromLabel(label, "Select ");
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
                ShowItemTooltip("Configure runtime camera behavior, lens settings, and feed selection.");
                ImGui::PushID("CameraComponent");

                bool primary = camera.primary;
                if (CheckboxWithTooltip("Primary", &primary))
                {
                    camera.primary = primary;
                    if (context.setContentStatus)
                    {
                        if (camera.primary)
                        {
                            context.setContentStatus(
                                "Camera marked Primary. Highest-priority primary camera drives the game view.");
                        }
                        else
                        {
                            const auto cameraView = registry.view<CameraComponent>();
                            bool hasAnotherPrimary = false;
                            for (const EntityID entity : cameraView)
                            {
                                if (entity == context.selectedEntity)
                                {
                                    continue;
                                }

                                const auto& otherCamera = cameraView.get<CameraComponent>(entity);
                                if (otherCamera.primary && otherCamera.active)
                                {
                                    hasAnotherPrimary = true;
                                    break;
                                }
                            }

                            context.setContentStatus(
                                hasAnotherPrimary
                                    ? "Another primary camera remains. Highest render priority primary camera will be used."
                                    : "No Primary Camera Is Selected.");
                        }
                    }
                }
                CheckboxWithTooltip("Active", &camera.active);

                DrawSectionLabel("Projection", "Choose whether this camera renders with perspective or orthographic projection.");
                int projectionMode = static_cast<int>(camera.projection);
                const char* projectionItems[] = { "Perspective", "Orthographic" };
                if (ImGui::Combo("Projection", &projectionMode, projectionItems, IM_ARRAYSIZE(projectionItems)))
                {
                    camera.projection = static_cast<CameraProjectionMode>(projectionMode);
                    if (camera.projection == CameraProjectionMode::Perspective)
                    {
                        SyncLensFromFov(camera);
                    }
                }
                ShowItemTooltipFromLabel("Projection");

                if (camera.projection == CameraProjectionMode::Perspective)
                {
                    if (DragFloatWithTooltip("Vertical FOV", &camera.fovDegrees, 0.1f, 1.0f, 170.0f))
                    {
                        SyncLensFromFov(camera);
                    }
                }
                else
                {
                    DragFloatWithTooltip("Orthographic Size", &camera.orthographicSize, 0.01f, 0.01f, 100000.0f);
                }

                DragFloatWithTooltip("Near Clip", &camera.nearClip, 0.01f, 0.001f, 1000.0f);
                DragFloatWithTooltip("Far Clip", &camera.farClip, 1.0f, 0.01f, 1000000.0f);

                DrawSectionLabel("Lens", "Physical sensor and focal values used to derive perspective FOV.");
                bool lensChanged = false;
                lensChanged |= DragFloatWithTooltip("Sensor Width", &camera.sensorWidth, 0.01f, 0.001f, 1000.0f);
                lensChanged |= DragFloatWithTooltip("Sensor Height", &camera.sensorHeight, 0.01f, 0.001f, 1000.0f);
                lensChanged |= DragFloatWithTooltip("Focal Length", &camera.focalLength, 0.01f, 0.001f, 1000.0f);
                if (lensChanged && camera.projection == CameraProjectionMode::Perspective)
                {
                    SyncFovFromLens(camera);
                }

                DrawSectionLabel("Output", "Control the output aspect ratio and frame clear settings for this camera.");
                CheckboxWithTooltip("Use Viewport Aspect Ratio", &camera.useViewportAspectRatio);
                if (!camera.useViewportAspectRatio)
                {
                    DragFloatWithTooltip("Aspect Ratio", &camera.aspectRatio, 0.01f, 0.001f, 100.0f);
                }
                CheckboxWithTooltip("Constrain Aspect Ratio", &camera.constrainAspectRatio);

                int clearMode = static_cast<int>(camera.clearMode);
                const char* clearModeItems[] = { "Skybox", "Solid Color", "Depth Only", "Don't Clear" };
                if (ImGui::Combo("Clear Mode", &clearMode, clearModeItems, IM_ARRAYSIZE(clearModeItems)))
                {
                    camera.clearMode = static_cast<CameraClearMode>(clearMode);
                }
                ShowItemTooltipFromLabel("Clear Mode");
                if (camera.clearMode == CameraClearMode::SolidColor)
                {
                    ColorEdit4WithTooltip("Clear Color", camera.clearColor.data());
                }

                DrawSectionLabel("Rendering", "Rendering toggles, culling mask, exposure, and feed priority.");
                LayerMaskSelectorWithTooltip("Culling Mask", &camera.cullingMask);
                CheckboxWithTooltip("HDR", &camera.hdr);
                CheckboxWithTooltip("Allow Post Process", &camera.allowPostProcess);
                CheckboxWithTooltip("Allow MSAA", &camera.allowMSAA);
                CheckboxWithTooltip("Allow Motion Blur", &camera.allowMotionBlur);
                DragFloatWithTooltip("Exposure", &camera.exposure, 0.01f, -16.0f, 16.0f);
                DragIntWithTooltip("Render Priority", &camera.renderPriority, 1.0f, -1000, 1000);

                ClampCamera(camera);
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
            bool pointLightChanged = false;
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Configure omnidirectional local lighting.");
                ImGui::PushID("PointLightComponent");
                pointLightChanged |= CheckboxWithTooltip("Active", &pointLight.active);
                int pointLightType = 0;
                const char* pointLightTypeItems[] = { "Point" };
                ImGui::BeginDisabled();
                ImGui::Combo("Type", &pointLightType, pointLightTypeItems, IM_ARRAYSIZE(pointLightTypeItems));
                ImGui::EndDisabled();
                ShowItemTooltipFromLabel("Type");

                int mode = static_cast<int>(pointLight.mode);
                const char* modeItems[] = { "Realtime", "Baked", "Mixed" };
                if (ImGui::Combo("Mode", &mode, modeItems, IM_ARRAYSIZE(modeItems)))
                {
                    pointLight.mode = static_cast<PointLightMode>(mode);
                    pointLightChanged = true;
                }
                ShowItemTooltipFromLabel("Mode");

                pointLightChanged |= DragFloatWithTooltip("Range", &pointLight.range, 0.05f, 0.05f, 1000.0f);
                pointLightChanged |= ColorEdit3WithTooltip("Color", pointLight.color.data());
                pointLightChanged |=
                    SliderFloatWithTooltip("Temperature", &pointLight.temperature, 1000.0f, 40000.0f, "%.0f K");
                const std::array<float, 3> temperaturePreview = KelvinToRgb(pointLight.temperature);
                ImGui::TextUnformatted("Temperature Preview");
                ImGui::ColorButton(
                    "##PointLightTemperaturePreview",
                    ImVec4(temperaturePreview[0], temperaturePreview[1], temperaturePreview[2], 1.0f),
                    ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop,
                    ImVec2(24.0f, 24.0f));
                ShowItemTooltip("Preview of the blackbody tint applied by the Temperature control.");
                pointLightChanged |= DragFloatWithTooltip("Intensity", &pointLight.intensity, 0.05f, 0.0f, 256.0f);
                pointLightChanged |=
                    DragFloatWithTooltip("Indirect Multiplier", &pointLight.indirectMultiplier, 0.01f, 0.0f, 8.0f);
                pointLightChanged |=
                    DragFloatWithTooltip("Attenuation", &pointLight.attenuation, 0.01f, 0.01f, 16.0f);
                pointLightChanged |= CheckboxWithTooltip("Cast Shadows", &pointLight.castShadows);
                if (pointLight.castShadows)
                {
                    int shadowType = static_cast<int>(pointLight.shadowType);
                    const char* shadowTypeItems[] = { "Hard Shadows", "Soft Shadows" };
                    if (ImGui::Combo("Shadow Type", &shadowType, shadowTypeItems, IM_ARRAYSIZE(shadowTypeItems)))
                    {
                        pointLight.shadowType = static_cast<PointLightShadowType>(shadowType);
                        pointLightChanged = true;
                    }
                    ShowItemTooltipFromLabel("Shadow Type");
                    pointLightChanged |=
                        DragFloatWithTooltip("Shadow Bias", &pointLight.shadowBias, 0.0001f, 0.0f, 0.05f, "%.4f");
                    if (ShadowResolutionComboWithTooltip("Shadow Resolution", &pointLight.shadowResolution))
                    {
                        pointLight.shadowResolution = NormalizeShadowResolution(pointLight.shadowResolution);
                        pointLightChanged = true;
                    }
                    pointLightChanged |=
                        DragFloatWithTooltip("Baked Shadow Radius", &pointLight.bakedShadowRadius, 0.01f, 0.0f, 16.0f);
                }
                pointLightChanged |= CheckboxWithTooltip("Draw Halo", &pointLight.drawHalo);
                int renderMode = static_cast<int>(pointLight.renderMode);
                const char* renderModeItems[] = { "Auto", "Important", "Not Important" };
                if (ImGui::Combo("Render Mode", &renderMode, renderModeItems, IM_ARRAYSIZE(renderModeItems)))
                {
                    pointLight.renderMode = static_cast<PointLightRenderMode>(renderMode);
                    pointLightChanged = true;
                }
                ShowItemTooltipFromLabel("Render Mode");
                pointLightChanged |= LayerMaskSelectorWithTooltip("Culling Mask", &pointLight.cullingMask);
                pointLight.intensity = std::max(pointLight.intensity, 0.0f);
                pointLight.indirectMultiplier = std::max(pointLight.indirectMultiplier, 0.0f);
                pointLight.range = std::max(pointLight.range, 0.05f);
                pointLight.temperature = std::clamp(pointLight.temperature, 1000.0f, 40000.0f);
                pointLight.attenuation = std::max(pointLight.attenuation, 0.001f);
                pointLight.shadowBias = std::max(pointLight.shadowBias, 0.0f);
                pointLight.shadowResolution = NormalizeShadowResolution(pointLight.shadowResolution);
                pointLight.bakedShadowRadius = std::max(pointLight.bakedShadowRadius, 0.0f);
                ImGui::PopID();
            }

            if (pointLightChanged && context.markSceneRenderCacheDirty)
            {
                context.markSceneRenderCacheDirty();
            }

            if (ButtonWithTooltip("Remove Point Light Component"))
            {
                registry.remove<PointLightComponent>(context.selectedEntity);
                if (context.markSceneRenderCacheDirty)
                {
                    context.markSceneRenderCacheDirty();
                }
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
