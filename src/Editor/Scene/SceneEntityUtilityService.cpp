#include "Luma/Editor/Scene/SceneEntityUtilityService.h"

#include <unordered_set>

#include "Luma/Scene/TagComponent.h"

namespace Luma::Editor
{
    std::string SceneEntityUtilityService::GenerateUniqueEntityName(
        const Scene& scene,
        const std::string_view baseName) const
    {
        const auto& registry = scene.GetRegistry();
        const auto view = registry.view<TagComponent>();
        std::unordered_set<std::string> existingNames;
        existingNames.reserve(128);

        for (const EntityID entity : view)
        {
            existingNames.insert(view.get<TagComponent>(entity).name);
        }

        const std::string base(baseName);
        if (!existingNames.contains(base))
        {
            return base;
        }

        int index = 1;
        while (index < 10000)
        {
            const std::string candidate = base + " " + std::to_string(index);
            if (!existingNames.contains(candidate))
            {
                return candidate;
            }
            ++index;
        }

        return base + " Copy";
    }

    void SceneEntityUtilityService::SelectSingleEntity(SceneEntitySelectionContext& context, const EntityID entity) const
    {
        if (context.scene == nullptr || context.selectionState == nullptr)
        {
            return;
        }

        if (context.selectedContentEntry != nullptr)
        {
            context.selectedContentEntry->clear();
        }

        const auto& registry = context.scene->GetRegistry();
        if (entity != entt::null && !registry.valid(entity))
        {
            return;
        }

        context.selectionState->SelectSingle(entity);
    }

    void SceneEntityUtilityService::ToggleEntitySelection(SceneEntitySelectionContext& context, const EntityID entity) const
    {
        if (context.scene == nullptr || context.selectionState == nullptr)
        {
            return;
        }

        const auto& registry = context.scene->GetRegistry();
        if (entity != entt::null && !registry.valid(entity))
        {
            return;
        }

        if (context.selectedContentEntry != nullptr)
        {
            context.selectedContentEntry->clear();
        }

        context.selectionState->Toggle(entity);
    }

    bool SceneEntityUtilityService::IsEntitySelected(const EditorSelectionState& selectionState, const EntityID entity) const
    {
        return selectionState.IsSelected(entity);
    }

    void SceneEntityUtilityService::ClearEntitySelection(EditorSelectionState& selectionState) const
    {
        selectionState.Clear();
    }

    void SceneEntityUtilityService::PruneEntitySelection(Scene& scene, EditorSelectionState& selectionState) const
    {
        selectionState.Prune(scene.GetRegistry());
    }

    void SceneEntityUtilityService::InitializeSkyLightDefaults(SkyLightComponent& skyLight) const
    {
        skyLight = SkyLightComponent {};
        skyLight.skyLightType = SceneSkyLightType::Procedural;
        skyLight.skyColor = { 0.38f, 0.56f, 0.95f };
        skyLight.color = { 1.0f, 1.0f, 1.0f };
        skyLight.intensity = 1.0f;
        skyLight.colorIntensity = 1.0f;
        skyLight.rotation = 0.0f;
        skyLight.exposureEV = 0.0f;
        skyLight.skyboxExposureEV = 0.0f;
        skyLight.sunIntensityMultiplier = 1.0f;
        skyLight.sunSpecularMultiplier = 1.0f;
        skyLight.autoExposureEnabled = true;
        skyLight.autoExposureMinEV = -6.0f;
        skyLight.autoExposureMaxEV = 5.0f;
        skyLight.autoExposureSpeedUp = 3.0f;
        skyLight.autoExposureSpeedDown = 1.5f;
        skyLight.rebuildIBLRequested = true;
    }

    void SceneEntityUtilityService::InitializePostProcessDefaults(PostProcessComponent& postProcess) const
    {
        postProcess = PostProcessComponent {};
        postProcess.active = true;
        postProcess.priority = 0;
        postProcess.unbound = true;
        postProcess.volumeExtents = { 5.0f, 5.0f, 5.0f };
        postProcess.blendDistance = 2.0f;
        postProcess.toneMappingEnabled = true;
        postProcess.toneMappingOperator = ToneMappingOperator::ACES;
        postProcess.exposureCompensationEV = 0.0f;
        postProcess.eyeAdaptationCompensationEV = 0.0f;
        postProcess.whitePoint = 1.0f;
        postProcess.colorFilter = { 1.0f, 1.0f, 1.0f };
        postProcess.colorBalance = { 1.0f, 1.0f, 1.0f };
        postProcess.saturation = 1.0f;
        postProcess.contrast = 1.0f;
        postProcess.gamma = 1.0f;
        postProcess.filmCurveShoulder = 1.0f;
        postProcess.filmCurveLinear = 1.0f;
        postProcess.filmCurveToe = 1.0f;
        postProcess.bloomEnabled = false;
        postProcess.bloomIntensity = 0.15f;
        postProcess.bloomThreshold = 1.0f;
        postProcess.bloomKnee = 0.5f;
    }
}
