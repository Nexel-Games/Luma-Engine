#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Luma/Editor/Rendering/SkyEnvironmentService.h"
#include "Luma/RHI/IRenderBackend.h"
#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct SkyPreviewTextureHostContext
    {
        IRenderBackend* renderer = nullptr;
        Scene* scene = nullptr;
        SkyEnvironmentService* skyEnvironmentService = nullptr;
        bool projectLoaded = false;
        std::filesystem::path projectRoot;
        std::filesystem::path assetsPath;
        std::filesystem::path cachePath;
        std::string* skyStatus = nullptr;
        void** skyboxPreviewTexture = nullptr;
        int* skyboxPreviewWidth = nullptr;
        int* skyboxPreviewHeight = nullptr;
        std::filesystem::path* skyboxSourcePath = nullptr;
        std::string* skyboxSignature = nullptr;
        std::array<float, 3>* skyAverageColor = nullptr;
        std::vector<float>* skyEnvironmentLinearPixels = nullptr;
        int* skyEnvironmentWidth = nullptr;
        int* skyEnvironmentHeight = nullptr;
        std::function<EntityID()> findPrimarySkyEntity;
        std::function<std::filesystem::path(const std::string&)> resolveAssetPath;
        std::function<std::array<float, 3>(const SkyColorEvalContext&)> evaluateSkyColor;
    };

    class SkyPreviewTextureHostService
    {
    public:
        void Sync(SkyPreviewTextureHostContext& context) const;
        void Release(SkyPreviewTextureHostContext& context) const;

    private:
        bool RebuildPreviewTexture(
            SkyPreviewTextureHostContext& context,
            const std::filesystem::path& imagePath,
            const SkyLightComponent& skyLight) const;
        bool RebuildFallbackPreviewTexture(
            SkyPreviewTextureHostContext& context,
            const SkyLightComponent& skyLight) const;
        void RunIBLHooks(
            SkyPreviewTextureHostContext& context,
            EntityID skyEntity,
            const std::filesystem::path& imagePath) const;
    };
}
