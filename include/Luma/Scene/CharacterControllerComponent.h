#pragma once

#include <cstdint>

namespace Luma
{
    enum class CharacterMovementMode : std::uint8_t
    {
        Walk = 0,
        Fly,
        Swim
    };

    struct CharacterControllerComponent
    {
        bool active = true;
        CharacterMovementMode movementMode = CharacterMovementMode::Walk;

        float radius = 0.35f;
        float height = 1.8f;
        float stepOffset = 0.35f;
        float slopeLimitDegrees = 45.0f;
        float skinWidth = 0.05f;
        float minMoveDistance = 0.001f;
        float gravityScale = 1.0f;

        std::uint32_t collisionLayer = 1;
        std::uint32_t collisionMask = 0xFFFFFFFFu;
        bool isGrounded = false;
    };
}

