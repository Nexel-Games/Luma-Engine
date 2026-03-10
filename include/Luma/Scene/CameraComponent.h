#pragma once

namespace Luma
{
    struct CameraComponent
    {
        bool primary = true;
        float fovDegrees = 60.0f;
        float nearClip = 0.1f;
        float farClip = 1000.0f;
    };
}
