#pragma once

#include <array>

#include "Luma/Renderer/PostProcessSettings.h"

namespace Luma
{
    struct PostProcessComponent
    {
        bool active = true;
        int priority = 0;
        bool unbound = true;
        std::array<float, 3> volumeExtents { 5.0f, 5.0f, 5.0f };
        float blendDistance = 2.0f;

        bool toneMappingEnabled = true;
        ToneMappingOperator toneMappingOperator = ToneMappingOperator::ACES;
        float exposureCompensationEV = 0.0f;
        float eyeAdaptationCompensationEV = 0.0f;
        float whitePoint = 1.0f;

        std::array<float, 3> colorFilter { 1.0f, 1.0f, 1.0f };
        std::array<float, 3> colorBalance { 1.0f, 1.0f, 1.0f };
        float saturation = 1.0f;
        float contrast = 1.0f;
        float gamma = 1.0f;
        float filmCurveShoulder = 1.0f;
        float filmCurveLinear = 1.0f;
        float filmCurveToe = 1.0f;

        bool bloomEnabled = false;
        float bloomIntensity = 0.15f;
        float bloomThreshold = 1.0f;
        float bloomKnee = 0.5f;
    };
}
