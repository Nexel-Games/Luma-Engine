#pragma once

#include <array>
#include <string>

namespace Luma
{
    enum class SceneSkyLightType
    {
        Color = 0,
        HDRI = 1,
        Procedural = 2
    };

    enum class SceneSkyUpdateMode
    {
        Static = 0,
        Dynamic = 1
    };

    struct SkyLightComponent
    {
        bool active = true;
        std::array<float, 3> color { 0.8f, 0.86f, 1.0f };
        float intensity = 0.15f;

        SceneSkyLightType skyLightType = SceneSkyLightType::HDRI;
        bool castShadows = false;

        std::array<float, 3> skyColor { 0.8f, 0.86f, 1.0f };
        float colorIntensity = 1.0f;

        std::string environmentMap;
        std::string irradianceMap;
        std::string prefilteredReflectionMap;
        std::string brdfLut;

        float rotation = 0.0f;
        float diffuseIntensity = 1.0f;
        float reflectionIntensity = 1.0f;
        float exposureEV = 0.0f;
        float skyboxExposureEV = 0.0f;
        float sunIntensityMultiplier = 1.0f;
        float sunSpecularMultiplier = 1.0f;
        bool autoExposureEnabled = false;
        float autoExposureMinEV = -6.0f;
        float autoExposureMaxEV = 6.0f;
        float autoExposureSpeedUp = 3.0f;
        float autoExposureSpeedDown = 1.5f;

        float ambientOcclusionStrength = 1.0f;
        bool affectAmbientOcclusion = true;

        bool lowerHemisphereIsBlack = true;
        std::array<float, 3> lowerHemisphereColor { 0.0f, 0.0f, 0.0f };

        float blendFactor = 1.0f;
        int priority = 0;

        bool realTimeCapture = false;
        float captureUpdateInterval = 0.5f;

        float volumetricScatteringIntensity = 1.0f;
        bool affectFog = true;

        SceneSkyUpdateMode updateMode = SceneSkyUpdateMode::Static;
        bool rebuildIBLRequested = false;
        bool previewRefreshRequested = true;
    };
}
