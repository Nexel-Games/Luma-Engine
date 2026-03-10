#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace Luma
{
    enum class MaterialRenderingMode : std::uint8_t
    {
        Opaque = 0,
        Cutout,
        Fade,
        Transparent
    };

    enum class MaterialSmoothnessSource : std::uint8_t
    {
        MetallicAlpha = 0,
        AlbedoAlpha
    };

    enum class MaterialGlobalIlluminationMode : std::uint8_t
    {
        Realtime = 0,
        Baked,
        None
    };

    struct MaterialComponent
    {
        std::string name = "New Material";
        std::string shader = "Standard";
        std::string sharedMaterial;
        MaterialRenderingMode renderingMode = MaterialRenderingMode::Opaque;

        std::array<float, 4> albedoColor { 1.0f, 1.0f, 1.0f, 1.0f };
        std::string albedoTexture;
        std::string metallicTexture;
        float metallic = 0.0f;
        float smoothness = 0.5f;
        MaterialSmoothnessSource smoothnessSource = MaterialSmoothnessSource::MetallicAlpha;
        bool enableHighlights = true;
        bool enableReflections = true;

        std::string normalTexture;
        float normalScale = 1.0f;
        std::string heightTexture;
        float heightScale = 0.02f;
        std::string occlusionTexture;
        float occlusionStrength = 1.0f;
        bool emissionEnabled = false;
        std::string emissionTexture;
        std::array<float, 4> emissionColor { 0.0f, 0.0f, 0.0f, 1.0f };
        float emissionIntensity = 0.0f;
        MaterialGlobalIlluminationMode globalIllumination = MaterialGlobalIlluminationMode::Realtime;

        std::string detailMaskTexture;
        std::array<float, 2> tiling { 1.0f, 1.0f };
        std::array<float, 2> offset { 0.0f, 0.0f };
        std::string detailAlbedoTexture;
        std::string detailNormalTexture;
        float detailNormalScale = 1.0f;
        std::array<float, 2> detailTiling { 1.0f, 1.0f };
        std::array<float, 2> detailOffset { 0.0f, 0.0f };
        std::int32_t uvSet = 0;
    };
}
