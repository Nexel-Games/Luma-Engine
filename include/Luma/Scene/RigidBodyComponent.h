#pragma once

#include <array>

#include "Luma/Physics/PhysicsTypes.h"

namespace Luma
{
    struct RigidBodyComponent
    {
        bool active = true;
        RigidBodyType bodyType = RigidBodyType::Dynamic;
        bool enableGravity = true;
        bool startAwake = true;
        bool enableCCD = false;

        float mass = 1.0f;
        float linearDamping = 0.05f;
        float angularDamping = 0.05f;
        float maxLinearVelocity = 100.0f;
        float maxAngularVelocity = 360.0f;

        std::array<bool, 3> lockLinearAxes { false, false, false };
        std::array<bool, 3> lockAngularAxes { false, false, false };

        std::array<float, 3> linearVelocity { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> angularVelocity { 0.0f, 0.0f, 0.0f };
        bool sleeping = false;
    };
}
