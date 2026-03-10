#pragma once

#include <string>
#include <vector>

#include "Luma/Scene/UUID.h"

namespace Luma
{
    struct VehicleComponent
    {
        bool active = true;

        UUID chassisRigidBody = 0;
        std::vector<UUID> wheelEntities {};

        float engineTorque = 500.0f;
        float maxRPM = 6500.0f;
        float gearRatio = 3.5f;
        float differentialRatio = 3.4f;
        float tireFrictionScale = 1.0f;

        float suspensionStiffness = 30000.0f;
        float suspensionDamping = 4500.0f;
        float suspensionTravel = 0.20f;

        float maxSteerAngleDegrees = 35.0f;
        float steerSensitivity = 1.0f;

        bool enableABS = false;
        bool enableTCS = false;

        std::string inputMap = "Vehicle.Default";
    };
}

