#pragma once

#include <array>

#include "Luma/Scene/UUID.h"

namespace Luma
{
    struct BuoyancyComponent
    {
        bool active = true;

        float waterLevel = 0.0f;
        UUID waterVolumeEntity = 0;
        float density = 1.0f;
        float drag = 0.8f;
        float angularDrag = 0.6f;

        std::array<std::array<float, 3>, 4> floatPoints {
            std::array<float, 3> { -0.5f, 0.0f, -0.5f },
            std::array<float, 3> { 0.5f, 0.0f, -0.5f },
            std::array<float, 3> { -0.5f, 0.0f, 0.5f },
            std::array<float, 3> { 0.5f, 0.0f, 0.5f }
        };
    };
}

