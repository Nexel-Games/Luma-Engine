#pragma once

namespace Luma
{
    struct WheelColliderComponent
    {
        bool active = true;

        float radius = 0.35f;
        float width = 0.22f;
        float wheelMass = 20.0f;

        float suspensionStiffness = 30000.0f;
        float suspensionDamping = 4500.0f;
        float suspensionTravel = 0.20f;

        float tireFriction = 1.0f;
    };
}

