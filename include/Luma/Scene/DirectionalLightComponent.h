#pragma once

#include <array>

namespace Luma
{
    struct DirectionalLightComponent
    {
        bool active = true;
        std::array<float, 3> color { 1.0f, 1.0f, 1.0f };
        float intensity = 1.0f;
        bool castShadows = false;
    };
}
