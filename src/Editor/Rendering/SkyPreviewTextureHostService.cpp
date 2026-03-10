#include "Luma/Editor/Rendering/SkyPreviewTextureHostService.h"

namespace Luma::Editor
{
    namespace
    {
        std::string BuildSkyPreviewSignature(const std::filesystem::path& sourcePath, const SkyLightComponent& skyLight)
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

    void SkyPreviewTextureHostService::Sync(SkyPreviewTextureHostContext& context) const
    {
        if (context.renderer == nullptr || context.scene == nullptr || context.skyEnvironmentService == nullptr ||
            context.skyStatus == nullptr || context.skyboxPreviewTexture == nullptr || context.skyboxSourcePath == nullptr ||
            context.skyboxSignature == nullptr)
        {
            return;
        }

        const EntityID skyEntity = context.findPrimarySkyEntity ? context.findPrimarySkyEntity() : entt::null;
        if (skyEntity == entt::null)
        {
            Release(context);
            context.skyStatus->clear();
            return;
        }

        auto& registry = context.scene->GetRegistry();
        if (!registry.valid(skyEntity) || !registry.all_of<SkyLightComponent>(skyEntity))
        {
            return;
        }

        auto& skyLight = registry.get<SkyLightComponent>(skyEntity);
        const bool usesHDRI =
            skyLight.skyLightType == SceneSkyLightType::HDRI &&
            !skyLight.environmentMap.empty();

        const std::filesystem::path fallbackPath("<skylight-fallback>");
        const std::string fallbackSignature = BuildSkyPreviewSignature(fallbackPath, skyLight);
        const bool fallbackAlreadyCurrent =
            *context.skyboxPreviewTexture != nullptr &&
            *context.skyboxSignature == fallbackSignature;

        if (!skyLight.previewRefreshRequested &&
            !skyLight.rebuildIBLRequested &&
            *context.skyboxPreviewTexture != nullptr)
        {
            return;
        }

        if (usesHDRI)
        {
            const std::filesystem::path imagePath = context.skyEnvironmentService->ResolveAssetPath(
                skyLight.environmentMap,
                context.projectLoaded,
                context.projectRoot,
                context.assetsPath);
            const std::string hdriSignature = BuildSkyPreviewSignature(imagePath, skyLight);
            const bool hdriAlreadyCurrent =
                *context.skyboxPreviewTexture != nullptr &&
                *context.skyboxSignature == hdriSignature;

            if (!skyLight.rebuildIBLRequested && hdriAlreadyCurrent)
            {
                skyLight.previewRefreshRequested = false;
                return;
            }

            std::error_code imageExistsEc;
            const bool hdriExists =
                !imagePath.empty() &&
                std::filesystem::exists(imagePath, imageExistsEc) &&
                !imageExistsEc;

            if (!skyLight.rebuildIBLRequested && fallbackAlreadyCurrent && !hdriExists)
            {
                *context.skyStatus = "Sky HDRI missing, using SkyLight fallback.";
                skyLight.previewRefreshRequested = false;
                return;
            }

            if (hdriExists)
            {
                if (!hdriAlreadyCurrent)
                {
                    if (!RebuildPreviewTexture(context, imagePath, skyLight))
                    {
                        return;
                    }
                    *context.skyboxSignature = hdriSignature;
                }

                if (skyLight.rebuildIBLRequested || !hdriAlreadyCurrent)
                {
                    RunIBLHooks(context, skyEntity, imagePath);
                }
                skyLight.previewRefreshRequested = false;
                return;
            }
        }

        if (!fallbackAlreadyCurrent || skyLight.rebuildIBLRequested)
        {
            if (!RebuildFallbackPreviewTexture(context, skyLight))
            {
                return;
            }
            *context.skyboxSignature = fallbackSignature;
        }

        if (skyLight.rebuildIBLRequested)
        {
            RunIBLHooks(context, skyEntity, {});
        }

        skyLight.previewRefreshRequested = false;

        if (usesHDRI)
        {
            *context.skyStatus = "Sky HDRI missing, using SkyLight fallback.";
        }
        else if (skyLight.skyLightType == SceneSkyLightType::Color)
        {
            *context.skyStatus = "Sky from SkyLight color.";
        }
        else
        {
            *context.skyStatus = "Sky from SkyLight procedural.";
        }
    }

    void SkyPreviewTextureHostService::Release(SkyPreviewTextureHostContext& context) const
    {
        if (context.skyboxPreviewTexture != nullptr &&
            *context.skyboxPreviewTexture != nullptr &&
            context.renderer != nullptr)
        {
            context.renderer->DestroyImGuiTexture(*context.skyboxPreviewTexture);
        }

        if (context.skyboxPreviewTexture != nullptr)
        {
            *context.skyboxPreviewTexture = nullptr;
        }
        if (context.skyboxPreviewWidth != nullptr)
        {
            *context.skyboxPreviewWidth = 0;
        }
        if (context.skyboxPreviewHeight != nullptr)
        {
            *context.skyboxPreviewHeight = 0;
        }
        if (context.skyboxSourcePath != nullptr)
        {
            context.skyboxSourcePath->clear();
        }
        if (context.skyboxSignature != nullptr)
        {
            context.skyboxSignature->clear();
        }
        if (context.skyEnvironmentLinearPixels != nullptr)
        {
            context.skyEnvironmentLinearPixels->clear();
        }
        if (context.skyEnvironmentWidth != nullptr)
        {
            *context.skyEnvironmentWidth = 0;
        }
        if (context.skyEnvironmentHeight != nullptr)
        {
            *context.skyEnvironmentHeight = 0;
        }
    }

    bool SkyPreviewTextureHostService::RebuildPreviewTexture(
        SkyPreviewTextureHostContext& context,
        const std::filesystem::path& imagePath,
        const SkyLightComponent& skyLight) const
    {
        if (context.renderer == nullptr || context.skyEnvironmentService == nullptr || context.skyStatus == nullptr)
        {
            return false;
        }

        SkyPreviewTextureData previewData;
        std::string error;
        if (!context.skyEnvironmentService->BuildPreviewTextureData(
                imagePath,
                skyLight,
                context.evaluateSkyColor,
                previewData,
                error))
        {
            *context.skyStatus = error;
            return false;
        }

        void* newTexture = context.renderer->CreateImGuiTextureRGBA8(
            static_cast<std::uint32_t>(previewData.previewWidth),
            static_cast<std::uint32_t>(previewData.previewHeight),
            previewData.previewPixels.data());
        if (newTexture == nullptr)
        {
            *context.skyStatus = "Failed to create GPU skybox texture.";
            return false;
        }

        Release(context);
        *context.skyboxPreviewTexture = newTexture;
        if (context.skyboxPreviewWidth != nullptr)
        {
            *context.skyboxPreviewWidth = previewData.previewWidth;
        }
        if (context.skyboxPreviewHeight != nullptr)
        {
            *context.skyboxPreviewHeight = previewData.previewHeight;
        }
        if (context.skyboxSourcePath != nullptr)
        {
            *context.skyboxSourcePath = previewData.sourcePath;
        }
        if (context.skyAverageColor != nullptr)
        {
            *context.skyAverageColor = previewData.averageColor;
        }
        if (context.skyEnvironmentLinearPixels != nullptr)
        {
            *context.skyEnvironmentLinearPixels = std::move(previewData.linearPixels);
        }
        if (context.skyEnvironmentWidth != nullptr)
        {
            *context.skyEnvironmentWidth = previewData.sourceWidth;
        }
        if (context.skyEnvironmentHeight != nullptr)
        {
            *context.skyEnvironmentHeight = previewData.sourceHeight;
        }
        *context.skyStatus = previewData.successStatus;
        return true;
    }

    bool SkyPreviewTextureHostService::RebuildFallbackPreviewTexture(
        SkyPreviewTextureHostContext& context,
        const SkyLightComponent& skyLight) const
    {
        if (context.renderer == nullptr || context.skyEnvironmentService == nullptr || context.skyStatus == nullptr)
        {
            return false;
        }

        SkyPreviewTextureData previewData;
        std::string error;
        if (!context.skyEnvironmentService->BuildFallbackPreviewTextureData(
                skyLight,
                context.evaluateSkyColor,
                previewData,
                error))
        {
            *context.skyStatus = error.empty() ? "Failed to build fallback skybox texture." : error;
            return false;
        }

        void* newTexture = context.renderer->CreateImGuiTextureRGBA8(
            static_cast<std::uint32_t>(previewData.previewWidth),
            static_cast<std::uint32_t>(previewData.previewHeight),
            previewData.previewPixels.data());
        if (newTexture == nullptr)
        {
            *context.skyStatus = "Failed to create fallback skybox texture.";
            return false;
        }

        Release(context);
        *context.skyboxPreviewTexture = newTexture;
        if (context.skyboxPreviewWidth != nullptr)
        {
            *context.skyboxPreviewWidth = previewData.previewWidth;
        }
        if (context.skyboxPreviewHeight != nullptr)
        {
            *context.skyboxPreviewHeight = previewData.previewHeight;
        }
        if (context.skyAverageColor != nullptr)
        {
            *context.skyAverageColor = previewData.averageColor;
        }
        *context.skyStatus = previewData.successStatus.empty() ? "Sky preview updated from SkyLight fallback." : previewData.successStatus;
        return true;
    }

    void SkyPreviewTextureHostService::RunIBLHooks(
        SkyPreviewTextureHostContext& context,
        const EntityID skyEntity,
        const std::filesystem::path& imagePath) const
    {
        if (context.scene == nullptr || context.skyEnvironmentService == nullptr || context.skyStatus == nullptr ||
            context.skyAverageColor == nullptr)
        {
            return;
        }

        auto& registry = context.scene->GetRegistry();
        if (!registry.valid(skyEntity) || !registry.all_of<SkyLightComponent>(skyEntity))
        {
            return;
        }

        auto& skyLight = registry.get<SkyLightComponent>(skyEntity);
        if (!imagePath.empty())
        {
            skyLight.environmentMap = imagePath.string();
        }

        SkyIBLHookData hookData;
        context.skyEnvironmentService->BuildIBLHookData(
            skyLight,
            imagePath,
            *context.skyAverageColor,
            context.projectLoaded,
            context.cachePath,
            hookData);
        skyLight.irradianceMap = hookData.irradianceMap;
        skyLight.prefilteredReflectionMap = hookData.prefilteredReflectionMap;
        skyLight.brdfLut = hookData.brdfLut;
        skyLight.rebuildIBLRequested = false;
        *context.skyStatus = hookData.successStatus;
    }
}
