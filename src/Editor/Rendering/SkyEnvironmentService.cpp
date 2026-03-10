#include "Luma/Editor/Rendering/SkyEnvironmentService.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <system_error>

#include <stb_image.h>

namespace
{
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    std::uint8_t LinearToSRGB8(const float linearValue)
    {
        const float clamped = std::clamp(linearValue, 0.0f, 1.0f);
        const float srgb =
            clamped <= 0.0031308f
                ? (12.92f * clamped)
                : (1.055f * std::pow(clamped, 1.0f / 2.4f) - 0.055f);
        return static_cast<std::uint8_t>(std::round(std::clamp(srgb, 0.0f, 1.0f) * 255.0f));
    }

    Vec3 EquirectUVToDir(const float u, const float v)
    {
        constexpr float kPi = 3.14159265359f;
        const float phi = (u - 0.5f) * 2.0f * kPi;
        const float theta = (0.5f - v) * kPi;
        const float cosTheta = std::cos(theta);
        return {
            cosTheta * std::sin(phi),
            std::sin(theta),
            cosTheta * std::cos(phi)
        };
    }

    const char* SkyLightTypeLabel(const Luma::SceneSkyLightType type)
    {
        using Luma::SceneSkyLightType;
        switch (type)
        {
        case SceneSkyLightType::Color:
            return "Color";
        case SceneSkyLightType::HDRI:
            return "HDRI";
        case SceneSkyLightType::Procedural:
            return "Procedural";
        default:
            return "Unknown";
        }
    }
}

namespace Luma::Editor
{
    std::filesystem::path SkyEnvironmentService::ResolveAssetPath(
        const std::string& path,
        const bool projectLoaded,
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& assetsPath) const
    {
        if (path.empty())
        {
            return {};
        }

        std::filesystem::path inputPath(path);
        std::error_code ec;
        if (inputPath.is_absolute())
        {
            const auto absolute = std::filesystem::weakly_canonical(inputPath, ec);
            if (!ec)
            {
                return absolute;
            }
            return inputPath.lexically_normal();
        }

        std::vector<std::filesystem::path> candidates;
        if (projectLoaded)
        {
            candidates.push_back(projectRoot / inputPath);
            candidates.push_back(assetsPath / inputPath);
        }
        candidates.push_back(std::filesystem::current_path() / inputPath);
        candidates.push_back(std::filesystem::current_path() / "assets" / inputPath);
        candidates.push_back(std::filesystem::current_path() / "assets" / "Textures" / inputPath);
        candidates.push_back(std::filesystem::current_path().parent_path() / inputPath);

        for (const auto& candidate : candidates)
        {
            if (std::filesystem::exists(candidate, ec) && !ec)
            {
                const auto canonicalCandidate = std::filesystem::weakly_canonical(candidate, ec);
                if (!ec)
                {
                    return canonicalCandidate;
                }
                return candidate.lexically_normal();
            }
        }

        return inputPath.lexically_normal();
    }

    bool SkyEnvironmentService::BuildPreviewTextureData(
        const std::filesystem::path& imagePath,
        const SkyLightComponent& skyLight,
        const std::function<std::array<float, 3>(const SkyColorEvalContext&)>& evaluateSkyColor,
        SkyPreviewTextureData& outData,
        std::string& outError) const
    {
        outData = {};
        stbi_set_flip_vertically_on_load(0);

        int sourceWidth = 0;
        int sourceHeight = 0;
        int sourceChannels = 0;
        std::vector<float> linearPixels;

        if (stbi_is_hdr(imagePath.string().c_str()) != 0)
        {
            float* hdrPixels = stbi_loadf(
                imagePath.string().c_str(),
                &sourceWidth,
                &sourceHeight,
                &sourceChannels,
                4);
            if (hdrPixels == nullptr)
            {
                outError = "Failed to load HDR skybox: " + imagePath.string();
                return false;
            }

            linearPixels.assign(
                hdrPixels,
                hdrPixels + static_cast<std::size_t>(sourceWidth) * static_cast<std::size_t>(sourceHeight) * 4ULL);
            stbi_image_free(hdrPixels);
        }
        else
        {
            unsigned char* ldrPixels = stbi_load(
                imagePath.string().c_str(),
                &sourceWidth,
                &sourceHeight,
                &sourceChannels,
                4);
            if (ldrPixels == nullptr)
            {
                outError = "Failed to load skybox texture: " + imagePath.string();
                return false;
            }

            const std::size_t pixelCount = static_cast<std::size_t>(sourceWidth) * static_cast<std::size_t>(sourceHeight);
            linearPixels.resize(pixelCount * 4ULL);
            for (std::size_t i = 0; i < pixelCount; ++i)
            {
                const float r = static_cast<float>(ldrPixels[i * 4 + 0]) / 255.0f;
                const float g = static_cast<float>(ldrPixels[i * 4 + 1]) / 255.0f;
                const float b = static_cast<float>(ldrPixels[i * 4 + 2]) / 255.0f;
                linearPixels[i * 4 + 0] = std::pow(r, 2.2f);
                linearPixels[i * 4 + 1] = std::pow(g, 2.2f);
                linearPixels[i * 4 + 2] = std::pow(b, 2.2f);
                linearPixels[i * 4 + 3] = 1.0f;
            }
            stbi_image_free(ldrPixels);
        }

        if (sourceWidth <= 0 || sourceHeight <= 0 || linearPixels.empty())
        {
            outError = "Invalid skybox image data: " + imagePath.string();
            return false;
        }

        constexpr int kMaxPreviewWidth = 1024;
        constexpr int kMaxPreviewHeight = 512;

        int previewWidth = sourceWidth;
        int previewHeight = sourceHeight;
        if (previewWidth > kMaxPreviewWidth || previewHeight > kMaxPreviewHeight)
        {
            const float widthScale = static_cast<float>(kMaxPreviewWidth) / static_cast<float>(previewWidth);
            const float heightScale = static_cast<float>(kMaxPreviewHeight) / static_cast<float>(previewHeight);
            const float scale = std::min(widthScale, heightScale);
            previewWidth = std::max(1, static_cast<int>(std::floor(static_cast<float>(previewWidth) * scale)));
            previewHeight = std::max(1, static_cast<int>(std::floor(static_cast<float>(previewHeight) * scale)));
        }

        std::vector<std::uint8_t> previewPixels(
            static_cast<std::size_t>(previewWidth) * static_cast<std::size_t>(previewHeight) * 4ULL);

        std::array<double, 3> averageLinearAcc { 0.0, 0.0, 0.0 };
        std::uint64_t sampleCount = 0;
        for (int y = 0; y < previewHeight; ++y)
        {
            for (int x = 0; x < previewWidth; ++x)
            {
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(previewHeight);
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(previewWidth);
                const Vec3 direction = EquirectUVToDir(u, v);
                const std::array<float, 3> skyColor =
                    evaluateSkyColor(
                        SkyColorEvalContext {
                            { direction.x, direction.y, direction.z },
                            &skyLight,
                            true,
                            &linearPixels,
                            sourceWidth,
                            sourceHeight });

                averageLinearAcc[0] += static_cast<double>(skyColor[0]);
                averageLinearAcc[1] += static_cast<double>(skyColor[1]);
                averageLinearAcc[2] += static_cast<double>(skyColor[2]);
                ++sampleCount;

                const float mappedR = skyColor[0] / (1.0f + skyColor[0]);
                const float mappedG = skyColor[1] / (1.0f + skyColor[1]);
                const float mappedB = skyColor[2] / (1.0f + skyColor[2]);

                const std::size_t previewIndex =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(previewWidth) + static_cast<std::size_t>(x)) * 4ULL;
                previewPixels[previewIndex + 0] = LinearToSRGB8(mappedR);
                previewPixels[previewIndex + 1] = LinearToSRGB8(mappedG);
                previewPixels[previewIndex + 2] = LinearToSRGB8(mappedB);
                previewPixels[previewIndex + 3] = 255;
            }
        }

        outData.previewPixels = std::move(previewPixels);
        outData.previewWidth = previewWidth;
        outData.previewHeight = previewHeight;
        outData.linearPixels = std::move(linearPixels);
        outData.sourceWidth = sourceWidth;
        outData.sourceHeight = sourceHeight;
        outData.averageColor = sampleCount > 0
            ? std::array<float, 3> {
                static_cast<float>(averageLinearAcc[0] / static_cast<double>(sampleCount)),
                static_cast<float>(averageLinearAcc[1] / static_cast<double>(sampleCount)),
                static_cast<float>(averageLinearAcc[2] / static_cast<double>(sampleCount)) }
            : std::array<float, 3> { 0.0f, 0.0f, 0.0f };
        outData.sourcePath = imagePath;
        outData.successStatus = "Skybox loaded from SkyLight HDRI: " + imagePath.filename().string();
        return true;
    }

    bool SkyEnvironmentService::BuildFallbackPreviewTextureData(
        const SkyLightComponent& skyLight,
        const std::function<std::array<float, 3>(const SkyColorEvalContext&)>& evaluateSkyColor,
        SkyPreviewTextureData& outData,
        std::string& outError) const
    {
        (void)outError;
        outData = {};

        constexpr int previewWidth = 1024;
        constexpr int previewHeight = 512;
        std::vector<std::uint8_t> previewPixels(
            static_cast<std::size_t>(previewWidth) * static_cast<std::size_t>(previewHeight) * 4ULL);

        std::array<double, 3> averageLinearAcc { 0.0, 0.0, 0.0 };
        std::uint64_t sampleCount = 0;
        for (int y = 0; y < previewHeight; ++y)
        {
            const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(previewHeight);
            for (int x = 0; x < previewWidth; ++x)
            {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(previewWidth);
                const Vec3 direction = EquirectUVToDir(u, v);
                const std::array<float, 3> skyColor =
                    evaluateSkyColor(
                        SkyColorEvalContext {
                            { direction.x, direction.y, direction.z },
                            &skyLight,
                            false,
                            nullptr,
                            0,
                            0 });

                averageLinearAcc[0] += static_cast<double>(skyColor[0]);
                averageLinearAcc[1] += static_cast<double>(skyColor[1]);
                averageLinearAcc[2] += static_cast<double>(skyColor[2]);
                ++sampleCount;

                const float mappedR = skyColor[0] / (1.0f + skyColor[0]);
                const float mappedG = skyColor[1] / (1.0f + skyColor[1]);
                const float mappedB = skyColor[2] / (1.0f + skyColor[2]);

                const std::size_t previewIndex =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(previewWidth) + static_cast<std::size_t>(x)) * 4ULL;
                previewPixels[previewIndex + 0] = LinearToSRGB8(mappedR);
                previewPixels[previewIndex + 1] = LinearToSRGB8(mappedG);
                previewPixels[previewIndex + 2] = LinearToSRGB8(mappedB);
                previewPixels[previewIndex + 3] = 255;
            }
        }

        outData.previewPixels = std::move(previewPixels);
        outData.previewWidth = previewWidth;
        outData.previewHeight = previewHeight;
        outData.averageColor = sampleCount > 0
            ? std::array<float, 3> {
                static_cast<float>(averageLinearAcc[0] / static_cast<double>(sampleCount)),
                static_cast<float>(averageLinearAcc[1] / static_cast<double>(sampleCount)),
                static_cast<float>(averageLinearAcc[2] / static_cast<double>(sampleCount)) }
            : std::array<float, 3> { 0.0f, 0.0f, 0.0f };
        return true;
    }

    bool SkyEnvironmentService::BuildIBLHookData(
        const SkyLightComponent& skyLight,
        const std::filesystem::path& imagePath,
        const std::array<float, 3>& averageColor,
        const bool projectLoaded,
        const std::filesystem::path& cachePath,
        SkyIBLHookData& outData) const
    {
        outData = {};

        std::filesystem::path cacheRoot =
            projectLoaded ? (cachePath / "SkyIBL") : (std::filesystem::current_path() / "Cache" / "SkyIBL");
        std::error_code ec;
        std::filesystem::create_directories(cacheRoot, ec);

        std::string baseName = imagePath.empty() ? std::string {} : imagePath.stem().string();
        if (baseName.empty())
        {
            baseName = skyLight.skyLightType == SceneSkyLightType::Color ? "sky_color" : "sky_procedural";
        }
        for (char& character : baseName)
        {
            if (!std::isalnum(static_cast<unsigned char>(character)) && character != '_' && character != '-')
            {
                character = '_';
            }
        }

        outData.irradianceMap = (cacheRoot / (baseName + "_irradiance.ktx2")).string();
        outData.prefilteredReflectionMap = (cacheRoot / (baseName + "_prefiltered.ktx2")).string();
        outData.brdfLut = (cacheRoot / (baseName + "_brdf_lut.ktx2")).string();
        outData.metadataPath = cacheRoot / (baseName + "_ibl_hook.txt");
        outData.successStatus = "IBL hook generated from SkyLight: " + outData.metadataPath.filename().string();

        std::ofstream metadata(outData.metadataPath, std::ios::trunc);
        if (metadata.is_open())
        {
            metadata << "Source=" << (imagePath.empty() ? "<procedural>" : imagePath.string()) << '\n';
            metadata << "Type=" << SkyLightTypeLabel(skyLight.skyLightType) << '\n';
            metadata << "AverageLinearColor=" << averageColor[0] << "," << averageColor[1] << "," << averageColor[2] << '\n';
            metadata << "IrradianceMap=" << outData.irradianceMap << '\n';
            metadata << "PrefilteredReflectionMap=" << outData.prefilteredReflectionMap << '\n';
            metadata << "BRDFLUT=" << outData.brdfLut << '\n';
            metadata << "Status=HookGenerated\n";
        }

        return true;
    }
}
