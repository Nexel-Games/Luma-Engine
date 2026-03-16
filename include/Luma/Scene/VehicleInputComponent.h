#pragma once

namespace Luma
{
    struct VehicleInputComponent
    {
        bool active = true;

        float throttle = 0.0f;
        float brake = 0.0f;
        float steering = 0.0f;
        float handbrake = 0.0f;
        float clutch = 0.0f;

        bool gearUpRequested = false;
        bool gearDownRequested = false;
        bool resetRequested = false;
    };
}
