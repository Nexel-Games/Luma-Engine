#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Luma/Scene/UUID.h"

namespace Luma
{
    enum class VehicleType : std::uint8_t
    {
        Car = 0,
        Truck,
        Bike
    };

    enum class VehicleInputSource : std::uint8_t
    {
        Player = 0,
        AI,
        Script
    };

    struct VehicleComponent
    {
        bool active = true;
        bool simulationEnabled = true;
        VehicleType vehicleType = VehicleType::Car;
        VehicleInputSource inputSource = VehicleInputSource::Player;

        bool useCenterOfMassOverride = false;
        std::array<float, 3> centerOfMassOffset { 0.0f, -0.35f, 0.0f };
        UUID chassisRigidBody = 0;
        std::vector<UUID> wheelEntities {};

        float dragCoefficient = 0.25f;
        float rollingResistance = 12.0f;
        float aeroDownforce = 0.0f;

        float engineTorque = 500.0f;
        float idleRPM = 900.0f;
        float maxRPM = 6500.0f;
        float reverseGearRatio = 3.2f;
        std::vector<float> gearRatios { 3.5f, 2.1f, 1.4f, 1.0f, 0.8f };
        float differentialRatio = 3.4f;
        float brakeForce = 4500.0f;
        float handbrakeForce = 7000.0f;
        float frontBrakeBias = 0.65f;
        float frontDriveBias = 0.5f;
        float tireFrictionScale = 1.0f;

        float suspensionStiffness = 30000.0f;
        float suspensionDamping = 4500.0f;
        float suspensionTravel = 0.20f;

        float maxSteerAngleDegrees = 35.0f;
        float steerSensitivity = 1.0f;
        float shiftUpRPM = 5800.0f;
        float shiftDownRPM = 2200.0f;

        bool automaticTransmission = true;
        bool enableABS = false;
        bool enableTCS = false;
        bool ackermannSteering = true;
        bool autoFlip = true;
        bool useSubstepping = true;
        bool sleepWhenInactive = true;

        std::string inputMap = "Vehicle.Default";
        std::string tuningAsset;
    };
}
