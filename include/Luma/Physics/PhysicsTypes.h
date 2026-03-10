#pragma once

#include <array>
#include <cstdint>

namespace Luma
{
    enum class PhysicsBackendType : std::uint8_t
    {
        None = 0,
        PhysX
    };

    enum class RigidBodyType : std::uint8_t
    {
        Static = 0,
        Dynamic,
        Kinematic
    };

    enum class ColliderShapeType : std::uint8_t
    {
        Box = 0,
        Sphere,
        Capsule,
        Mesh,
        Cylinder
    };

    struct PhysicsMaterialDesc
    {
        float staticFriction = 0.60f;
        float dynamicFriction = 0.60f;
        float restitution = 0.00f;
    };

    struct PhysicsSettings
    {
        PhysicsBackendType backend = PhysicsBackendType::PhysX;
        std::array<float, 3> gravity { 0.0f, -9.81f, 0.0f };
        float fixedTimeStep = 1.0f / 60.0f;
        std::uint32_t maxSubSteps = 4;
        bool enableCCD = true;
        bool enableSleeping = true;
    };
}
