#pragma once

#include <array>
#include <string>

#include "Luma/Physics/PhysicsTypes.h"

namespace Luma
{
    struct ColliderComponent
    {
        bool active = true;
        bool isTrigger = false;
        ColliderShapeType shape = ColliderShapeType::Box;

        std::array<float, 3> center { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> boxHalfExtents { 0.5f, 0.5f, 0.5f };
        float sphereRadius = 0.5f;
        float capsuleRadius = 0.5f;
        float capsuleHalfHeight = 0.5f;
        bool meshConvex = true;
        std::string meshSource;

        PhysicsMaterialDesc material {};
    };
}
