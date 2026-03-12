#pragma once

#include <string>

#include "Luma/Audio/Core/AudioSystem.h"

namespace Luma
{
    struct AudioSourceComponent
    {
        std::string clipAsset;
        bool playOnAwake = false;
        bool looping = false;
        bool spatialized = true;
        bool mute = false;

        float volume = 1.0f;
        float pitch = 1.0f;
        float minDistance = 1.0f;
        float maxDistance = 50.0f;

        Audio::AudioHandle runtimeHandle = 0;
    };
}
