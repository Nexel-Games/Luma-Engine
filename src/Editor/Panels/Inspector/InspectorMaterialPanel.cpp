#include "Luma/Editor/Panels/Inspector/InspectorMaterialPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>

#include <imgui.h>

#include "Luma/Core/App/Project.h"
#include "Luma/Editor/Panels/Assets/MaterialTextureAssetPickerPanel.h"
#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/MaterialComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"

namespace Luma::Editor
{
    namespace
    {
        std::string TrimCopy(std::string value)
        {
            const auto notWhitespace = [](const unsigned char character)
            {
                return !std::isspace(character);
            };

            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notWhitespace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notWhitespace).base(), value.end());
            return value;
        }

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
        bool ComboWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::Combo(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Select ");
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
        bool SmallButtonWithTooltip(const char* label, Args&&... args)
        {
            const bool pressed = ImGui::SmallButton(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return pressed;
        }

        template <typename... Args>
        bool InputTextWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::InputText(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
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
        bool DragFloat2WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragFloat2(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
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
        bool InputIntWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::InputInt(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        bool MaterialPropertiesEqual(const MaterialComponent& lhs, const MaterialComponent& rhs)
        {
            return lhs.name == rhs.name &&
                lhs.shader == rhs.shader &&
                lhs.renderingMode == rhs.renderingMode &&
                lhs.albedoColor == rhs.albedoColor &&
                lhs.albedoTexture == rhs.albedoTexture &&
                lhs.metallicTexture == rhs.metallicTexture &&
                lhs.metallic == rhs.metallic &&
                lhs.smoothness == rhs.smoothness &&
                lhs.smoothnessSource == rhs.smoothnessSource &&
                lhs.enableHighlights == rhs.enableHighlights &&
                lhs.enableReflections == rhs.enableReflections &&
                lhs.normalTexture == rhs.normalTexture &&
                lhs.normalScale == rhs.normalScale &&
                lhs.heightTexture == rhs.heightTexture &&
                lhs.heightScale == rhs.heightScale &&
                lhs.occlusionTexture == rhs.occlusionTexture &&
                lhs.occlusionStrength == rhs.occlusionStrength &&
                lhs.emissionEnabled == rhs.emissionEnabled &&
                lhs.emissionTexture == rhs.emissionTexture &&
                lhs.emissionColor == rhs.emissionColor &&
                lhs.emissionIntensity == rhs.emissionIntensity &&
                lhs.globalIllumination == rhs.globalIllumination &&
                lhs.detailMaskTexture == rhs.detailMaskTexture &&
                lhs.tiling == rhs.tiling &&
                lhs.offset == rhs.offset &&
                lhs.detailAlbedoTexture == rhs.detailAlbedoTexture &&
                lhs.detailNormalTexture == rhs.detailNormalTexture &&
                lhs.detailNormalScale == rhs.detailNormalScale &&
                lhs.detailTiling == rhs.detailTiling &&
                lhs.detailOffset == rhs.detailOffset &&
                lhs.uvSet == rhs.uvSet;
        }

        const char* MaterialRenderingModeLabel(const MaterialRenderingMode mode)
        {
            switch (mode)
            {
            case MaterialRenderingMode::Opaque:
                return "Opaque";
            case MaterialRenderingMode::Cutout:
                return "Cutout";
            case MaterialRenderingMode::Fade:
                return "Fade";
            case MaterialRenderingMode::Transparent:
                return "Transparent";
            default:
                return "Opaque";
            }
        }

        const char* MaterialSmoothnessSourceLabel(const MaterialSmoothnessSource source)
        {
            switch (source)
            {
            case MaterialSmoothnessSource::MetallicAlpha:
                return "Metallic Alpha";
            case MaterialSmoothnessSource::AlbedoAlpha:
                return "Albedo Alpha";
            default:
                return "Metallic Alpha";
            }
        }

        const char* MaterialGlobalIlluminationModeLabel(const MaterialGlobalIlluminationMode mode)
        {
            switch (mode)
            {
            case MaterialGlobalIlluminationMode::Realtime:
                return "Realtime";
            case MaterialGlobalIlluminationMode::Baked:
                return "Baked";
            case MaterialGlobalIlluminationMode::None:
                return "None";
            default:
                return "Realtime";
            }
        }
    }

    void InspectorMaterialPanel::Draw(const InspectorMaterialPanelContext& context)
    {
        if (context.scene == nullptr || context.selectedEntity == entt::null)
        {
            return;
        }

        auto& registry = context.scene->GetRegistry();
        if (!registry.valid(context.selectedEntity) || !registry.all_of<MaterialComponent>(context.selectedEntity))
        {
            return;
        }

        auto& material = registry.get<MaterialComponent>(context.selectedEntity);
        const MaterialComponent materialBeforeEdit = material;
        const std::string previousSharedMaterial = material.sharedMaterial;
        ImGui::Separator();
        if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ShowItemTooltip("Unity-style material properties stored on the entity as pure scene data.");
        ImGui::PushID("MaterialComponent");

        auto drawStringField = [](const char* label, std::string& value, const char* tooltip)
        {
            std::array<char, 260> buffer {};
            std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
            if (InputTextWithTooltip(label, buffer.data(), buffer.size()))
            {
                value = buffer.data();
            }
            ShowItemTooltip(tooltip);
        };

        MaterialTextureAssetPickerPanelContext assetPickerContext;
        assetPickerContext.pickerService = context.pickerService;
        assetPickerContext.roots = context.contentRoots;
        assetPickerContext.projectLoaded = Project::IsLoaded();
        assetPickerContext.projectAssetsPath = Project::IsLoaded() ? Project::GetAssetsPath() : std::filesystem::path {};
        assetPickerContext.resolveAssetPath = context.resolveAssetPath;
        assetPickerContext.getThumbnail = context.getThumbnail;
        assetPickerContext.markSceneRenderCacheDirty = context.markSceneMaterialsDirty;

        auto drawMaterialAssetSelector =
            [&](const char* label, std::string& value, const char* popupId, const char* tooltip)
        {
            if (context.pickerPanel == nullptr)
            {
                return;
            }

            context.pickerPanel->DrawMaterialSelector(assetPickerContext, label, value, popupId, tooltip);
        };

        auto drawTextureAssetSelector =
            [&](const char* label, std::string& value, const char* popupId, const char* tooltip)
        {
            if (context.pickerPanel == nullptr)
            {
                return;
            }

            context.pickerPanel->DrawTextureSelector(assetPickerContext, label, value, popupId, tooltip);
        };

        auto markSceneRenderCacheDirty = [&]()
        {
            if (context.markSceneMaterialsDirty)
            {
                context.markSceneMaterialsDirty();
            }
        };

        auto logMaterialWarning = [&](const std::string_view message)
        {
            if (context.logMaterialWarning)
            {
                context.logMaterialWarning(message);
            }
        };

        auto resolveAssetPath = [&](const std::string& assetPath)
        {
            return context.resolveAssetPath ? context.resolveAssetPath(assetPath) : std::filesystem::path {};
        };

        auto getThumbnail = [&](const std::filesystem::path& assetPath)
        {
            return context.getThumbnail ? context.getThumbnail(assetPath, false) : nullptr;
        };

        auto resolveMaterialSlotNames = [&](const MeshRendererComponent& meshRenderer)
        {
            return context.resolveMaterialSlotNames
                ? context.resolveMaterialSlotNames(meshRenderer)
                : std::vector<std::string> {};
        };

        auto drawMaterialPreview = [&](const MaterialComponent& previewMaterial)
        {
            static int previewMode = 0;
            static float previewLightYawDegrees = -32.0f;
            static float previewLightPitchDegrees = -24.0f;

            ImGui::SeparatorText("Preview");
            if (ButtonWithTooltip("Sphere##MaterialPreviewModeSphere"))
            {
                previewMode = 0;
            }
            ImGui::SameLine();
            if (ButtonWithTooltip("Card##MaterialPreviewModeCard"))
            {
                previewMode = 1;
            }
            ImGui::SameLine();
            if (SmallButtonWithTooltip("Reset Light##MaterialPreviewLightReset"))
            {
                previewLightYawDegrees = -32.0f;
                previewLightPitchDegrees = -24.0f;
            }
            ShowItemTooltip("Reset preview light direction.");

            const float previewWidth = std::max(180.0f, ImGui::GetContentRegionAvail().x);
            const float previewHeight = 176.0f;
            const ImVec2 previewSize(previewWidth, previewHeight);
            const ImVec2 previewMin = ImGui::GetCursorScreenPos();
            const ImVec2 previewMax(previewMin.x + previewSize.x, previewMin.y + previewSize.y);

            ImGui::InvisibleButton("##MaterialPreviewCanvas", previewSize);
            ShowItemTooltip("Inspector-only preview of the current material settings.");
            const bool previewHovered = ImGui::IsItemHovered();
            const bool previewActive = ImGui::IsItemActive();
            if (previewActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
            {
                const ImVec2 delta = ImGui::GetIO().MouseDelta;
                previewLightYawDegrees += delta.x * 0.45f;
                previewLightPitchDegrees = std::clamp(previewLightPitchDegrees + delta.y * 0.35f, -80.0f, 80.0f);
            }

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilledMultiColor(
                previewMin,
                previewMax,
                IM_COL32(38, 44, 56, 255),
                IM_COL32(26, 30, 38, 255),
                IM_COL32(14, 16, 22, 255),
                IM_COL32(20, 24, 30, 255));
            drawList->AddRect(previewMin, previewMax, IM_COL32(72, 79, 92, 255), 6.0f);

            const std::filesystem::path albedoPath = resolveAssetPath(previewMaterial.albedoTexture);
            const std::filesystem::path metallicPath = resolveAssetPath(previewMaterial.metallicTexture);
            const std::filesystem::path normalPath = resolveAssetPath(previewMaterial.normalTexture);
            const std::filesystem::path occlusionPath = resolveAssetPath(previewMaterial.occlusionTexture);
            const std::filesystem::path emissionPath = resolveAssetPath(previewMaterial.emissionTexture);
            void* albedoThumbnail = nullptr;
            void* metallicThumbnail = nullptr;
            void* normalThumbnail = nullptr;
            void* occlusionThumbnail = nullptr;
            void* emissionThumbnail = nullptr;
            if (!previewMaterial.albedoTexture.empty() && !albedoPath.empty())
            {
                albedoThumbnail = getThumbnail(albedoPath);
            }
            if (!previewMaterial.metallicTexture.empty() && !metallicPath.empty())
            {
                metallicThumbnail = getThumbnail(metallicPath);
            }
            if (!previewMaterial.normalTexture.empty() && !normalPath.empty())
            {
                normalThumbnail = getThumbnail(normalPath);
            }
            if (!previewMaterial.occlusionTexture.empty() && !occlusionPath.empty())
            {
                occlusionThumbnail = getThumbnail(occlusionPath);
            }
            if (!previewMaterial.emissionTexture.empty() && !emissionPath.empty())
            {
                emissionThumbnail = getThumbnail(emissionPath);
            }

            const ImVec4 albedoColor(
                previewMaterial.albedoColor[0],
                previewMaterial.albedoColor[1],
                previewMaterial.albedoColor[2],
                previewMaterial.albedoColor[3]);
            const float metallic = std::clamp(previewMaterial.metallic, 0.0f, 1.0f);
            const float smoothness = std::clamp(previewMaterial.smoothness, 0.0f, 1.0f);
            const float occlusion = std::clamp(previewMaterial.occlusionStrength, 0.0f, 1.0f);
            const float normalInfluence = std::clamp(previewMaterial.normalScale / 4.0f, 0.0f, 1.0f);
            const float heightInfluence = std::clamp(previewMaterial.heightScale * 12.0f, 0.0f, 1.0f);
            constexpr float kPi = 3.14159265359f;
            const float lightYawRadians = previewLightYawDegrees * (kPi / 180.0f);
            const float lightPitchRadians = previewLightPitchDegrees * (kPi / 180.0f);
            const float lightDirX = std::cos(lightYawRadians) * std::cos(lightPitchRadians);
            const float lightDirY = std::sin(lightPitchRadians);
            const float lightDirZ = std::sin(lightYawRadians) * std::cos(lightPitchRadians);
            const float lightFacing = std::clamp((-lightDirZ * 0.5f) + 0.5f, 0.0f, 1.0f);
            const ImVec4 emissionColor(
                previewMaterial.emissionColor[0],
                previewMaterial.emissionColor[1],
                previewMaterial.emissionColor[2],
                previewMaterial.emissionEnabled ? std::clamp(previewMaterial.emissionIntensity / 4.0f, 0.0f, 1.0f) : 0.0f);
            const ImVec4 baseColorVec(
                std::clamp(albedoColor.x * std::lerp(0.92f, 0.72f, metallic), 0.0f, 1.0f),
                std::clamp(albedoColor.y * std::lerp(0.92f, 0.72f, metallic), 0.0f, 1.0f),
                std::clamp(albedoColor.z * std::lerp(0.92f, 0.72f, metallic), 0.0f, 1.0f),
                1.0f);
            const ImVec4 highlightColorVec(
                std::clamp(std::lerp(baseColorVec.x, 0.96f, 0.24f + metallic * 0.60f + lightFacing * 0.16f), 0.0f, 1.0f),
                std::clamp(std::lerp(baseColorVec.y, 0.97f, 0.24f + metallic * 0.60f + lightFacing * 0.16f), 0.0f, 1.0f),
                std::clamp(std::lerp(baseColorVec.z, 0.99f, 0.24f + metallic * 0.60f + lightFacing * 0.16f), 0.0f, 1.0f),
                1.0f);
            const ImU32 baseColor = ImGui::GetColorU32(baseColorVec);
            const ImU32 litColor = ImGui::GetColorU32(ImVec4(
                std::clamp(highlightColorVec.x * (0.96f + metallic * 0.24f + lightFacing * 0.22f) + emissionColor.x * 0.35f, 0.0f, 1.0f),
                std::clamp(highlightColorVec.y * (0.96f + metallic * 0.24f + lightFacing * 0.22f) + emissionColor.y * 0.35f, 0.0f, 1.0f),
                std::clamp(highlightColorVec.z * (0.96f + metallic * 0.24f + lightFacing * 0.22f) + emissionColor.z * 0.35f, 0.0f, 1.0f),
                1.0f));
            const ImU32 shadowColor = ImGui::GetColorU32(ImVec4(
                std::clamp(baseColorVec.x * std::lerp(0.42f, 0.16f, occlusion + (1.0f - lightFacing) * 0.25f), 0.0f, 1.0f),
                std::clamp(baseColorVec.y * std::lerp(0.42f, 0.16f, occlusion + (1.0f - lightFacing) * 0.25f), 0.0f, 1.0f),
                std::clamp(baseColorVec.z * std::lerp(0.42f, 0.16f, occlusion + (1.0f - lightFacing) * 0.25f), 0.0f, 1.0f),
                1.0f));
            const ImU32 rimColor = ImGui::GetColorU32(ImVec4(
                std::clamp(highlightColorVec.x * (0.52f + metallic * 0.28f), 0.0f, 1.0f),
                std::clamp(highlightColorVec.y * (0.52f + metallic * 0.28f), 0.0f, 1.0f),
                std::clamp(highlightColorVec.z * (0.52f + metallic * 0.28f), 0.0f, 1.0f),
                0.75f));

            if (previewMode == 0)
            {
                const ImVec2 center(
                    previewMin.x + previewSize.x * 0.5f,
                    previewMin.y + previewSize.y * 0.56f);
                const float radius = std::min(previewSize.x, previewSize.y) * 0.26f;
                drawList->AddCircleFilled(
                    ImVec2(
                        center.x + radius * (0.12f + heightInfluence * 0.06f - lightDirX * 0.28f),
                        center.y + radius * (0.20f + heightInfluence * 0.10f - lightDirY * 0.24f)),
                    radius * (1.02f + heightInfluence * 0.04f),
                    shadowColor,
                    48);
                drawList->AddCircleFilled(center, radius, baseColor, 64);
                drawList->AddCircle(
                    center,
                    radius * (0.96f + normalInfluence * 0.03f),
                    rimColor,
                    64,
                    2.0f + metallic * 2.0f);
                drawList->AddCircleFilled(
                    ImVec2(
                        center.x - radius * (lightDirX * (0.26f + normalInfluence * 0.10f)),
                        center.y - radius * (lightDirY * (0.24f + normalInfluence * 0.07f))),
                    radius * std::lerp(0.26f, 0.72f, smoothness),
                    litColor,
                    48);
                if (normalInfluence > 1.0e-3f)
                {
                    for (int lineIndex = 0; lineIndex < 5; ++lineIndex)
                    {
                        const float t = static_cast<float>(lineIndex) / 4.0f;
                        const float y = center.y - radius * 0.55f + t * radius * 1.10f;
                        drawList->AddLine(
                            ImVec2(center.x - radius * 0.60f, y),
                            ImVec2(center.x + radius * 0.60f, y + radius * 0.08f * normalInfluence),
                            IM_COL32(255, 255, 255, static_cast<int>(36.0f + normalInfluence * 42.0f)),
                            1.0f);
                    }
                }
                if (previewMaterial.emissionEnabled && previewMaterial.emissionIntensity > 0.0f)
                {
                    drawList->AddCircle(
                        center,
                        radius * 1.08f,
                        ImGui::GetColorU32(ImVec4(
                            emissionColor.x,
                            emissionColor.y,
                            emissionColor.z,
                            std::clamp(previewMaterial.emissionIntensity / 6.0f, 0.08f, 0.45f))),
                        64,
                        4.0f);
                }
            }
            else
            {
                const ImVec2 cardMin(
                    previewMin.x + previewSize.x * 0.24f,
                    previewMin.y + previewSize.y * 0.24f);
                const ImVec2 cardMax(
                    previewMin.x + previewSize.x * 0.76f,
                    previewMin.y + previewSize.y * 0.76f);
                drawList->AddRectFilled(
                    ImVec2(cardMin.x + 8.0f - lightDirX * 8.0f, cardMin.y + 10.0f - lightDirY * 8.0f),
                    ImVec2(cardMax.x + 8.0f - lightDirX * 8.0f, cardMax.y + 10.0f - lightDirY * 8.0f),
                    shadowColor,
                    8.0f);
                drawList->AddRectFilledMultiColor(cardMin, cardMax, litColor, baseColor, shadowColor, baseColor);
                drawList->AddRect(cardMin, cardMax, IM_COL32(240, 240, 245, 120), 8.0f, 0, 1.5f);
                if (normalInfluence > 1.0e-3f || heightInfluence > 1.0e-3f)
                {
                    for (int lineIndex = 0; lineIndex < 6; ++lineIndex)
                    {
                        const float t = static_cast<float>(lineIndex) / 5.0f;
                        const float y = std::lerp(cardMin.y + 8.0f, cardMax.y - 8.0f, t);
                        drawList->AddLine(
                            ImVec2(cardMin.x + 8.0f, y),
                            ImVec2(cardMax.x - 8.0f, y + 8.0f * normalInfluence),
                            IM_COL32(255, 255, 255, static_cast<int>(24.0f + normalInfluence * 36.0f)),
                            1.0f);
                    }
                }
                if (previewMaterial.emissionEnabled && previewMaterial.emissionIntensity > 0.0f)
                {
                    drawList->AddRect(
                        ImVec2(cardMin.x - 2.0f, cardMin.y - 2.0f),
                        ImVec2(cardMax.x + 2.0f, cardMax.y + 2.0f),
                        ImGui::GetColorU32(ImVec4(
                            emissionColor.x,
                            emissionColor.y,
                            emissionColor.z,
                            std::clamp(previewMaterial.emissionIntensity / 6.0f, 0.10f, 0.45f))),
                        8.0f,
                        0,
                        3.0f);
                }
            }

            struct PreviewThumb
            {
                void* texture = nullptr;
                const char* label = "";
            };
            const std::array<PreviewThumb, 5> previewThumbs = {
                PreviewThumb { albedoThumbnail, "A" },
                PreviewThumb { metallicThumbnail, "M" },
                PreviewThumb { normalThumbnail, "N" },
                PreviewThumb { occlusionThumbnail, "O" },
                PreviewThumb { emissionThumbnail, "E" }
            };
            float thumbX = previewMin.x + 10.0f;
            for (const PreviewThumb& thumb : previewThumbs)
            {
                if (thumb.texture == nullptr)
                {
                    continue;
                }

                const ImVec2 thumbMin(thumbX, previewMin.y + 10.0f);
                const ImVec2 thumbMax(thumbMin.x + 38.0f, thumbMin.y + 38.0f);
                drawList->AddImage(
                    reinterpret_cast<ImTextureID>(thumb.texture),
                    thumbMin,
                    thumbMax,
                    ImVec2(0.0f, 1.0f),
                    ImVec2(1.0f, 0.0f));
                drawList->AddRect(thumbMin, thumbMax, IM_COL32(240, 240, 245, 96), 4.0f);
                drawList->AddText(ImVec2(thumbMin.x + 12.0f, thumbMax.y + 2.0f), IM_COL32(210, 214, 222, 220), thumb.label);
                thumbX += 48.0f;
            }

            drawList->AddText(
                ImVec2(previewMin.x + 12.0f, previewMax.y - 24.0f),
                IM_COL32(210, 214, 222, 255),
                previewMaterial.name.empty() ? "Material Preview" : previewMaterial.name.c_str());
            const std::string metricsText =
                "M " + std::to_string(static_cast<int>(metallic * 100.0f)) +
                "  S " + std::to_string(static_cast<int>(smoothness * 100.0f)) +
                "  AO " + std::to_string(static_cast<int>(occlusion * 100.0f));
            drawList->AddText(
                ImVec2(previewMax.x - ImGui::CalcTextSize(metricsText.c_str()).x - 12.0f, previewMax.y - 24.0f),
                IM_COL32(170, 176, 188, 230),
                metricsText.c_str());
            const ImVec2 lightWidgetCenter(previewMax.x - 34.0f, previewMin.y + 32.0f);
            drawList->AddCircle(lightWidgetCenter, 16.0f, IM_COL32(210, 214, 222, 120), 32, 1.0f);
            drawList->AddLine(
                lightWidgetCenter,
                ImVec2(lightWidgetCenter.x + lightDirX * 12.0f, lightWidgetCenter.y + lightDirY * 12.0f),
                IM_COL32(255, 230, 140, 220),
                2.0f);
            drawList->AddCircleFilled(
                ImVec2(lightWidgetCenter.x + lightDirX * 12.0f, lightWidgetCenter.y + lightDirY * 12.0f),
                3.0f,
                IM_COL32(255, 244, 180, 255),
                16);
            if (previewHovered)
            {
                drawList->AddText(
                    ImVec2(previewMin.x + 12.0f, previewMin.y + 12.0f),
                    IM_COL32(220, 224, 232, 220),
                    "Drag to rotate light");
            }

            ImGui::Dummy(ImVec2(0.0f, ImGui::GetStyle().ItemSpacing.y));
        };

        drawStringField("Name", material.name, "Material display name.");
        drawStringField("Shader", material.shader, "Shader name or shader family label.");
        drawMaterialAssetSelector(
            "Shared Material",
            material.sharedMaterial,
            "SharedMaterialPickerPopup",
            "Click to choose a material asset or drag one here from the Content Browser.");

        int renderingModeIndex = static_cast<int>(material.renderingMode);
        const char* renderingModeItems[] = { "Opaque", "Cutout", "Fade", "Transparent" };
        if (ComboWithTooltip("Rendering Mode", &renderingModeIndex, renderingModeItems, IM_ARRAYSIZE(renderingModeItems)))
        {
            renderingModeIndex = std::clamp(renderingModeIndex, 0, static_cast<int>(IM_ARRAYSIZE(renderingModeItems)) - 1);
            material.renderingMode = static_cast<MaterialRenderingMode>(renderingModeIndex);
        }
        ImGui::TextDisabled("Current Mode: %s", MaterialRenderingModeLabel(material.renderingMode));

        ImGui::SeparatorText("Main Maps");
        ColorEdit4WithTooltip("Albedo", material.albedoColor.data());
        drawTextureAssetSelector("Albedo Texture", material.albedoTexture, "AlbedoTexturePickerPopup", "Base color/albedo texture.");
        drawTextureAssetSelector("Metallic Texture", material.metallicTexture, "MetallicTexturePickerPopup", "Metallic texture.");
        DragFloatWithTooltip("Metallic", &material.metallic, 0.01f, 0.0f, 1.0f, "%.3f");
        DragFloatWithTooltip("Smoothness", &material.smoothness, 0.01f, 0.0f, 1.0f, "%.3f");

        int smoothnessSourceIndex = static_cast<int>(material.smoothnessSource);
        const char* smoothnessSourceItems[] = { "Metallic Alpha", "Albedo Alpha" };
        if (ComboWithTooltip(
                "Smoothness Source",
                &smoothnessSourceIndex,
                smoothnessSourceItems,
                IM_ARRAYSIZE(smoothnessSourceItems)))
        {
            smoothnessSourceIndex =
                std::clamp(smoothnessSourceIndex, 0, static_cast<int>(IM_ARRAYSIZE(smoothnessSourceItems)) - 1);
            material.smoothnessSource = static_cast<MaterialSmoothnessSource>(smoothnessSourceIndex);
        }
        ImGui::TextDisabled(
            "Current Smoothness Source: %s",
            MaterialSmoothnessSourceLabel(material.smoothnessSource));
        CheckboxWithTooltip("Highlights", &material.enableHighlights);
        CheckboxWithTooltip("Reflections", &material.enableReflections);

        drawTextureAssetSelector("Normal Map", material.normalTexture, "NormalTexturePickerPopup", "Normal map texture.");
        DragFloatWithTooltip("Normal Scale", &material.normalScale, 0.01f, 0.0f, 8.0f, "%.3f");
        drawTextureAssetSelector("Height Map", material.heightTexture, "HeightTexturePickerPopup", "Height or parallax texture.");
        DragFloatWithTooltip("Height Scale", &material.heightScale, 0.001f, 0.0f, 1.0f, "%.3f");
        drawTextureAssetSelector("Occlusion Map", material.occlusionTexture, "OcclusionTexturePickerPopup", "Ambient occlusion texture.");
        DragFloatWithTooltip("Occlusion Strength", &material.occlusionStrength, 0.01f, 0.0f, 1.0f, "%.3f");

        CheckboxWithTooltip("Emission Enabled", &material.emissionEnabled);
        drawTextureAssetSelector("Emission Map", material.emissionTexture, "EmissionTexturePickerPopup", "Emission texture.");
        ColorEdit4WithTooltip("Emission", material.emissionColor.data());
        DragFloatWithTooltip("Emission Intensity", &material.emissionIntensity, 0.01f, 0.0f, 64.0f, "%.3f");

        int giModeIndex = static_cast<int>(material.globalIllumination);
        const char* giModeItems[] = { "Realtime", "Baked", "None" };
        if (ComboWithTooltip("Global Illumination", &giModeIndex, giModeItems, IM_ARRAYSIZE(giModeItems)))
        {
            giModeIndex = std::clamp(giModeIndex, 0, static_cast<int>(IM_ARRAYSIZE(giModeItems)) - 1);
            material.globalIllumination = static_cast<MaterialGlobalIlluminationMode>(giModeIndex);
        }
        ImGui::TextDisabled(
            "Current GI Mode: %s",
            MaterialGlobalIlluminationModeLabel(material.globalIllumination));

        drawTextureAssetSelector("Detail Mask", material.detailMaskTexture, "DetailMaskTexturePickerPopup", "Detail mask texture.");
        DragFloat2WithTooltip("Tiling", material.tiling.data(), 0.01f, -100.0f, 100.0f, "%.3f");
        DragFloat2WithTooltip("Offset", material.offset.data(), 0.01f, -100.0f, 100.0f, "%.3f");

        ImGui::SeparatorText("Secondary Maps");
        drawTextureAssetSelector(
            "Detail Albedo x2",
            material.detailAlbedoTexture,
            "DetailAlbedoTexturePickerPopup",
            "Secondary albedo detail texture.");
        drawTextureAssetSelector(
            "Detail Normal Map",
            material.detailNormalTexture,
            "DetailNormalTexturePickerPopup",
            "Secondary normal detail texture.");
        DragFloatWithTooltip("Detail Normal Scale", &material.detailNormalScale, 0.01f, 0.0f, 8.0f, "%.3f");
        DragFloat2WithTooltip("Detail Tiling", material.detailTiling.data(), 0.01f, -100.0f, 100.0f, "%.3f");
        DragFloat2WithTooltip("Detail Offset", material.detailOffset.data(), 0.01f, -100.0f, 100.0f, "%.3f");
        InputIntWithTooltip("UV Set", &material.uvSet);

        if (registry.all_of<MeshRendererComponent>(context.selectedEntity))
        {
            auto& meshRenderer = registry.get<MeshRendererComponent>(context.selectedEntity);
            if (ImGui::CollapsingHeader("Renderer Material Slots", ImGuiTreeNodeFlags_DefaultOpen))
            {
                std::vector<std::string> slotNames = resolveMaterialSlotNames(meshRenderer);
                const std::size_t slotCount = meshRenderer.meshPartIndex != std::numeric_limits<std::uint32_t>::max()
                    ? 1
                    : std::max<std::size_t>({ 1, slotNames.size(), meshRenderer.materialOverrides.size() });
                if (meshRenderer.materialOverrides.size() < slotCount)
                {
                    meshRenderer.materialOverrides.resize(slotCount);
                }

                if (ImGui::BeginTable("RendererMaterialSlotsTable", 2, ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("Slot", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                    ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthStretch);

                    for (std::size_t slotIndex = 0; slotIndex < slotCount; ++slotIndex)
                    {
                        ImGui::TableNextRow();
                        ImGui::PushID(static_cast<int>(slotIndex));

                        ImGui::TableSetColumnIndex(0);
                        const std::string slotLabel = "Element " + std::to_string(slotIndex);
                        ImGui::TextUnformatted(slotLabel.c_str());
                        if (slotIndex < slotNames.size() && !slotNames[slotIndex].empty() &&
                            ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        {
                            ImGui::SetTooltip("%s", slotNames[slotIndex].c_str());
                        }

                        ImGui::TableSetColumnIndex(1);
                        drawMaterialAssetSelector(
                            "##SlotMaterial",
                            meshRenderer.materialOverrides[slotIndex],
                            ("RendererMaterialSlotPickerPopup" + std::to_string(slotIndex)).c_str(),
                            "Click to choose a slot override or drag a material asset here.");

                        ImGui::PopID();
                    }

                    ImGui::EndTable();
                }
            }
        }

        drawMaterialPreview(material);

        if (material.sharedMaterial != previousSharedMaterial &&
            !TrimCopy(material.sharedMaterial).empty())
        {
            MaterialComponent loadedMaterial {};
            if (context.initializeDefaultMaterial)
            {
                context.initializeDefaultMaterial(loadedMaterial);
            }
            loadedMaterial.sharedMaterial = material.sharedMaterial;
            std::string materialLoadError;
            if (context.loadMaterialAsset &&
                context.loadMaterialAsset(resolveAssetPath(material.sharedMaterial), loadedMaterial, materialLoadError))
            {
                loadedMaterial.sharedMaterial = material.sharedMaterial;
                material = std::move(loadedMaterial);
            }
            else if (!materialLoadError.empty())
            {
                logMaterialWarning(materialLoadError);
            }
        }

        if (!MaterialPropertiesEqual(material, materialBeforeEdit) ||
            material.sharedMaterial != previousSharedMaterial)
        {
            markSceneRenderCacheDirty();
        }

        if (ButtonWithTooltip("Remove Material Component"))
        {
            registry.remove<MaterialComponent>(context.selectedEntity);
            ImGui::PopID();
            return;
        }

        ImGui::PopID();
    }
}
