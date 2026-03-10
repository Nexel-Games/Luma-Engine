#pragma once

#include <array>

namespace Luma
{
    struct TransformComponent
    {
        std::array<float, 3> position { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> rotation { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> scale { 1.0f, 1.0f, 1.0f };

        std::array<float, 3> worldPosition { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> worldRotation { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> worldScale { 1.0f, 1.0f, 1.0f };
        bool dirty = true;
    };
}
