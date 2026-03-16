#include "Luma/RHI/LightingSystem.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Luma
{
    std::uint32_t LightingSystem::UniformBufferSize()
    {
        return static_cast<std::uint32_t>(sizeof(LightingUniformData));
    }

    bool LightingSystem::Initialize(const std::uint32_t descriptorBinding)
    {
        m_DescriptorBinding = descriptorBinding;
        m_AmbientColor = { 0.09f, 0.11f, 0.14f };
        m_AmbientIntensity = 1.0f;
        m_CameraWorldPosition = { 0.0f, 0.0f, 5.0f };
        m_DirectionalLight = DirectionalLightDesc {};
        m_DirectionalLight.direction = NormalizeDirection(m_DirectionalLight.direction);
        m_DirectionalLight.intensity = ClampToNonNegative(m_DirectionalLight.intensity);
        m_PointLights.fill(PointLightDesc {});
        m_PointLightCount = 0;
        m_SpotLights.fill(SpotLightDesc {});
        m_SpotLightCount = 0;
        m_DirectionalShadowEnabled = false;
        m_DirectionalShadowBias = 0.0015f;
        m_DirectionalShadowTexelSize = 1.0f / 1024.0f;
        m_PointShadowEnabled = false;
        m_PointShadowBias = 0.0025f;
        m_PointShadowSoftShadows = true;
        m_PointShadowLightIndex = -1.0f;
        m_PointShadowLightPositionRange = { 0.0f, 0.0f, 0.0f, 1.0f };
        m_PointShadowAtlasInvWidth = 1.0f / 1536.0f;
        m_PointShadowAtlasInvHeight = 1.0f / 1024.0f;
        m_SpotShadowEnabled = false;
        m_SpotShadowBias = 0.0010f;
        m_SpotShadowTexelSize = 1.0f / 1024.0f;
        m_SpotShadowLightIndex = -1.0f;
        m_ImageBasedLight = ImageBasedLightDesc {};
        m_PostProcess = PostProcessDesc {};
        m_Initialized = true;
        return true;
    }

    void LightingSystem::Shutdown()
    {
        m_Initialized = false;
        m_DescriptorBinding = kDefaultBinding;
        m_AmbientColor = { 0.09f, 0.11f, 0.14f };
        m_AmbientIntensity = 1.0f;
        m_CameraWorldPosition = { 0.0f, 0.0f, 5.0f };
        m_DirectionalLight = DirectionalLightDesc {};
        m_PointLights.fill(PointLightDesc {});
        m_PointLightCount = 0;
        m_SpotLights.fill(SpotLightDesc {});
        m_SpotLightCount = 0;
        m_DirectionalShadowEnabled = false;
        m_PointShadowEnabled = false;
        m_PointShadowSoftShadows = true;
        m_PointShadowLightIndex = -1.0f;
        m_SpotShadowEnabled = false;
        m_SpotShadowLightIndex = -1.0f;
        m_ImageBasedLight = ImageBasedLightDesc {};
        m_PostProcess = PostProcessDesc {};
    }

    void LightingSystem::SetAmbientLight(const std::array<float, 3>& color, const float intensity)
    {
        m_AmbientColor = {
            ClampToNonNegative(color[0]),
            ClampToNonNegative(color[1]),
            ClampToNonNegative(color[2])
        };
        m_AmbientIntensity = ClampToNonNegative(intensity);
    }

    void LightingSystem::SetCameraWorldPosition(const std::array<float, 3>& position)
    {
        m_CameraWorldPosition = position;
    }

    void LightingSystem::SetDirectionalLight(const DirectionalLightDesc& light)
    {
        m_DirectionalLight = light;
        m_DirectionalLight.direction = NormalizeDirection(light.direction);
        m_DirectionalLight.color = {
            ClampToNonNegative(light.color[0]),
            ClampToNonNegative(light.color[1]),
            ClampToNonNegative(light.color[2])
        };
        m_DirectionalLight.intensity = ClampToNonNegative(light.intensity);
    }

    void LightingSystem::SetPointLights(const std::array<PointLightDesc, kMaxPointLights>& lights, const std::size_t count)
    {
        m_PointLightCount = std::min(count, kMaxPointLights);
        for (std::size_t index = 0; index < kMaxPointLights; ++index)
        {
            if (index >= m_PointLightCount)
            {
                m_PointLights[index] = PointLightDesc {};
                continue;
            }

            m_PointLights[index] = lights[index];
            m_PointLights[index].color = {
                ClampToNonNegative(lights[index].color[0]),
                ClampToNonNegative(lights[index].color[1]),
                ClampToNonNegative(lights[index].color[2])
            };
            m_PointLights[index].intensity = ClampToNonNegative(lights[index].intensity);
            m_PointLights[index].range = std::max(lights[index].range, 0.001f);
            m_PointLights[index].attenuation = std::max(lights[index].attenuation, 0.001f);
            m_PointLights[index].castsShadows = lights[index].castsShadows;
            m_PointLights[index].softShadows = lights[index].softShadows;
            m_PointLights[index].shadowBias = std::max(lights[index].shadowBias, 0.0f);
            m_PointLights[index].shadowResolution = lights[index].shadowResolution;
        }
    }

    void LightingSystem::SetSpotLights(const std::array<SpotLightDesc, kMaxSpotLights>& lights, const std::size_t count)
    {
        m_SpotLightCount = std::min(count, kMaxSpotLights);
        for (std::size_t index = 0; index < kMaxSpotLights; ++index)
        {
            if (index >= m_SpotLightCount)
            {
                m_SpotLights[index] = SpotLightDesc {};
                continue;
            }

            m_SpotLights[index] = lights[index];
            m_SpotLights[index].direction = NormalizeDirection(lights[index].direction);
            m_SpotLights[index].color = {
                ClampToNonNegative(lights[index].color[0]),
                ClampToNonNegative(lights[index].color[1]),
                ClampToNonNegative(lights[index].color[2])
            };
            m_SpotLights[index].intensity = ClampToNonNegative(lights[index].intensity);
            m_SpotLights[index].range = std::max(lights[index].range, 0.001f);
            m_SpotLights[index].innerConeAngleDegrees =
                std::clamp(lights[index].innerConeAngleDegrees, 0.0f, 89.0f);
            m_SpotLights[index].outerConeAngleDegrees =
                std::clamp(
                    lights[index].outerConeAngleDegrees,
                    m_SpotLights[index].innerConeAngleDegrees + 0.1f,
                    89.9f);
        }
    }

    void LightingSystem::SetDirectionalShadow(
        const std::array<float, 16>& matrix,
        const bool enabled,
        const float bias,
        const float texelSize)
    {
        m_DirectionalShadowMatrix = matrix;
        m_DirectionalShadowEnabled = enabled;
        m_DirectionalShadowBias = std::max(bias, 0.0f);
        m_DirectionalShadowTexelSize = std::max(texelSize, 1.0e-6f);
    }

    void LightingSystem::SetPointShadow(
        const std::array<std::array<float, 16>, kPointShadowFaceCount>& matrices,
        const std::array<float, 4>& lightPositionRange,
        const bool enabled,
        const float bias,
        const bool softShadows,
        const float lightIndex,
        const float atlasInvWidth,
        const float atlasInvHeight)
    {
        m_PointShadowMatrices = matrices;
        m_PointShadowLightPositionRange = lightPositionRange;
        m_PointShadowEnabled = enabled;
        m_PointShadowBias = std::max(bias, 0.0f);
        m_PointShadowSoftShadows = softShadows;
        m_PointShadowLightIndex = lightIndex;
        m_PointShadowAtlasInvWidth = std::max(atlasInvWidth, 1.0e-6f);
        m_PointShadowAtlasInvHeight = std::max(atlasInvHeight, 1.0e-6f);
    }

    void LightingSystem::SetSpotShadow(
        const std::array<float, 16>& matrix,
        const bool enabled,
        const float bias,
        const float texelSize,
        const float lightIndex)
    {
        m_SpotShadowMatrix = matrix;
        m_SpotShadowEnabled = enabled;
        m_SpotShadowBias = std::max(bias, 0.0f);
        m_SpotShadowTexelSize = std::max(texelSize, 1.0e-6f);
        m_SpotShadowLightIndex = lightIndex;
    }

    void LightingSystem::SetImageBasedLight(const ImageBasedLightDesc& light)
    {
        m_ImageBasedLight.enabled = light.enabled;
        m_ImageBasedLight.diffuseColor = {
            ClampToNonNegative(light.diffuseColor[0]),
            ClampToNonNegative(light.diffuseColor[1]),
            ClampToNonNegative(light.diffuseColor[2])
        };
        m_ImageBasedLight.diffuseIntensity = ClampToNonNegative(light.diffuseIntensity);
        m_ImageBasedLight.specularColor = {
            ClampToNonNegative(light.specularColor[0]),
            ClampToNonNegative(light.specularColor[1]),
            ClampToNonNegative(light.specularColor[2])
        };
        m_ImageBasedLight.specularIntensity = ClampToNonNegative(light.specularIntensity);
        m_ImageBasedLight.ambientOcclusionStrength =
            std::clamp(light.ambientOcclusionStrength, 0.0f, 1.0f);
        m_ImageBasedLight.rotationDegrees = light.rotationDegrees;
        m_ImageBasedLight.hasEnvironmentTexture = light.hasEnvironmentTexture;
        m_ImageBasedLight.lowerHemisphereIsSolidColor = light.lowerHemisphereIsSolidColor;
        m_ImageBasedLight.lowerHemisphereColor = {
            ClampToNonNegative(light.lowerHemisphereColor[0]),
            ClampToNonNegative(light.lowerHemisphereColor[1]),
            ClampToNonNegative(light.lowerHemisphereColor[2])
        };
        m_ImageBasedLight.exposureMultiplier = std::max(light.exposureMultiplier, 1.0e-4f);
        m_ImageBasedLight.skyboxExposureMultiplier = std::max(light.skyboxExposureMultiplier, 1.0e-4f);
        m_ImageBasedLight.sunSpecularMultiplier = std::max(light.sunSpecularMultiplier, 0.0f);
        m_ImageBasedLight.autoExposureEnabled = light.autoExposureEnabled;
        m_ImageBasedLight.autoExposureMinEV = light.autoExposureMinEV;
        m_ImageBasedLight.autoExposureMaxEV = light.autoExposureMaxEV;
        m_ImageBasedLight.autoExposureSpeedUp = light.autoExposureSpeedUp;
        m_ImageBasedLight.autoExposureSpeedDown = light.autoExposureSpeedDown;
    }

    void LightingSystem::SetPostProcess(const PostProcessDesc& postProcess)
    {
        m_PostProcess.active = postProcess.active;
        m_PostProcess.toneMappingEnabled = postProcess.toneMappingEnabled;
        m_PostProcess.toneMappingOperator = postProcess.toneMappingOperator;
        m_PostProcess.exposureCompensationEV = postProcess.exposureCompensationEV;
        m_PostProcess.eyeAdaptationCompensationEV = postProcess.eyeAdaptationCompensationEV;
        m_PostProcess.whitePoint = std::max(postProcess.whitePoint, 1.0e-4f);
        m_PostProcess.colorFilter = {
            ClampToNonNegative(postProcess.colorFilter[0]),
            ClampToNonNegative(postProcess.colorFilter[1]),
            ClampToNonNegative(postProcess.colorFilter[2])
        };
        m_PostProcess.colorBalance = {
            ClampToNonNegative(postProcess.colorBalance[0]),
            ClampToNonNegative(postProcess.colorBalance[1]),
            ClampToNonNegative(postProcess.colorBalance[2])
        };
        m_PostProcess.saturation = std::max(postProcess.saturation, 0.0f);
        m_PostProcess.contrast = std::max(postProcess.contrast, 0.0f);
        m_PostProcess.gamma = std::max(postProcess.gamma, 1.0e-4f);
        m_PostProcess.filmCurveShoulder = std::max(postProcess.filmCurveShoulder, 0.1f);
        m_PostProcess.filmCurveLinear = std::max(postProcess.filmCurveLinear, 0.1f);
        m_PostProcess.filmCurveToe = std::max(postProcess.filmCurveToe, 0.1f);
        m_PostProcess.bloomEnabled = postProcess.bloomEnabled;
        m_PostProcess.bloomIntensity = std::max(postProcess.bloomIntensity, 0.0f);
        m_PostProcess.bloomThreshold = std::max(postProcess.bloomThreshold, 0.0f);
        m_PostProcess.bloomKnee = std::max(postProcess.bloomKnee, 1.0e-4f);
    }

    std::uint32_t LightingSystem::GetDescriptorBinding() const
    {
        return m_DescriptorBinding;
    }

    std::array<float, 3> LightingSystem::GetAmbientColor() const
    {
        return m_AmbientColor;
    }

    float LightingSystem::GetAmbientIntensity() const
    {
        return m_AmbientIntensity;
    }

    const std::array<float, 3>& LightingSystem::GetCameraWorldPosition() const
    {
        return m_CameraWorldPosition;
    }

    const DirectionalLightDesc& LightingSystem::GetDirectionalLight() const
    {
        return m_DirectionalLight;
    }

    const std::array<PointLightDesc, LightingSystem::kMaxPointLights>& LightingSystem::GetPointLights() const
    {
        return m_PointLights;
    }

    std::size_t LightingSystem::GetPointLightCount() const
    {
        return m_PointLightCount;
    }

    const std::array<SpotLightDesc, LightingSystem::kMaxSpotLights>& LightingSystem::GetSpotLights() const
    {
        return m_SpotLights;
    }

    std::size_t LightingSystem::GetSpotLightCount() const
    {
        return m_SpotLightCount;
    }

    const ImageBasedLightDesc& LightingSystem::GetImageBasedLight() const
    {
        return m_ImageBasedLight;
    }

    const PostProcessDesc& LightingSystem::GetPostProcess() const
    {
        return m_PostProcess;
    }

    std::size_t LightingSystem::GetDirectionalLightCount() const
    {
        return m_DirectionalLight.enabled ? 1U : 0U;
    }

    bool LightingSystem::BuildDescriptorSetDesc(DescriptorSetDesc& outDesc) const
    {
        if (!m_Initialized)
        {
            return false;
        }

        LightingUniformData uniformData {};
        uniformData.ambientColorIntensity[0] = m_AmbientColor[0];
        uniformData.ambientColorIntensity[1] = m_AmbientColor[1];
        uniformData.ambientColorIntensity[2] = m_AmbientColor[2];
        uniformData.ambientColorIntensity[3] = m_AmbientIntensity;

        uniformData.directionalDirectionIntensity[0] = m_DirectionalLight.direction[0];
        uniformData.directionalDirectionIntensity[1] = m_DirectionalLight.direction[1];
        uniformData.directionalDirectionIntensity[2] = m_DirectionalLight.direction[2];
        uniformData.directionalDirectionIntensity[3] = m_DirectionalLight.enabled ? m_DirectionalLight.intensity : 0.0f;

        uniformData.directionalColorEnabled[0] = m_DirectionalLight.color[0];
        uniformData.directionalColorEnabled[1] = m_DirectionalLight.color[1];
        uniformData.directionalColorEnabled[2] = m_DirectionalLight.color[2];
        uniformData.directionalColorEnabled[3] = m_DirectionalLight.enabled ? 1.0f : 0.0f;

        uniformData.cameraWorldPosition[0] = m_CameraWorldPosition[0];
        uniformData.cameraWorldPosition[1] = m_CameraWorldPosition[1];
        uniformData.cameraWorldPosition[2] = m_CameraWorldPosition[2];
        uniformData.cameraWorldPosition[3] = 0.0f;

        uniformData.iblDiffuseColorIntensity[0] = m_ImageBasedLight.diffuseColor[0];
        uniformData.iblDiffuseColorIntensity[1] = m_ImageBasedLight.diffuseColor[1];
        uniformData.iblDiffuseColorIntensity[2] = m_ImageBasedLight.diffuseColor[2];
        uniformData.iblDiffuseColorIntensity[3] =
            m_ImageBasedLight.enabled ? m_ImageBasedLight.diffuseIntensity : 0.0f;

        uniformData.iblSpecularColorIntensity[0] = m_ImageBasedLight.specularColor[0];
        uniformData.iblSpecularColorIntensity[1] = m_ImageBasedLight.specularColor[1];
        uniformData.iblSpecularColorIntensity[2] = m_ImageBasedLight.specularColor[2];
        uniformData.iblSpecularColorIntensity[3] =
            m_ImageBasedLight.enabled ? m_ImageBasedLight.specularIntensity : 0.0f;

        uniformData.iblParams[0] = m_ImageBasedLight.enabled ? 1.0f : 0.0f;
        uniformData.iblParams[1] = m_ImageBasedLight.ambientOcclusionStrength;
        uniformData.iblParams[2] = 0.0f;
        uniformData.iblParams[3] = 0.0f;

        uniformData.iblRotationAndFlags[0] = m_ImageBasedLight.rotationDegrees;
        uniformData.iblRotationAndFlags[1] = m_ImageBasedLight.hasEnvironmentTexture ? 1.0f : 0.0f;
        uniformData.iblRotationAndFlags[2] = 0.0f;
        uniformData.iblRotationAndFlags[3] = 0.0f;

        uniformData.iblLowerHemisphereColorFlag[0] = m_ImageBasedLight.lowerHemisphereColor[0];
        uniformData.iblLowerHemisphereColorFlag[1] = m_ImageBasedLight.lowerHemisphereColor[1];
        uniformData.iblLowerHemisphereColorFlag[2] = m_ImageBasedLight.lowerHemisphereColor[2];
        uniformData.iblLowerHemisphereColorFlag[3] =
            m_ImageBasedLight.lowerHemisphereIsSolidColor ? 1.0f : 0.0f;

        uniformData.iblExposureAndSun[0] = m_ImageBasedLight.exposureMultiplier;
        uniformData.iblExposureAndSun[1] = m_ImageBasedLight.skyboxExposureMultiplier;
        uniformData.iblExposureAndSun[2] = m_ImageBasedLight.sunSpecularMultiplier;
        uniformData.iblExposureAndSun[3] = 0.0f;

        uniformData.postProcessToneMap[0] = m_PostProcess.active ? 1.0f : 0.0f;
        uniformData.postProcessToneMap[1] = m_PostProcess.toneMappingEnabled ? 1.0f : 0.0f;
        uniformData.postProcessToneMap[2] = static_cast<float>(static_cast<int>(m_PostProcess.toneMappingOperator));
        uniformData.postProcessToneMap[3] = m_PostProcess.exposureCompensationEV;

        uniformData.postProcessColor[0] = m_PostProcess.colorFilter[0];
        uniformData.postProcessColor[1] = m_PostProcess.colorFilter[1];
        uniformData.postProcessColor[2] = m_PostProcess.colorFilter[2];
        uniformData.postProcessColor[3] = m_PostProcess.saturation;

        uniformData.postProcessCurve[0] = m_PostProcess.contrast;
        uniformData.postProcessCurve[1] = m_PostProcess.gamma;
        uniformData.postProcessCurve[2] = m_PostProcess.whitePoint;
        uniformData.postProcessCurve[3] = 0.0f;

        uniformData.postProcessBloom[0] = m_PostProcess.bloomEnabled ? 1.0f : 0.0f;
        uniformData.postProcessBloom[1] = m_PostProcess.bloomIntensity;
        uniformData.postProcessBloom[2] = m_PostProcess.bloomThreshold;
        uniformData.postProcessBloom[3] = m_PostProcess.bloomKnee;

        uniformData.postProcessColorBalance[0] = m_PostProcess.colorBalance[0];
        uniformData.postProcessColorBalance[1] = m_PostProcess.colorBalance[1];
        uniformData.postProcessColorBalance[2] = m_PostProcess.colorBalance[2];
        uniformData.postProcessColorBalance[3] = m_PostProcess.eyeAdaptationCompensationEV;

        uniformData.postProcessFilmCurve[0] = m_PostProcess.filmCurveShoulder;
        uniformData.postProcessFilmCurve[1] = m_PostProcess.filmCurveLinear;
        uniformData.postProcessFilmCurve[2] = m_PostProcess.filmCurveToe;
        uniformData.postProcessFilmCurve[3] = 0.0f;

        uniformData.localLightCounts[0] = static_cast<float>(m_PointLightCount);
        uniformData.localLightCounts[1] = static_cast<float>(m_SpotLightCount);
        uniformData.localLightCounts[2] = 0.0f;
        uniformData.localLightCounts[3] = 0.0f;

        for (std::size_t index = 0; index < kMaxPointLights; ++index)
        {
            const PointLightDesc& light = m_PointLights[index];
            uniformData.pointPositionRange[index][0] = light.position[0];
            uniformData.pointPositionRange[index][1] = light.position[1];
            uniformData.pointPositionRange[index][2] = light.position[2];
            uniformData.pointPositionRange[index][3] = light.enabled ? std::max(light.range, 0.001f) : 0.0f;

            uniformData.pointColorIntensity[index][0] =
                light.enabled ? light.color[0] * light.intensity : 0.0f;
            uniformData.pointColorIntensity[index][1] =
                light.enabled ? light.color[1] * light.intensity : 0.0f;
            uniformData.pointColorIntensity[index][2] =
                light.enabled ? light.color[2] * light.intensity : 0.0f;
            uniformData.pointColorIntensity[index][3] =
                light.enabled ? std::max(light.attenuation, 0.001f) : 0.0f;
        }

        constexpr float kPi = 3.14159265359f;
        for (std::size_t index = 0; index < kMaxSpotLights; ++index)
        {
            const SpotLightDesc& light = m_SpotLights[index];
            uniformData.spotPositionRange[index][0] = light.position[0];
            uniformData.spotPositionRange[index][1] = light.position[1];
            uniformData.spotPositionRange[index][2] = light.position[2];
            uniformData.spotPositionRange[index][3] = light.enabled ? std::max(light.range, 0.001f) : 0.0f;

            uniformData.spotDirectionInner[index][0] = light.direction[0];
            uniformData.spotDirectionInner[index][1] = light.direction[1];
            uniformData.spotDirectionInner[index][2] = light.direction[2];
            uniformData.spotDirectionInner[index][3] =
                light.enabled ? std::cos(light.innerConeAngleDegrees * (kPi / 180.0f)) : 1.0f;

            uniformData.spotColorOuter[index][0] =
                light.enabled ? light.color[0] * light.intensity : 0.0f;
            uniformData.spotColorOuter[index][1] =
                light.enabled ? light.color[1] * light.intensity : 0.0f;
            uniformData.spotColorOuter[index][2] =
                light.enabled ? light.color[2] * light.intensity : 0.0f;
            uniformData.spotColorOuter[index][3] =
                light.enabled ? std::cos(light.outerConeAngleDegrees * (kPi / 180.0f)) : 1.0f;
        }

        std::memcpy(
            uniformData.directionalShadowMatrix,
            m_DirectionalShadowMatrix.data(),
            sizeof(uniformData.directionalShadowMatrix));
        std::memcpy(
            uniformData.pointShadowMatrices,
            m_PointShadowMatrices.data(),
            sizeof(uniformData.pointShadowMatrices));
        std::memcpy(
            uniformData.spotShadowMatrix,
            m_SpotShadowMatrix.data(),
            sizeof(uniformData.spotShadowMatrix));

        uniformData.directionalShadowParams[0] = m_DirectionalShadowEnabled ? 1.0f : 0.0f;
        uniformData.directionalShadowParams[1] = m_DirectionalShadowBias;
        uniformData.directionalShadowParams[2] = m_DirectionalShadowTexelSize;
        uniformData.directionalShadowParams[3] = 0.0f;

        uniformData.pointShadowParams[0] = m_PointShadowEnabled ? 1.0f : 0.0f;
        uniformData.pointShadowParams[1] = m_PointShadowBias;
        uniformData.pointShadowParams[2] = m_PointShadowLightIndex;
        uniformData.pointShadowParams[3] = m_PointShadowSoftShadows ? 1.0f : 0.0f;

        uniformData.pointShadowLightPositionRange[0] = m_PointShadowLightPositionRange[0];
        uniformData.pointShadowLightPositionRange[1] = m_PointShadowLightPositionRange[1];
        uniformData.pointShadowLightPositionRange[2] = m_PointShadowLightPositionRange[2];
        uniformData.pointShadowLightPositionRange[3] = m_PointShadowLightPositionRange[3];

        uniformData.pointShadowAtlasInvSize[0] = m_PointShadowAtlasInvWidth;
        uniformData.pointShadowAtlasInvSize[1] = m_PointShadowAtlasInvHeight;
        uniformData.pointShadowAtlasInvSize[2] = 0.0f;
        uniformData.pointShadowAtlasInvSize[3] = 0.0f;

        uniformData.spotShadowParams[0] = m_SpotShadowEnabled ? 1.0f : 0.0f;
        uniformData.spotShadowParams[1] = m_SpotShadowBias;
        uniformData.spotShadowParams[2] = m_SpotShadowTexelSize;
        uniformData.spotShadowParams[3] = m_SpotShadowLightIndex;

        DescriptorBufferWrite write;
        write.binding = m_DescriptorBinding;
        write.data.resize(sizeof(LightingUniformData));
        std::memcpy(write.data.data(), &uniformData, sizeof(LightingUniformData));

        auto existing = std::find_if(
            outDesc.buffers.begin(),
            outDesc.buffers.end(),
            [this](const DescriptorBufferWrite& bufferWrite)
            {
                return bufferWrite.binding == m_DescriptorBinding;
            });

        if (existing != outDesc.buffers.end())
        {
            *existing = std::move(write);
        }
        else
        {
            outDesc.buffers.push_back(std::move(write));
        }

        return true;
    }

    std::array<float, 3> LightingSystem::NormalizeDirection(const std::array<float, 3>& direction)
    {
        const float lengthSq =
            direction[0] * direction[0] +
            direction[1] * direction[1] +
            direction[2] * direction[2];
        if (lengthSq <= 0.000001f)
        {
            return { 0.0f, -1.0f, 0.0f };
        }

        const float invLength = 1.0f / std::sqrt(lengthSq);
        return {
            direction[0] * invLength,
            direction[1] * invLength,
            direction[2] * invLength
        };
    }

    float LightingSystem::ClampToNonNegative(const float value)
    {
        return std::max(value, 0.0f);
    }
}
