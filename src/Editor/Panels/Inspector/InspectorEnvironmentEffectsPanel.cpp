#include "Luma/Editor/Panels/Inspector/InspectorEnvironmentEffectsPanel.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Core/App/Project.h"
#include "Luma/Editor/Panels/Assets/MaterialTextureAssetPickerPanel.h"
#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/PostProcessComponent.h"
#include "Luma/Scene/SkyLightComponent.h"

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

        template <typename... Args>
        bool ColorEdit3WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::ColorEdit3(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }

        const char* SkyLightTypeLabel(const SceneSkyLightType type)
        {
            switch (type)
            {
            case SceneSkyLightType::Color:
                return "Color";
            case SceneSkyLightType::HDRI:
                return "HDRI";
            case SceneSkyLightType::Procedural:
                return "Procedural";
            default:
                return "HDRI";
            }
        }

        const char* SkyUpdateModeLabel(const SceneSkyUpdateMode mode)
        {
            switch (mode)
            {
            case SceneSkyUpdateMode::Static:
                return "Static";
            case SceneSkyUpdateMode::Dynamic:
                return "Dynamic";
            default:
                return "Static";
            }
        }

        const char* ToneMappingOperatorLabel(const ToneMappingOperator toneMappingOperator)
        {
            switch (toneMappingOperator)
            {
            case ToneMappingOperator::Linear:
                return "Linear";
            case ToneMappingOperator::Reinhard:
                return "Reinhard";
            case ToneMappingOperator::ACES:
                return "ACES";
            default:
                return "ACES";
            }
        }
    }

    void InspectorEnvironmentEffectsPanel::Draw(const InspectorEnvironmentEffectsPanelContext& context)
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

        if (registry.all_of<SkyLightComponent>(context.selectedEntity))
        {
            auto& skyLight = registry.get<SkyLightComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Sky Light", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Configure sky lighting type, environment, and atmospheric controls.");
                ImGui::PushID("SkyLightComponent");
                bool skyLightChanged = false;
                bool skyVisualCommitRequested = false;
                bool skyIblRebuildRequested = false;
                const auto registerImmediateChange =
                    [&](const bool changed, const bool affectsSkyVisual = false, const bool rebuildIbl = false)
                    {
                        skyLightChanged |= changed;
                        skyVisualCommitRequested |= changed && affectsSkyVisual;
                        skyIblRebuildRequested |= changed && rebuildIbl;
                    };
                const auto registerDeferredChange =
                    [&](const bool changed, const bool affectsSkyVisual = false, const bool rebuildIbl = false)
                    {
                        skyLightChanged |= changed;
                        skyVisualCommitRequested |= changed && affectsSkyVisual;
                        const bool committed = ImGui::IsItemDeactivatedAfterEdit() || (changed && !ImGui::IsItemActive());
                        skyIblRebuildRequested |= committed && rebuildIbl;
                    };

                MaterialTextureAssetPickerPanelContext assetPickerContext;
                assetPickerContext.pickerService = context.materialTextureAssetPickerService;
                assetPickerContext.roots = context.contentRoots;
                assetPickerContext.projectLoaded = Project::IsLoaded();
                assetPickerContext.projectAssetsPath = Project::IsLoaded() ? Project::GetAssetsPath() : std::filesystem::path {};
                assetPickerContext.resolveAssetPath = context.resolveAssetPath;
                assetPickerContext.getThumbnail = context.getThumbnail;

                registerImmediateChange(CheckboxWithTooltip("Active", &skyLight.active), true);
                registerDeferredChange(ColorEdit3WithTooltip("Color", skyLight.color.data()));
                registerDeferredChange(
                    DragFloatWithTooltip("Intensity", &skyLight.intensity, 0.01f, 0.0f, 16.0f),
                    true);
                registerImmediateChange(CheckboxWithTooltip("Cast Shadows", &skyLight.castShadows));

                int skyTypeIndex = static_cast<int>(skyLight.skyLightType);
                const char* skyTypeItems[] = { "Color", "HDRI", "Procedural" };
                if (ComboWithTooltip("Type", &skyTypeIndex, skyTypeItems, IM_ARRAYSIZE(skyTypeItems)))
                {
                    skyLight.skyLightType = static_cast<SceneSkyLightType>(skyTypeIndex);
                    registerImmediateChange(true, true, true);
                }

                ImGui::TextDisabled("Selected Type: %s", SkyLightTypeLabel(skyLight.skyLightType));

                if (skyLight.skyLightType == SceneSkyLightType::Color)
                {
                    registerDeferredChange(ColorEdit3WithTooltip("Sky Color", skyLight.skyColor.data()), true);
                    registerDeferredChange(
                        DragFloatWithTooltip("Color Intensity", &skyLight.colorIntensity, 0.01f, 0.0f, 32.0f),
                        true);
                }
                else if (skyLight.skyLightType == SceneSkyLightType::HDRI)
                {
                    if (context.materialTextureAssetPickerPanel != nullptr &&
                        context.materialTextureAssetPickerService != nullptr)
                    {
                        const std::string previousEnvironmentMap = skyLight.environmentMap;
                        context.materialTextureAssetPickerPanel->DrawTextureSelector(
                            assetPickerContext,
                            "Skybox (Equirect HDR)",
                            skyLight.environmentMap,
                            "##SkyHdrEnvironmentPicker",
                            "Assign the equirectangular HDR sky texture. Supports drag and drop from Content Browser.");
                        registerImmediateChange(skyLight.environmentMap != previousEnvironmentMap, true, true);
                    }
                    ImGui::TextDisabled("Mapping: Equirectangular");

                    if (context.materialTextureAssetPickerPanel != nullptr &&
                        context.materialTextureAssetPickerService != nullptr)
                    {
                        const std::string previousIrradianceMap = skyLight.irradianceMap;
                        context.materialTextureAssetPickerPanel->DrawTextureSelector(
                            assetPickerContext,
                            "Irradiance Override",
                            skyLight.irradianceMap,
                            "##SkyIrradiancePicker",
                            "Optional irradiance cubemap override for sky diffuse lighting.");
                        registerImmediateChange(skyLight.irradianceMap != previousIrradianceMap);
                    }

                    if (context.materialTextureAssetPickerPanel != nullptr &&
                        context.materialTextureAssetPickerService != nullptr)
                    {
                        const std::string previousPrefilteredMap = skyLight.prefilteredReflectionMap;
                        context.materialTextureAssetPickerPanel->DrawTextureSelector(
                            assetPickerContext,
                            "Reflection Override",
                            skyLight.prefilteredReflectionMap,
                            "##SkyReflectionPicker",
                            "Optional prefiltered reflection environment override.");
                        registerImmediateChange(skyLight.prefilteredReflectionMap != previousPrefilteredMap);
                    }

                    if (context.materialTextureAssetPickerPanel != nullptr &&
                        context.materialTextureAssetPickerService != nullptr)
                    {
                        const std::string previousBrdfLut = skyLight.brdfLut;
                        context.materialTextureAssetPickerPanel->DrawTextureSelector(
                            assetPickerContext,
                            "BRDF LUT",
                            skyLight.brdfLut,
                            "##SkyBrdfLutPicker",
                            "Optional BRDF integration lookup texture override.");
                        registerImmediateChange(skyLight.brdfLut != previousBrdfLut);
                    }

                    registerDeferredChange(
                        DragFloatWithTooltip("Rotation (deg)", &skyLight.rotation, 0.1f, -360.0f, 360.0f),
                        true);
                    registerDeferredChange(
                        DragFloatWithTooltip("Diffuse Intensity", &skyLight.diffuseIntensity, 0.01f, 0.0f, 16.0f),
                        true);
                    registerDeferredChange(
                        DragFloatWithTooltip(
                            "Reflection Intensity",
                            &skyLight.reflectionIntensity,
                            0.01f,
                            0.0f,
                            16.0f),
                        true);
                }
                else
                {
                    registerDeferredChange(ColorEdit3WithTooltip("Sky Color", skyLight.skyColor.data()), true);
                    registerDeferredChange(
                        DragFloatWithTooltip("Color Intensity", &skyLight.colorIntensity, 0.01f, 0.0f, 32.0f),
                        true);
                    registerDeferredChange(
                        DragFloatWithTooltip("Rotation (deg)", &skyLight.rotation, 0.1f, -360.0f, 360.0f),
                        true);
                }

                registerDeferredChange(
                    DragFloatWithTooltip("Exposure EV", &skyLight.exposureEV, 0.05f, -16.0f, 16.0f));
                registerDeferredChange(
                    DragFloatWithTooltip("Skybox Exposure EV", &skyLight.skyboxExposureEV, 0.05f, -16.0f, 16.0f));
                registerDeferredChange(
                    DragFloatWithTooltip("Sun Intensity Mult", &skyLight.sunIntensityMultiplier, 0.01f, 0.0f, 16.0f));
                registerDeferredChange(
                    DragFloatWithTooltip("Sun Specular Mult", &skyLight.sunSpecularMultiplier, 0.01f, 0.0f, 16.0f));
                registerImmediateChange(CheckboxWithTooltip("Auto Exposure", &skyLight.autoExposureEnabled));
                if (skyLight.autoExposureEnabled)
                {
                    registerDeferredChange(
                        DragFloatWithTooltip("Auto EV Min", &skyLight.autoExposureMinEV, 0.05f, -16.0f, 16.0f));
                    registerDeferredChange(
                        DragFloatWithTooltip("Auto EV Max", &skyLight.autoExposureMaxEV, 0.05f, -16.0f, 16.0f));
                    registerDeferredChange(
                        DragFloatWithTooltip("Expose Speed Up", &skyLight.autoExposureSpeedUp, 0.05f, 0.05f, 20.0f));
                    registerDeferredChange(
                        DragFloatWithTooltip(
                            "Expose Speed Down",
                            &skyLight.autoExposureSpeedDown,
                            0.05f,
                            0.05f,
                            20.0f));
                }

                registerDeferredChange(
                    DragFloatWithTooltip("AO Strength", &skyLight.ambientOcclusionStrength, 0.01f, 0.0f, 4.0f));
                registerImmediateChange(CheckboxWithTooltip("Affect AO", &skyLight.affectAmbientOcclusion));
                registerImmediateChange(
                    CheckboxWithTooltip("Lower Hemisphere Black", &skyLight.lowerHemisphereIsBlack),
                    true);
                if (!skyLight.lowerHemisphereIsBlack)
                {
                    registerDeferredChange(
                        ColorEdit3WithTooltip("Lower Hemisphere Color", skyLight.lowerHemisphereColor.data()),
                        true);
                }

                registerDeferredChange(
                    DragFloatWithTooltip("Blend", &skyLight.blendFactor, 0.01f, 0.0f, 1.0f),
                    true);
                registerImmediateChange(InputIntWithTooltip("Priority", &skyLight.priority));
                registerImmediateChange(CheckboxWithTooltip("Real-Time Capture", &skyLight.realTimeCapture));
                if (skyLight.realTimeCapture)
                {
                    registerDeferredChange(
                        DragFloatWithTooltip(
                            "Capture Interval",
                            &skyLight.captureUpdateInterval,
                            0.01f,
                            0.02f,
                            60.0f));
                }

                registerDeferredChange(
                    DragFloatWithTooltip(
                        "Volumetric Scatter",
                        &skyLight.volumetricScatteringIntensity,
                        0.01f,
                        0.0f,
                        16.0f));
                registerImmediateChange(CheckboxWithTooltip("Affect Fog", &skyLight.affectFog));

                int updateModeIndex = static_cast<int>(skyLight.updateMode);
                const char* updateModeItems[] = { "Static", "Dynamic" };
                if (ComboWithTooltip("Update Mode", &updateModeIndex, updateModeItems, IM_ARRAYSIZE(updateModeItems)))
                {
                    skyLight.updateMode = static_cast<SceneSkyUpdateMode>(updateModeIndex);
                    registerImmediateChange(true);
                }

                ImGui::TextDisabled("Update Mode: %s", SkyUpdateModeLabel(skyLight.updateMode));

                if (ButtonWithTooltip("Rebuild Sky IBL"))
                {
                    skyLight.rebuildIBLRequested = true;
                }

                if (skyLightChanged)
                {
                    skyLight.sunIntensityMultiplier = std::max(skyLight.sunIntensityMultiplier, 0.0f);
                    skyLight.sunSpecularMultiplier = std::max(skyLight.sunSpecularMultiplier, 0.0f);
                    skyLight.autoExposureMinEV = std::clamp(skyLight.autoExposureMinEV, -16.0f, 16.0f);
                    skyLight.autoExposureMaxEV = std::clamp(skyLight.autoExposureMaxEV, skyLight.autoExposureMinEV, 16.0f);
                    skyLight.autoExposureSpeedUp = std::max(skyLight.autoExposureSpeedUp, 0.05f);
                    skyLight.autoExposureSpeedDown = std::max(skyLight.autoExposureSpeedDown, 0.05f);
                    skyLight.previewRefreshRequested |= skyVisualCommitRequested;
                    if (skyIblRebuildRequested)
                    {
                        skyLight.rebuildIBLRequested = true;
                    }
                    if (skyVisualCommitRequested && context.onEnvironmentChanged)
                    {
                        context.onEnvironmentChanged();
                    }
                }
                ImGui::PopID();
            }
            if (ButtonWithTooltip("Remove Sky Light Component"))
            {
                registry.remove<SkyLightComponent>(context.selectedEntity);
                if (context.onEnvironmentChanged)
                {
                    context.onEnvironmentChanged();
                }
            }
        }

        if (registry.all_of<PostProcessComponent>(context.selectedEntity))
        {
            auto& postProcess = registry.get<PostProcessComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Post Process", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Configure tone mapping and color grading for this post-process volume.");
                ImGui::PushID("PostProcessComponent");
                bool postProcessChanged = false;
                postProcessChanged |= CheckboxWithTooltip("Active", &postProcess.active);
                postProcessChanged |= InputIntWithTooltip("Priority", &postProcess.priority);
                postProcessChanged |= CheckboxWithTooltip("Unbound", &postProcess.unbound);
                if (!postProcess.unbound)
                {
                    postProcessChanged |= DragFloat3WithTooltip("Volume Extents", postProcess.volumeExtents.data(), 0.05f, 0.05f, 10000.0f);
                    postProcessChanged |= DragFloatWithTooltip("Blend Distance", &postProcess.blendDistance, 0.05f, 0.0f, 10000.0f);
                }
                postProcessChanged |= CheckboxWithTooltip("Tone Mapping", &postProcess.toneMappingEnabled);

                int toneMappingOperatorIndex = static_cast<int>(postProcess.toneMappingOperator);
                const char* toneMappingItems[] = { "Linear", "Reinhard", "ACES" };
                if (ComboWithTooltip("Tone Mapper", &toneMappingOperatorIndex, toneMappingItems, IM_ARRAYSIZE(toneMappingItems)))
                {
                    postProcess.toneMappingOperator = static_cast<ToneMappingOperator>(toneMappingOperatorIndex);
                    postProcessChanged = true;
                }

                postProcessChanged |= DragFloatWithTooltip("Exposure EV", &postProcess.exposureCompensationEV, 0.05f, -16.0f, 16.0f);
                postProcessChanged |= DragFloatWithTooltip("Eye Adapt Comp EV", &postProcess.eyeAdaptationCompensationEV, 0.05f, -8.0f, 8.0f);
                postProcessChanged |= DragFloatWithTooltip("White Point", &postProcess.whitePoint, 0.01f, 0.1f, 32.0f);
                postProcessChanged |= ColorEdit3WithTooltip("Color Filter", postProcess.colorFilter.data());
                postProcessChanged |= ColorEdit3WithTooltip("Color Balance", postProcess.colorBalance.data());
                postProcessChanged |= DragFloatWithTooltip("Saturation", &postProcess.saturation, 0.01f, 0.0f, 2.0f);
                postProcessChanged |= DragFloatWithTooltip("Contrast", &postProcess.contrast, 0.01f, 0.0f, 2.5f);
                postProcessChanged |= DragFloatWithTooltip("Gamma", &postProcess.gamma, 0.01f, 0.1f, 4.0f);
                postProcessChanged |= DragFloatWithTooltip("Film Shoulder", &postProcess.filmCurveShoulder, 0.01f, 0.1f, 4.0f);
                postProcessChanged |= DragFloatWithTooltip("Film Linear", &postProcess.filmCurveLinear, 0.01f, 0.1f, 4.0f);
                postProcessChanged |= DragFloatWithTooltip("Film Toe", &postProcess.filmCurveToe, 0.01f, 0.1f, 4.0f);
                ImGui::TextDisabled("Tone Mapper: %s", ToneMappingOperatorLabel(postProcess.toneMappingOperator));
                ImGui::TextDisabled("Attach to any entity; disable Unbound to use Transform + Volume Extents + Blend Distance.");
                ImGui::TextDisabled("CryEngine-style HDR: eye adaptation compensation, color balance, film curve.");
                if (postProcess.bloomEnabled || postProcess.bloomIntensity > 0.0f)
                {
                    ImGui::TextDisabled("Bloom settings exist on this volume but are hidden until the fullscreen bloom pass ships.");
                }
                else
                {
                    ImGui::TextDisabled("Bloom controls are hidden until the fullscreen bloom pass ships.");
                }

                if (postProcessChanged)
                {
                    postProcess.volumeExtents[0] = std::max(postProcess.volumeExtents[0], 0.05f);
                    postProcess.volumeExtents[1] = std::max(postProcess.volumeExtents[1], 0.05f);
                    postProcess.volumeExtents[2] = std::max(postProcess.volumeExtents[2], 0.05f);
                    postProcess.blendDistance = std::max(postProcess.blendDistance, 0.0f);
                    postProcess.whitePoint = std::max(postProcess.whitePoint, 0.1f);
                    postProcess.saturation = std::max(postProcess.saturation, 0.0f);
                    postProcess.contrast = std::max(postProcess.contrast, 0.0f);
                    postProcess.gamma = std::max(postProcess.gamma, 0.1f);
                    postProcess.filmCurveShoulder = std::max(postProcess.filmCurveShoulder, 0.1f);
                    postProcess.filmCurveLinear = std::max(postProcess.filmCurveLinear, 0.1f);
                    postProcess.filmCurveToe = std::max(postProcess.filmCurveToe, 0.1f);
                    if (context.onEnvironmentChanged)
                    {
                        context.onEnvironmentChanged();
                    }
                }

                ImGui::PopID();
            }
            if (ButtonWithTooltip("Remove Post Process Component"))
            {
                registry.remove<PostProcessComponent>(context.selectedEntity);
                if (context.onEnvironmentChanged)
                {
                    context.onEnvironmentChanged();
                }
            }
        }
    }
}
