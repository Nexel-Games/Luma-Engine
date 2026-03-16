#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "Luma/Renderer/PostProcessSettings.h"
#include "Luma/RHI/RHIResources.h"

namespace Luma
{
    struct DirectionalLightDesc
    {
        bool enabled = true;
        std::array<float, 3> direction { -0.35f, -0.85f, -0.40f };
        std::array<float, 3> color { 1.0f, 1.0f, 1.0f };
        float intensity = 1.0f;
        bool castsShadows = false;
    };

    struct PointLightDesc
    {
        bool enabled = false;
        std::array<float, 3> position { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> color { 1.0f, 1.0f, 1.0f };
        float intensity = 0.0f;
        float range = 1.0f;
        float attenuation = 1.0f;
        bool castsShadows = false;
        bool softShadows = true;
        float shadowBias = 0.0025f;
        std::uint32_t shadowResolution = 512;
    };

    struct SpotLightDesc
    {
        bool enabled = false;
        std::array<float, 3> position { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> direction { 0.0f, 0.0f, -1.0f };
        std::array<float, 3> color { 1.0f, 1.0f, 1.0f };
        float intensity = 0.0f;
        float range = 1.0f;
        float innerConeAngleDegrees = 20.0f;
        float outerConeAngleDegrees = 35.0f;
        bool castsShadows = false;
    };

    struct ImageBasedLightDesc
    {
        bool enabled = false;
        std::array<float, 3> diffuseColor { 0.0f, 0.0f, 0.0f };
        float diffuseIntensity = 0.0f;
        std::array<float, 3> specularColor { 0.0f, 0.0f, 0.0f };
        float specularIntensity = 0.0f;
        float ambientOcclusionStrength = 1.0f;
        float rotationDegrees = 0.0f;
        bool hasEnvironmentTexture = false;
        bool lowerHemisphereIsSolidColor = false;
        std::array<float, 3> lowerHemisphereColor { 0.0f, 0.0f, 0.0f };
        float exposureMultiplier = 1.0f;
        float skyboxExposureMultiplier = 1.0f;
        float sunSpecularMultiplier = 1.0f;
        bool autoExposureEnabled = false;
        float autoExposureMinEV = -6.0f;
        float autoExposureMaxEV = 6.0f;
        float autoExposureSpeedUp = 3.0f;
        float autoExposureSpeedDown = 1.5f;
    };

    struct PostProcessDesc
    {
        bool active = false;
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

    class LightingSystem
    {
    public:
        static constexpr std::uint32_t kDefaultBinding = 4;
        static constexpr std::size_t kMaxPointLights = 4;
        static constexpr std::size_t kMaxSpotLights = 4;
        static constexpr std::size_t kPointShadowFaceCount = 6;
        static std::uint32_t UniformBufferSize();

        bool Initialize(std::uint32_t descriptorBinding = kDefaultBinding);
        void Shutdown();

        void SetAmbientLight(const std::array<float, 3>& color, float intensity);
        void SetCameraWorldPosition(const std::array<float, 3>& position);
        void SetDirectionalLight(const DirectionalLightDesc& light);
        void SetPointLights(const std::array<PointLightDesc, kMaxPointLights>& lights, std::size_t count);
        void SetSpotLights(const std::array<SpotLightDesc, kMaxSpotLights>& lights, std::size_t count);
        void SetDirectionalShadow(const std::array<float, 16>& matrix, bool enabled, float bias, float texelSize);
        void SetPointShadow(
            const std::array<std::array<float, 16>, kPointShadowFaceCount>& matrices,
            const std::array<float, 4>& lightPositionRange,
            bool enabled,
            float bias,
            bool softShadows,
            float lightIndex,
            float atlasInvWidth,
            float atlasInvHeight);
        void SetSpotShadow(const std::array<float, 16>& matrix, bool enabled, float bias, float texelSize, float lightIndex);
        void SetImageBasedLight(const ImageBasedLightDesc& light);
        void SetPostProcess(const PostProcessDesc& postProcess);

        std::uint32_t GetDescriptorBinding() const;
        std::array<float, 3> GetAmbientColor() const;
        float GetAmbientIntensity() const;
        const std::array<float, 3>& GetCameraWorldPosition() const;
        const DirectionalLightDesc& GetDirectionalLight() const;
        const std::array<PointLightDesc, kMaxPointLights>& GetPointLights() const;
        std::size_t GetPointLightCount() const;
        const std::array<SpotLightDesc, kMaxSpotLights>& GetSpotLights() const;
        std::size_t GetSpotLightCount() const;
        const ImageBasedLightDesc& GetImageBasedLight() const;
        const PostProcessDesc& GetPostProcess() const;
        std::size_t GetDirectionalLightCount() const;

        bool BuildDescriptorSetDesc(DescriptorSetDesc& outDesc) const;

    private:
        struct LightingUniformData
        {
            float ambientColorIntensity[4];
            float directionalDirectionIntensity[4];
            float directionalColorEnabled[4];
            float cameraWorldPosition[4];
            float iblDiffuseColorIntensity[4];
            float iblSpecularColorIntensity[4];
            float iblParams[4];
            float iblRotationAndFlags[4];
            float iblLowerHemisphereColorFlag[4];
            float iblExposureAndSun[4];
            float postProcessToneMap[4];
            float postProcessColor[4];
            float postProcessCurve[4];
            float postProcessBloom[4];
            float postProcessColorBalance[4];
            float postProcessFilmCurve[4];
            float localLightCounts[4];
            float pointPositionRange[kMaxPointLights][4];
            float pointColorIntensity[kMaxPointLights][4];
            float spotPositionRange[kMaxSpotLights][4];
            float spotDirectionInner[kMaxSpotLights][4];
            float spotColorOuter[kMaxSpotLights][4];
            float directionalShadowMatrix[16];
            float pointShadowMatrices[kPointShadowFaceCount][16];
            float spotShadowMatrix[16];
            float directionalShadowParams[4];
            float pointShadowParams[4];
            float pointShadowLightPositionRange[4];
            float pointShadowAtlasInvSize[4];
            float spotShadowParams[4];
        };

        static std::array<float, 3> NormalizeDirection(const std::array<float, 3>& direction);
        static float ClampToNonNegative(float value);

        bool m_Initialized = false;
        std::uint32_t m_DescriptorBinding = kDefaultBinding;
        std::array<float, 3> m_AmbientColor { 0.09f, 0.11f, 0.14f };
        float m_AmbientIntensity = 1.0f;
        std::array<float, 3> m_CameraWorldPosition { 0.0f, 0.0f, 5.0f };
        DirectionalLightDesc m_DirectionalLight;
        std::array<PointLightDesc, kMaxPointLights> m_PointLights {};
        std::size_t m_PointLightCount = 0;
        std::array<SpotLightDesc, kMaxSpotLights> m_SpotLights {};
        std::size_t m_SpotLightCount = 0;
        std::array<float, 16> m_DirectionalShadowMatrix {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f };
        bool m_DirectionalShadowEnabled = false;
        float m_DirectionalShadowBias = 0.0015f;
        float m_DirectionalShadowTexelSize = 1.0f / 1024.0f;
        std::array<std::array<float, 16>, kPointShadowFaceCount> m_PointShadowMatrices {{
            { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
            { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
            { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
            { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
            { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f },
            { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f }
        }};
        std::array<float, 4> m_PointShadowLightPositionRange { 0.0f, 0.0f, 0.0f, 1.0f };
        bool m_PointShadowEnabled = false;
        float m_PointShadowBias = 0.0025f;
        bool m_PointShadowSoftShadows = true;
        float m_PointShadowLightIndex = -1.0f;
        float m_PointShadowAtlasInvWidth = 1.0f / 1536.0f;
        float m_PointShadowAtlasInvHeight = 1.0f / 1024.0f;
        std::array<float, 16> m_SpotShadowMatrix {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f };
        bool m_SpotShadowEnabled = false;
        float m_SpotShadowBias = 0.0010f;
        float m_SpotShadowTexelSize = 1.0f / 1024.0f;
        float m_SpotShadowLightIndex = -1.0f;
        ImageBasedLightDesc m_ImageBasedLight;
        PostProcessDesc m_PostProcess;
    };
}
