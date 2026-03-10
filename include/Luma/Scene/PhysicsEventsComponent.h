#pragma once

namespace Luma
{
    struct PhysicsEventsComponent
    {
        bool onCollisionEnter = true;
        bool onCollisionStay = false;
        bool onCollisionExit = true;

        bool onTriggerEnter = true;
        bool onTriggerStay = false;
        bool onTriggerExit = true;

        float contactImpulseThreshold = 0.0f;
    };
}

