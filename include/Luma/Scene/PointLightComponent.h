#pragma once

#include <array>

namespace Luma
{
    struct PointLightComponent
    {
        bool active = true;
        std::array<float, 3> color { 1.0f, 0.94f, 0.84f };
        float intensity = 8.0f;
        float range = 12.0f;
        bool castShadows = false;
        float volumetricScatteringIntensity = 1.0f;
    };
}
