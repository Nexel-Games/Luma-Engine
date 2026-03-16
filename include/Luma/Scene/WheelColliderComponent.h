#pragma once

#include <array>
#include <cstdint>

#include "Luma/Scene/UUID.h"

namespace Luma
{
    enum class VehicleAxleType : std::uint8_t
    {
        Front = 0,
        Rear,
        Custom
    };

    struct WheelColliderComponent
    {
        bool active = true;

        float radius = 0.35f;
        float width = 0.22f;
        float wheelMass = 20.0f;

        float suspensionRestLength = 0.20f;
        float suspensionMaxCompression = 0.10f;
        float suspensionMaxDroop = 0.10f;
        float suspensionStiffness = 30000.0f;
        float suspensionDamping = 4500.0f;
        float suspensionTravel = 0.20f;

        float tireFriction = 1.0f;
        float tireFrictionScale = 1.0f;

        bool steerable = false;
        bool driven = false;
        bool handbrakeAffected = false;
        VehicleAxleType axleType = VehicleAxleType::Front;

        UUID visualWheelEntity = 0;
        std::array<float, 3> suspensionAttachPoint { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> wheelRotationAxis { 1.0f, 0.0f, 0.0f };
        std::array<float, 3> suspensionAxis { 0.0f, -1.0f, 0.0f };
    };
}
