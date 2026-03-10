#pragma once

#include <array>

namespace Luma
{
    struct SpotLightComponent
    {
        bool active = true;
        std::array<float, 3> color { 1.0f, 0.96f, 0.88f };
        float intensity = 10.0f;
        float range = 18.0f;
        float innerConeAngle = 22.5f;
        float outerConeAngle = 35.0f;
        bool castShadows = false;
        float volumetricScatteringIntensity = 1.0f;
    };
}
