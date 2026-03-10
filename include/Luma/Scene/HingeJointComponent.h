#pragma once

#include <array>

namespace Luma
{
    struct HingeJointComponent
    {
        std::array<float, 3> axis { 1.0f, 0.0f, 0.0f };

        bool enableLimits = false;
        float lowerLimitDegrees = -90.0f;
        float upperLimitDegrees = 90.0f;

        bool enableMotor = false;
        float motorVelocityDegreesPerSecond = 0.0f;
        float motorMaxForce = 1000.0f;
    };
}

