#include "Luma/Editor/Rendering/PostProcessBlendService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <vector>

#include "Luma/Scene/PostProcessComponent.h"
#include "Luma/Scene/TransformComponent.h"

namespace Luma::Editor
{
    void PostProcessBlendService::BuildBlendedView(
        const Scene& scene,
        const std::array<float, 3>& cameraWorldPosition,
        ScenePostProcessView& outPostProcess) const
    {
        const auto& registry = scene.GetRegistry();
        const auto view = registry.view<TransformComponent, PostProcessComponent>();
        struct WeightedPostProcess
        {
            const PostProcessComponent* component = nullptr;
            float weight = 0.0f;
        };

        std::unordered_map<int, std::vector<WeightedPostProcess>> candidatesByPriority;

        auto computeVolumeWeight = [&](const TransformComponent& transform, const PostProcessComponent& postProcess) -> float
        {
            if (postProcess.unbound)
            {
                return 1.0f;
            }

            const std::array<float, 3> scaledExtents {
                std::max(0.05f, postProcess.volumeExtents[0] * std::max(std::abs(transform.worldScale[0]), 0.001f)),
                std::max(0.05f, postProcess.volumeExtents[1] * std::max(std::abs(transform.worldScale[1]), 0.001f)),
                std::max(0.05f, postProcess.volumeExtents[2] * std::max(std::abs(transform.worldScale[2]), 0.001f))
            };

            const float blendDistance = std::max(postProcess.blendDistance, 0.0f);
            float weight = 1.0f;
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                const float distanceFromCenter =
                    std::abs(cameraWorldPosition[axis] - transform.worldPosition[axis]);
                const float inner = scaledExtents[axis];
                if (distanceFromCenter <= inner)
                {
                    continue;
                }

                if (blendDistance <= 1.0e-4f)
                {
                    return 0.0f;
                }

                const float outer = inner + blendDistance;
                if (distanceFromCenter >= outer)
                {
                    return 0.0f;
                }

                const float axisWeight = 1.0f - ((distanceFromCenter - inner) / blendDistance);
                weight = std::min(weight, std::clamp(axisWeight, 0.0f, 1.0f));
            }

            return std::clamp(weight, 0.0f, 1.0f);
        };

        for (const EntityID entity : view)
        {
            const auto& transform = view.get<TransformComponent>(entity);
            const auto& postProcess = view.get<PostProcessComponent>(entity);
            if (!postProcess.active)
            {
                continue;
            }

            const float weight = computeVolumeWeight(transform, postProcess);
            if (weight > 1.0e-4f)
            {
                candidatesByPriority[postProcess.priority].push_back(WeightedPostProcess { &postProcess, weight });
            }
        }

        if (candidatesByPriority.empty())
        {
            return;
        }

        auto blendArray3 = [](const std::array<float, 3>& lhs, const std::array<float, 3>& rhs, const float alpha)
        {
            return std::array<float, 3> {
                std::lerp(lhs[0], rhs[0], alpha),
                std::lerp(lhs[1], rhs[1], alpha),
                std::lerp(lhs[2], rhs[2], alpha)
            };
        };

        auto blendGroup = [&](const std::vector<WeightedPostProcess>& group) -> std::pair<ScenePostProcessView, float>
        {
            ScenePostProcessView blended;
            blended.active = true;
            if (group.empty())
            {
                return { blended, 0.0f };
            }

            const WeightedPostProcess* dominant = &group.front();
            float totalWeight = 0.0f;
            for (const WeightedPostProcess& candidate : group)
            {
                totalWeight += candidate.weight;
                if (candidate.weight > dominant->weight)
                {
                    dominant = &candidate;
                }
            }

            if (totalWeight <= 1.0e-4f)
            {
                return { blended, 0.0f };
            }

            float exposureCompensationEV = 0.0f;
            float eyeAdaptationCompensationEV = 0.0f;
            float whitePoint = 0.0f;
            std::array<float, 3> colorFilter { 0.0f, 0.0f, 0.0f };
            std::array<float, 3> colorBalance { 0.0f, 0.0f, 0.0f };
            float saturation = 0.0f;
            float contrast = 0.0f;
            float gamma = 0.0f;
            float filmCurveShoulder = 0.0f;
            float filmCurveLinear = 0.0f;
            float filmCurveToe = 0.0f;
            float bloomIntensity = 0.0f;
            float bloomThreshold = 0.0f;
            float bloomKnee = 0.0f;

            for (const WeightedPostProcess& candidate : group)
            {
                const float normalizedWeight = candidate.weight / totalWeight;
                const PostProcessComponent& postProcess = *candidate.component;
                exposureCompensationEV += postProcess.exposureCompensationEV * normalizedWeight;
                eyeAdaptationCompensationEV += postProcess.eyeAdaptationCompensationEV * normalizedWeight;
                whitePoint += postProcess.whitePoint * normalizedWeight;
                colorFilter[0] += postProcess.colorFilter[0] * normalizedWeight;
                colorFilter[1] += postProcess.colorFilter[1] * normalizedWeight;
                colorFilter[2] += postProcess.colorFilter[2] * normalizedWeight;
                colorBalance[0] += postProcess.colorBalance[0] * normalizedWeight;
                colorBalance[1] += postProcess.colorBalance[1] * normalizedWeight;
                colorBalance[2] += postProcess.colorBalance[2] * normalizedWeight;
                saturation += postProcess.saturation * normalizedWeight;
                contrast += postProcess.contrast * normalizedWeight;
                gamma += postProcess.gamma * normalizedWeight;
                filmCurveShoulder += postProcess.filmCurveShoulder * normalizedWeight;
                filmCurveLinear += postProcess.filmCurveLinear * normalizedWeight;
                filmCurveToe += postProcess.filmCurveToe * normalizedWeight;
                bloomIntensity += postProcess.bloomIntensity * normalizedWeight;
                bloomThreshold += postProcess.bloomThreshold * normalizedWeight;
                bloomKnee += postProcess.bloomKnee * normalizedWeight;
            }

            blended.toneMappingEnabled = dominant->component->toneMappingEnabled;
            blended.toneMappingOperator = dominant->component->toneMappingOperator;
            blended.exposureCompensationEV = exposureCompensationEV;
            blended.eyeAdaptationCompensationEV = eyeAdaptationCompensationEV;
            blended.whitePoint = whitePoint;
            blended.colorFilter = colorFilter;
            blended.colorBalance = colorBalance;
            blended.saturation = saturation;
            blended.contrast = contrast;
            blended.gamma = gamma;
            blended.filmCurveShoulder = filmCurveShoulder;
            blended.filmCurveLinear = filmCurveLinear;
            blended.filmCurveToe = filmCurveToe;
            blended.bloomEnabled = dominant->component->bloomEnabled;
            blended.bloomIntensity = bloomIntensity;
            blended.bloomThreshold = bloomThreshold;
            blended.bloomKnee = bloomKnee;
            return { blended, std::clamp(totalWeight, 0.0f, 1.0f) };
        };

        std::vector<int> priorities;
        priorities.reserve(candidatesByPriority.size());
        for (const auto& [priority, _] : candidatesByPriority)
        {
            priorities.push_back(priority);
        }
        std::sort(priorities.begin(), priorities.end());

        bool initialized = false;
        for (const int priority : priorities)
        {
            const auto groupIt = candidatesByPriority.find(priority);
            if (groupIt == candidatesByPriority.end())
            {
                continue;
            }

            const auto [groupView, groupWeight] = blendGroup(groupIt->second);
            if (groupWeight <= 1.0e-4f)
            {
                continue;
            }

            if (!initialized)
            {
                outPostProcess = groupView;
                initialized = true;
                continue;
            }

            outPostProcess.exposureCompensationEV =
                std::lerp(outPostProcess.exposureCompensationEV, groupView.exposureCompensationEV, groupWeight);
            outPostProcess.eyeAdaptationCompensationEV =
                std::lerp(outPostProcess.eyeAdaptationCompensationEV, groupView.eyeAdaptationCompensationEV, groupWeight);
            outPostProcess.whitePoint =
                std::lerp(outPostProcess.whitePoint, groupView.whitePoint, groupWeight);
            outPostProcess.colorFilter =
                blendArray3(outPostProcess.colorFilter, groupView.colorFilter, groupWeight);
            outPostProcess.colorBalance =
                blendArray3(outPostProcess.colorBalance, groupView.colorBalance, groupWeight);
            outPostProcess.saturation =
                std::lerp(outPostProcess.saturation, groupView.saturation, groupWeight);
            outPostProcess.contrast =
                std::lerp(outPostProcess.contrast, groupView.contrast, groupWeight);
            outPostProcess.gamma =
                std::lerp(outPostProcess.gamma, groupView.gamma, groupWeight);
            outPostProcess.filmCurveShoulder =
                std::lerp(outPostProcess.filmCurveShoulder, groupView.filmCurveShoulder, groupWeight);
            outPostProcess.filmCurveLinear =
                std::lerp(outPostProcess.filmCurveLinear, groupView.filmCurveLinear, groupWeight);
            outPostProcess.filmCurveToe =
                std::lerp(outPostProcess.filmCurveToe, groupView.filmCurveToe, groupWeight);
            outPostProcess.bloomIntensity =
                std::lerp(outPostProcess.bloomIntensity, groupView.bloomIntensity, groupWeight);
            outPostProcess.bloomThreshold =
                std::lerp(outPostProcess.bloomThreshold, groupView.bloomThreshold, groupWeight);
            outPostProcess.bloomKnee =
                std::lerp(outPostProcess.bloomKnee, groupView.bloomKnee, groupWeight);
            outPostProcess.toneMappingEnabled = groupView.toneMappingEnabled;
            outPostProcess.toneMappingOperator = groupView.toneMappingOperator;
            outPostProcess.bloomEnabled = groupView.bloomEnabled;
            outPostProcess.active = true;
        }
    }
}
