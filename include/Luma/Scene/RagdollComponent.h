#pragma once

#include <string>

namespace Luma
{
    struct RagdollComponent
    {
        bool active = true;
        std::string skeletalMeshAsset;
        std::string physicsAsset;
        float animationPhysicsBlend = 1.0f;
        bool startSimulated = false;
    };
}

