#pragma once

#include <array>
#include <cstdint>

namespace Luma
{
    enum class CameraProjectionMode : std::uint8_t
    {
        Perspective = 0,
        Orthographic
    };

    enum class CameraClearMode : std::uint8_t
    {
        Skybox = 0,
        SolidColor,
        DepthOnly,
        DontClear
    };

    struct CameraComponent
    {
        bool primary = true;
        bool active = true;
        CameraProjectionMode projection = CameraProjectionMode::Perspective;
        float fovDegrees = 60.0f;
        float orthographicSize = 5.0f;
        float nearClip = 0.1f;
        float farClip = 1000.0f;
        float sensorWidth = 36.0f;
        float sensorHeight = 24.0f;
        float focalLength = 50.0f;
        bool useViewportAspectRatio = true;
        float aspectRatio = 16.0f / 9.0f;
        bool constrainAspectRatio = false;
        CameraClearMode clearMode = CameraClearMode::Skybox;
        std::array<float, 4> clearColor { 0.05f, 0.07f, 0.12f, 1.0f };
        std::uint32_t cullingMask = 0xFFFFFFFFu;
        bool hdr = true;
        bool allowPostProcess = true;
        bool allowMSAA = true;
        bool allowMotionBlur = true;
        float exposure = 0.0f;
        int renderPriority = 0;
    };
}
