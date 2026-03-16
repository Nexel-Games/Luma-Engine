#pragma once

#include <cstdint>
#include <string>

namespace Luma
{
    enum class DestructionActivationMode : std::uint8_t
    {
        StartIntact = 0,
        StartFractured
    };

    enum class DestructionChunkSize : std::uint8_t
    {
        Large = 0,
        Medium,
        Small,
        Tiny
    };

    struct DestructibleComponent
    {
        bool active = true;
        std::string blastAsset;
        std::string intactMeshOverride;
        bool visibleIntactMesh = true;
        bool fractureOnImpact = true;
        bool accumulateDamage = true;
        bool worldSupport = true;
        bool stressDamage = false;
        DestructionActivationMode activationMode = DestructionActivationMode::StartIntact;
        DestructionChunkSize chunkSize = DestructionChunkSize::Medium;
        // 0 keeps preset behavior from chunkSize. Any value > 0 requests a specific runtime chunk count.
        int desiredChunkCount = 0;
        float damageThreshold = 50.0f;
        float impactDamageScale = 1.0f;
        float damageSpread = 1.0f;
        float chunkMassScale = 1.0f;
        float maxChunkSpeed = 100.0f;
        float debrisLifetime = 10.0f;
        int supportDepth = 0;
    };
}
