#pragma once

#include <array>
#include <cstdint>

namespace Luma
{
    enum class PointLightMode : std::uint8_t
    {
        Realtime = 0,
        Baked,
        Mixed
    };

    enum class PointLightShadowType : std::uint8_t
    {
        HardShadows = 0,
        SoftShadows
    };

    enum class PointLightRenderMode : std::uint8_t
    {
        Auto = 0,
        Important,
        NotImportant
    };

    struct PointLightComponent
    {
        bool active = true;
        std::array<float, 3> color { 1.0f, 0.94f, 0.84f };
        PointLightMode mode = PointLightMode::Realtime;
        float temperature = 6500.0f;
        float intensity = 8.0f;
        float indirectMultiplier = 1.0f;
        float range = 12.0f;
        float attenuation = 1.0f;
        bool castShadows = false;
        PointLightShadowType shadowType = PointLightShadowType::SoftShadows;
        float shadowBias = 0.0025f;
        std::uint32_t shadowResolution = 512;
        float bakedShadowRadius = 0.0f;
        bool drawHalo = false;
        PointLightRenderMode renderMode = PointLightRenderMode::Important;
        std::uint32_t cullingMask = 0xFFFFFFFFu;
    };
}
