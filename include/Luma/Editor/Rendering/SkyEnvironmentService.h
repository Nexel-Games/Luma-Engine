#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Luma/Scene/SkyLightComponent.h"

namespace Luma::Editor
{
    struct SkyPreviewTextureData
    {
        std::vector<std::uint8_t> previewPixels;
        int previewWidth = 0;
        int previewHeight = 0;
        std::vector<float> linearPixels;
        int sourceWidth = 0;
        int sourceHeight = 0;
        std::array<float, 3> averageColor { 0.0f, 0.0f, 0.0f };
        std::filesystem::path sourcePath;
        std::string successStatus;
    };

    struct SkyIBLHookData
    {
        std::string irradianceMap;
        std::string prefilteredReflectionMap;
        std::string brdfLut;
        std::filesystem::path metadataPath;
        std::string successStatus;
    };

    struct SkyColorEvalContext
    {
        std::array<float, 3> direction { 0.0f, 0.0f, 0.0f };
        const SkyLightComponent* skyLight = nullptr;
        bool hasEnvironment = false;
        const std::vector<float>* linearPixels = nullptr;
        int sourceWidth = 0;
        int sourceHeight = 0;
    };

    class SkyEnvironmentService
    {
    public:
        std::filesystem::path ResolveAssetPath(
            const std::string& path,
            bool projectLoaded,
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& assetsPath) const;

        bool BuildPreviewTextureData(
            const std::filesystem::path& imagePath,
            const SkyLightComponent& skyLight,
            const std::function<std::array<float, 3>(const SkyColorEvalContext&)>& evaluateSkyColor,
            SkyPreviewTextureData& outData,
            std::string& outError) const;

        bool BuildFallbackPreviewTextureData(
            const SkyLightComponent& skyLight,
            const std::function<std::array<float, 3>(const SkyColorEvalContext&)>& evaluateSkyColor,
            SkyPreviewTextureData& outData,
            std::string& outError) const;

        bool BuildIBLHookData(
            const SkyLightComponent& skyLight,
            const std::filesystem::path& imagePath,
            const std::array<float, 3>& averageColor,
            bool projectLoaded,
            const std::filesystem::path& cachePath,
            SkyIBLHookData& outData) const;
    };
}
