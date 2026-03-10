#pragma once

#include <array>

namespace Luma
{
    struct SliderJointComponent
    {
        std::array<float, 3> axis { 1.0f, 0.0f, 0.0f };

        bool enableLimits = false;
        float lowerLimit = -1.0f;
        float upperLimit = 1.0f;

        bool enableMotor = false;
        float motorSpeed = 0.0f;
        float motorMaxForce = 1000.0f;
    };
}

