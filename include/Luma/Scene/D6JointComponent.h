#pragma once

#include <array>

#include "Luma/Scene/JointComponent.h"

namespace Luma
{
    struct D6JointComponent
    {
        // Linear X/Y/Z.
        std::array<JointMotionMode, 3> linearMotion {
            JointMotionMode::Locked,
            JointMotionMode::Locked,
            JointMotionMode::Locked
        };

        // Twist / SwingY / SwingZ.
        std::array<JointMotionMode, 3> angularMotion {
            JointMotionMode::Locked,
            JointMotionMode::Locked,
            JointMotionMode::Locked
        };

        float linearLimit = 1.0f;

        float twistLowerLimitDegrees = -45.0f;
        float twistUpperLimitDegrees = 45.0f;
        float swingYLimitDegrees = 45.0f;
        float swingZLimitDegrees = 45.0f;

        bool enableLinearDrive = false;
        std::array<float, 3> linearDrivePositionTarget { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> linearDriveVelocityTarget { 0.0f, 0.0f, 0.0f };
        float linearDriveStiffness = 50.0f;
        float linearDriveDamping = 5.0f;
        float linearDriveForceLimit = 1000.0f;

        bool enableAngularDrive = false;
        std::array<float, 3> angularDrivePositionTarget { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> angularDriveVelocityTarget { 0.0f, 0.0f, 0.0f };
        float angularDriveStiffness = 50.0f;
        float angularDriveDamping = 5.0f;
        float angularDriveForceLimit = 1000.0f;
    };
}

