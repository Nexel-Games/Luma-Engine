#include "Luma/Editor/Rendering/SceneRenderCacheStateService.h"

#include "Luma/Scene/SkyLightComponent.h"

namespace Luma::Editor
{
    namespace
    {
        std::string BuildSkySignatureForCache(const std::filesystem::path& sourcePath, const SkyLightComponent& skyLight)
        {
            std::string signature = sourcePath.generic_string();
            signature.push_back('|');
            signature += std::to_string(static_cast<int>(skyLight.skyLightType));
            signature.push_back('|');
            signature += skyLight.environmentMap;
            signature.push_back('|');
            signature += skyLight.active ? "1" : "0";
            signature.push_back('|');
            signature += std::to_string(skyLight.skyColor[0]);
            signature.push_back('|');
            signature += std::to_string(skyLight.skyColor[1]);
            signature.push_back('|');
            signature += std::to_string(skyLight.skyColor[2]);
            signature.push_back('|');
            signature += std::to_string(skyLight.intensity);
            signature.push_back('|');
            signature += std::to_string(skyLight.colorIntensity);
            signature.push_back('|');
            signature += std::to_string(skyLight.diffuseIntensity);
            signature.push_back('|');
            signature += std::to_string(skyLight.reflectionIntensity);
            signature.push_back('|');
            signature += std::to_string(skyLight.rotation);
            signature.push_back('|');
            signature += std::to_string(skyLight.blendFactor);
            signature.push_back('|');
            signature += std::to_string(skyLight.lowerHemisphereIsBlack ? 1 : 0);
            signature.push_back('|');
            signature += std::to_string(skyLight.lowerHemisphereColor[0]);
            signature.push_back('|');
            signature += std::to_string(skyLight.lowerHemisphereColor[1]);
            signature.push_back('|');
            signature += std::to_string(skyLight.lowerHemisphereColor[2]);
            return signature;
        }
    }

    std::string SceneRenderCacheStateService::BuildActiveSkyMeshSignature(const SceneRenderCacheStateContext& context) const
    {
        if (context.scene == nullptr || context.skyboxSourcePath == nullptr || !context.findPrimarySkyEntity)
        {
            return {};
        }

        const EntityID currentSkyEntity = context.findPrimarySkyEntity();
        const auto& registry = context.scene->GetRegistry();
        if (currentSkyEntity == entt::null ||
            !registry.valid(currentSkyEntity) ||
            !registry.all_of<SkyLightComponent>(currentSkyEntity))
        {
            return {};
        }

        const auto& skyLight = registry.get<SkyLightComponent>(currentSkyEntity);
        if (!skyLight.active)
        {
            return {};
        }

        return BuildSkySignatureForCache(*context.skyboxSourcePath, skyLight);
    }

    void SceneRenderCacheStateService::FinalizeBuild(SceneRenderCacheStateContext& context) const
    {
        if (context.renderSceneCacheDirtyFlags != nullptr)
        {
            *context.renderSceneCacheDirtyFlags = SceneRenderCacheDirtyFlags::None;
        }
        if (context.lastViewportGridEnabled != nullptr)
        {
            *context.lastViewportGridEnabled = context.viewportGridEnabled;
        }
        if (context.lastSkyMeshSignature != nullptr)
        {
            *context.lastSkyMeshSignature = BuildActiveSkyMeshSignature(context);
        }
    }
}
