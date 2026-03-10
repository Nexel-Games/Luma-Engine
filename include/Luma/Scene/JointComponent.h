#pragma once

#include <array>
#include <cstdint>
#include <limits>

#include "Luma/Scene/UUID.h"

namespace Luma
{
    enum class JointProjectionMode : std::uint8_t
    {
        None = 0,
        PositionOnly,
        PositionAndRotation
    };

    enum class JointMotionMode : std::uint8_t
    {
        Locked = 0,
        Limited,
        Free
    };

    struct JointComponent
    {
        bool active = true;
        UUID connectedBodyA = 0;
        UUID connectedBodyB = 0;
        bool collideConnectedBodies = false;

        bool enableBreak = false;
        float breakForce = std::numeric_limits<float>::max();
        float breakTorque = std::numeric_limits<float>::max();

        JointProjectionMode projectionMode = JointProjectionMode::PositionAndRotation;
        std::uint32_t solverPositionIterations = 8;
        std::uint32_t solverVelocityIterations = 1;
        float projectionLinearTolerance = 0.10f;
        float projectionAngularToleranceDegrees = 5.0f;
    };
}

