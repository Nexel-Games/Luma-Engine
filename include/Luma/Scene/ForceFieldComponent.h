#pragma once

#include <array>
#include <cstdint>

namespace Luma
{
    enum class ForceFieldShape : std::uint8_t
    {
        Box = 0,
        Sphere,
        Capsule
    };

    enum class ForceFieldType : std::uint8_t
    {
        Directional = 0,
        Radial,
        Custom
    };

    struct ForceFieldComponent
    {
        bool active = true;
        ForceFieldShape shape = ForceFieldShape::Box;
        ForceFieldType type = ForceFieldType::Directional;

        std::array<float, 3> center { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> boxHalfExtents { 1.0f, 1.0f, 1.0f };
        float sphereRadius = 1.0f;
        float capsuleRadius = 0.5f;
        float capsuleHalfHeight = 1.0f;

        std::array<float, 3> direction { 0.0f, 1.0f, 0.0f };
        float strength = 10.0f;
        float falloff = 1.0f;

        bool affectDynamicBodiesOnly = true;
        bool affectCharacters = true;
    };
}

