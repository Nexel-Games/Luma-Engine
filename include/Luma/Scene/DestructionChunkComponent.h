#pragma once

#include "Luma/Scene/UUID.h"

namespace Luma
{
    struct DestructionChunkComponent
    {
        UUID sourceDestructible = 0;
        float ageSeconds = 0.0f;
        float lifetimeSeconds = 0.0f;
    };
}
