#include "Luma/Editor/Rendering/SceneViewBuilder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "Luma/Scene/CameraComponent.h"
#include "Luma/Scene/DirectionalLightComponent.h"
#include "Luma/Scene/PointLightComponent.h"
#include "Luma/Scene/SkyLightComponent.h"
#include "Luma/Scene/SpotLightComponent.h"
#include "Luma/Scene/TransformComponent.h"

namespace Luma::Editor
{
    namespace
    {
        struct Vec3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct Mat4
        {
            std::array<float, 16> elements = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
        };

        Vec3 operator+(const Vec3& lhs, const Vec3& rhs)
        {
            return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
        }

        Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
        {
            return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
        }

        Vec3 operator*(const Vec3& value, const float scalar)
        {
            return { value.x * scalar, value.y * scalar, value.z * scalar };
        }

        float Dot(const Vec3& lhs, const Vec3& rhs)
        {
            return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
        }

        Vec3 Cross(const Vec3& lhs, const Vec3& rhs)
        {
            return {
                lhs.y * rhs.z - lhs.z * rhs.y,
                lhs.z * rhs.x - lhs.x * rhs.z,
                lhs.x * rhs.y - lhs.y * rhs.x
            };
        }

        Vec3 Normalize(const Vec3& value)
        {
            const float lengthSq = Dot(value, value);
            if (lengthSq <= 1.0e-8f)
            {
                return {};
            }

            const float invLength = 1.0f / std::sqrt(lengthSq);
            return value * invLength;
        }

        Mat4 Multiply(const Mat4& lhs, const Mat4& rhs)
        {
            Mat4 result {};
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    float value = 0.0f;
                    for (int k = 0; k < 4; ++k)
                    {
                        value += lhs.elements[k * 4 + row] * rhs.elements[column * 4 + k];
                    }
                    result.elements[column * 4 + row] = value;
                }
            }
            return result;
        }

        Mat4 BuildPerspective(const float fovRadians, const float aspectRatio, const float nearPlane, const float farPlane)
        {
            Mat4 result {};
            result.elements.fill(0.0f);

            const float tanHalfFov = std::tan(fovRadians * 0.5f);
            if (std::abs(tanHalfFov) <= 1.0e-6f || std::abs(aspectRatio) <= 1.0e-6f)
            {
                result.elements = {
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f
                };
                return result;
            }

            const float f = 1.0f / tanHalfFov;
            result.elements[0] = f / aspectRatio;
            result.elements[5] = f;
            result.elements[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
            result.elements[11] = -1.0f;
            result.elements[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
            return result;
        }

        Mat4 BuildPerspectiveZeroToOne(
            const float fovRadians,
            const float aspectRatio,
            const float nearPlane,
            const float farPlane)
        {
            Mat4 result {};
            result.elements.fill(0.0f);

            const float tanHalfFov = std::tan(fovRadians * 0.5f);
            if (std::abs(tanHalfFov) <= 1.0e-6f || std::abs(aspectRatio) <= 1.0e-6f)
            {
                result.elements = {
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f
                };
                return result;
            }

            const float f = 1.0f / tanHalfFov;
            result.elements[0] = f / aspectRatio;
            result.elements[5] = f;
            result.elements[10] = farPlane / (nearPlane - farPlane);
            result.elements[11] = -1.0f;
            result.elements[14] = (farPlane * nearPlane) / (nearPlane - farPlane);
            return result;
        }

        Mat4 BuildLookAt(const Vec3& eye, const Vec3& center, const Vec3& worldUp)
        {
            const Vec3 forward = Normalize(center - eye);
            const Vec3 right = Normalize(Cross(forward, worldUp));
            const Vec3 up = Cross(right, forward);

            Mat4 result {};
            result.elements = {
                right.x, up.x, -forward.x, 0.0f,
                right.y, up.y, -forward.y, 0.0f,
                right.z, up.z, -forward.z, 0.0f,
                -Dot(right, eye), -Dot(up, eye), Dot(forward, eye), 1.0f
            };
            return result;
        }

        Vec3 RotateByEulerDegrees(const Vec3& point, const std::array<float, 3>& degrees)
        {
            constexpr float kPi = 3.14159265359f;
            const float rx = degrees[0] * (kPi / 180.0f);
            const float ry = degrees[1] * (kPi / 180.0f);
            const float rz = degrees[2] * (kPi / 180.0f);

            const float cosX = std::cos(rx);
            const float sinX = std::sin(rx);
            const float cosY = std::cos(ry);
            const float sinY = std::sin(ry);
            const float cosZ = std::cos(rz);
            const float sinZ = std::sin(rz);

            Vec3 p = point;

            p = {
                p.x,
                p.y * cosX - p.z * sinX,
                p.y * sinX + p.z * cosX
            };

            p = {
                p.x * cosY + p.z * sinY,
                p.y,
                -p.x * sinY + p.z * cosY
            };

            p = {
                p.x * cosZ - p.y * sinZ,
                p.x * sinZ + p.y * cosZ,
                p.z
            };
            return p;
        }
    }

    EntityID FindEditorCameraEntity(const Scene& scene, const EntityID selectedEntity)
    {
        const auto& registry = scene.GetRegistry();
        auto isCameraEntity = [&registry](const EntityID entity)
        {
            return entity != entt::null &&
                registry.valid(entity) &&
                registry.all_of<TransformComponent, CameraComponent>(entity);
        };

        if (isCameraEntity(selectedEntity))
        {
            return selectedEntity;
        }

        const auto view = registry.view<TransformComponent, CameraComponent>();
        for (const EntityID entity : view)
        {
            const auto& camera = view.get<CameraComponent>(entity);
            if (camera.primary)
            {
                return entity;
            }
        }

        for (const EntityID entity : view)
        {
            return entity;
        }

        return entt::null;
    }

    EntityID FindPrimarySkyEntity(const Scene& scene)
    {
        const auto& registry = scene.GetRegistry();
        const auto view = registry.view<SkyLightComponent>();
        EntityID fallback = entt::null;
        int fallbackPriority = std::numeric_limits<int>::min();
        EntityID activeBest = entt::null;
        int activePriority = std::numeric_limits<int>::min();

        for (const EntityID entity : view)
        {
            const auto& skyLight = view.get<SkyLightComponent>(entity);

            if (fallback == entt::null || skyLight.priority > fallbackPriority)
            {
                fallback = entity;
                fallbackPriority = skyLight.priority;
            }

            if (skyLight.active && (activeBest == entt::null || skyLight.priority > activePriority))
            {
                activeBest = entity;
                activePriority = skyLight.priority;
            }
        }

        return activeBest != entt::null ? activeBest : fallback;
    }

    SceneViewBuildResult BuildSceneView(const SceneViewBuildInput& input)
    {
        SceneViewBuildResult result;
        if (input.scene == nullptr)
        {
            return result;
        }

        SceneView& sceneView = result.sceneView;
        sceneView.timeSeconds = input.timeSeconds;
        sceneView.outputWidth = std::max<std::uint32_t>(input.outputWidth, 1u);
        sceneView.outputHeight = std::max<std::uint32_t>(input.outputHeight, 1u);
        sceneView.skyMesh = input.skyMesh;
        sceneView.skyMeshRevision = input.skyMeshRevision;
        sceneView.hasSkyMesh = input.hasSkyMesh;
        sceneView.gridMesh = input.gridMesh;
        sceneView.gridMeshRevision = input.gridMeshRevision;
        sceneView.hasGridMesh = input.hasGridMesh;
        sceneView.renderItems = input.renderItems;
        sceneView.renderItemsRevision = input.renderItemsRevision;

        const auto& registry = input.scene->GetRegistry();
        const EntityID lensSourceEntity = input.previewSceneCameraLens
            ? FindEditorCameraEntity(*input.scene, input.selectedEntity)
            : entt::null;
        result.lensSourceEntity = lensSourceEntity;

        float nearPlane = 0.1f;
        float farPlane = 2000.0f;
        float fovDegrees = 60.0f;
        if (input.previewSceneCameraLens &&
            lensSourceEntity != entt::null &&
            registry.valid(lensSourceEntity) &&
            registry.all_of<TransformComponent, CameraComponent>(lensSourceEntity))
        {
            const auto& camera = registry.get<CameraComponent>(lensSourceEntity);
            fovDegrees = std::clamp(camera.fovDegrees, 10.0f, 170.0f);
        }

        constexpr float kPi = 3.14159265359f;
        const float aspectRatio = static_cast<float>(sceneView.outputWidth) /
            static_cast<float>(std::max(sceneView.outputHeight, 1u));
        const float yawRadians = input.editorCamera.yaw * (kPi / 180.0f);
        const float pitchRadians = input.editorCamera.pitch * (kPi / 180.0f);
        const Vec3 eye {
            input.editorCamera.position[0],
            input.editorCamera.position[1],
            input.editorCamera.position[2]
        };
        const Vec3 forward = Normalize({
            std::cos(yawRadians) * std::cos(pitchRadians),
            std::sin(pitchRadians),
            std::sin(yawRadians) * std::cos(pitchRadians)
        });
        const float fovRadians = fovDegrees * (kPi / 180.0f);
        const Mat4 view = BuildLookAt(eye, eye + forward, Vec3 { 0.0f, 1.0f, 0.0f });
        const Mat4 projection = BuildPerspective(fovRadians, aspectRatio, nearPlane, farPlane);
        const Mat4 viewProjection = Multiply(projection, view);
        sceneView.viewProjection = viewProjection.elements;
        sceneView.cameraWorldPosition = { eye.x, eye.y, eye.z };
        sceneView.ambientLightColor = { 0.09f, 0.11f, 0.14f };
        sceneView.ambientLightIntensity = 0.35f;
        sceneView.directionalLight.enabled = false;
        sceneView.directionalLight.direction = { -0.35f, -0.85f, -0.40f };
        sceneView.directionalLight.color = { 1.0f, 0.97f, 0.92f };
        sceneView.directionalLight.intensity = 0.0f;
        sceneView.directionalLight.castsShadows = false;
        sceneView.pointLights.fill(PointLightDesc {});
        sceneView.pointLightCount = 0;
        sceneView.spotLights.fill(SpotLightDesc {});
        sceneView.spotLightCount = 0;
        sceneView.imageBasedLight = SceneImageBasedLightView {};
        sceneView.postProcess = ScenePostProcessView {};

        float bestDirectionalIntensity = -1.0f;
        const auto directionalLights = registry.view<TransformComponent, DirectionalLightComponent>();
        for (const EntityID entity : directionalLights)
        {
            const auto& directionalLight = directionalLights.get<DirectionalLightComponent>(entity);
            if (!directionalLight.active)
            {
                continue;
            }

            const auto& lightTransform = directionalLights.get<TransformComponent>(entity);
            const Vec3 lightDirection = Normalize(
                RotateByEulerDegrees(
                    Vec3 { 0.0f, -1.0f, 0.0f },
                    lightTransform.worldRotation));
            if (directionalLight.intensity > bestDirectionalIntensity)
            {
                sceneView.directionalLight.enabled = true;
                sceneView.directionalLight.direction = { lightDirection.x, lightDirection.y, lightDirection.z };
                sceneView.directionalLight.color = directionalLight.color;
                sceneView.directionalLight.intensity = directionalLight.intensity;
                sceneView.directionalLight.castsShadows = directionalLight.castShadows;
                bestDirectionalIntensity = directionalLight.intensity;
            }
        }

        std::array<float, LightingSystem::kMaxPointLights> pointLightScores {};
        pointLightScores.fill(-1.0f);
        auto insertPointLight = [&](const PointLightDesc& light, const float score)
        {
            if (!(score > 0.0f))
            {
                return;
            }

            std::size_t targetIndex = LightingSystem::kMaxPointLights;
            float worstScore = score;
            for (std::size_t index = 0; index < LightingSystem::kMaxPointLights; ++index)
            {
                if (index >= sceneView.pointLightCount)
                {
                    targetIndex = index;
                    break;
                }
                if (pointLightScores[index] < worstScore)
                {
                    worstScore = pointLightScores[index];
                    targetIndex = index;
                }
            }

            if (targetIndex >= LightingSystem::kMaxPointLights)
            {
                return;
            }

            sceneView.pointLights[targetIndex] = light;
            pointLightScores[targetIndex] = score;
            sceneView.pointLightCount = std::min(sceneView.pointLightCount + 1, LightingSystem::kMaxPointLights);
        };

        const auto pointLights = registry.view<TransformComponent, PointLightComponent>();
        for (const EntityID entity : pointLights)
        {
            const auto& pointLight = pointLights.get<PointLightComponent>(entity);
            if (!pointLight.active || pointLight.intensity <= 0.0f || pointLight.range <= 0.05f)
            {
                continue;
            }

            const auto& lightTransform = pointLights.get<TransformComponent>(entity);
            const Vec3 lightPosition {
                lightTransform.worldPosition[0],
                lightTransform.worldPosition[1],
                lightTransform.worldPosition[2]
            };
            const Vec3 toLight = lightPosition - eye;
            const float distanceSq = Dot(toLight, toLight);
            const float range = std::max(pointLight.range, 0.05f);
            const float score = pointLight.intensity * range * range / (1.0f + distanceSq);

            PointLightDesc desc;
            desc.enabled = true;
            desc.position = { lightPosition.x, lightPosition.y, lightPosition.z };
            desc.color = pointLight.color;
            desc.intensity = pointLight.intensity;
            desc.range = range;
            insertPointLight(desc, score);
        }

        std::array<float, LightingSystem::kMaxSpotLights> spotLightScores {};
        spotLightScores.fill(-1.0f);
        auto insertSpotLight = [&](const SpotLightDesc& light, const float score)
        {
            if (!(score > 0.0f))
            {
                return;
            }

            std::size_t targetIndex = LightingSystem::kMaxSpotLights;
            float worstScore = score;
            for (std::size_t index = 0; index < LightingSystem::kMaxSpotLights; ++index)
            {
                if (index >= sceneView.spotLightCount)
                {
                    targetIndex = index;
                    break;
                }
                if (spotLightScores[index] < worstScore)
                {
                    worstScore = spotLightScores[index];
                    targetIndex = index;
                }
            }

            if (targetIndex >= LightingSystem::kMaxSpotLights)
            {
                return;
            }

            sceneView.spotLights[targetIndex] = light;
            spotLightScores[targetIndex] = score;
            sceneView.spotLightCount = std::min(sceneView.spotLightCount + 1, LightingSystem::kMaxSpotLights);
        };

        const auto spotLights = registry.view<TransformComponent, SpotLightComponent>();
        for (const EntityID entity : spotLights)
        {
            const auto& spotLight = spotLights.get<SpotLightComponent>(entity);
            if (!spotLight.active || spotLight.intensity <= 0.0f || spotLight.range <= 0.05f)
            {
                continue;
            }

            const auto& lightTransform = spotLights.get<TransformComponent>(entity);
            const Vec3 lightPosition {
                lightTransform.worldPosition[0],
                lightTransform.worldPosition[1],
                lightTransform.worldPosition[2]
            };
            const Vec3 lightDirection = Normalize(
                RotateByEulerDegrees(
                    Vec3 { 0.0f, 0.0f, -1.0f },
                    lightTransform.worldRotation));
            const Vec3 toLight = lightPosition - eye;
            const float distanceSq = Dot(toLight, toLight);
            const float range = std::max(spotLight.range, 0.05f);
            const float score = spotLight.intensity * range * range / (1.0f + distanceSq);

            SpotLightDesc desc;
            desc.enabled = true;
            desc.position = { lightPosition.x, lightPosition.y, lightPosition.z };
            desc.direction = { lightDirection.x, lightDirection.y, lightDirection.z };
            desc.color = spotLight.color;
            desc.intensity = spotLight.intensity;
            desc.range = range;
            desc.innerConeAngleDegrees = spotLight.innerConeAngle;
            desc.outerConeAngleDegrees = spotLight.outerConeAngle;
            desc.castsShadows = spotLight.castShadows;
            insertSpotLight(desc, score);
        }

        const EntityID skyEntity = FindPrimarySkyEntity(*input.scene);
        if (skyEntity != entt::null && registry.valid(skyEntity) && registry.all_of<SkyLightComponent>(skyEntity))
        {
            auto& skyLight = registry.get<SkyLightComponent>(skyEntity);
            if (skyLight.active)
            {
                const std::array<float, 3> capturedEnvironmentColor = input.skyAverageColor;
                const float capturedMagnitude =
                    capturedEnvironmentColor[0] + capturedEnvironmentColor[1] + capturedEnvironmentColor[2];
                std::array<float, 3> sourceEnvironmentColor =
                    capturedMagnitude > 1.0e-4f
                        ? capturedEnvironmentColor
                        : skyLight.skyColor;
                if (skyLight.skyLightType == SceneSkyLightType::Color)
                {
                    sourceEnvironmentColor = skyLight.skyColor;
                }

                const std::array<float, 3> tintedEnvironmentColor {
                    sourceEnvironmentColor[0] * std::max(0.0f, skyLight.color[0] * skyLight.colorIntensity),
                    sourceEnvironmentColor[1] * std::max(0.0f, skyLight.color[1] * skyLight.colorIntensity),
                    sourceEnvironmentColor[2] * std::max(0.0f, skyLight.color[2] * skyLight.colorIntensity)
                };
                const float blendFactor = std::clamp(skyLight.blendFactor, 0.0f, 1.0f);
                const std::array<float, 3> finalEnvironmentColor {
                    std::lerp(sourceEnvironmentColor[0], tintedEnvironmentColor[0], blendFactor),
                    std::lerp(sourceEnvironmentColor[1], tintedEnvironmentColor[1], blendFactor),
                    std::lerp(sourceEnvironmentColor[2], tintedEnvironmentColor[2], blendFactor)
                };

                sceneView.ambientLightColor = finalEnvironmentColor;
                sceneView.ambientLightIntensity =
                    std::max(0.04f, skyLight.intensity * std::lerp(0.08f, 0.18f, blendFactor));
                sceneView.imageBasedLight.enabled = true;
                sceneView.imageBasedLight.diffuseColor = finalEnvironmentColor;
                sceneView.imageBasedLight.diffuseIntensity =
                    std::max(0.0f, skyLight.intensity * skyLight.diffuseIntensity);
                sceneView.imageBasedLight.specularColor = finalEnvironmentColor;
                sceneView.imageBasedLight.specularIntensity =
                    std::max(0.0f, skyLight.intensity * skyLight.reflectionIntensity);
                sceneView.imageBasedLight.exposureMultiplier =
                    std::pow(2.0f, std::clamp(skyLight.exposureEV, -16.0f, 16.0f));
                sceneView.imageBasedLight.skyboxExposureMultiplier =
                    std::pow(2.0f, std::clamp(skyLight.skyboxExposureEV, -16.0f, 16.0f));
                sceneView.imageBasedLight.sunSpecularMultiplier =
                    std::max(0.0f, skyLight.sunSpecularMultiplier);
                sceneView.imageBasedLight.autoExposureEnabled = skyLight.autoExposureEnabled;
                sceneView.imageBasedLight.autoExposureMinEV = skyLight.autoExposureMinEV;
                sceneView.imageBasedLight.autoExposureMaxEV = skyLight.autoExposureMaxEV;
                sceneView.imageBasedLight.autoExposureSpeedUp = skyLight.autoExposureSpeedUp;
                sceneView.imageBasedLight.autoExposureSpeedDown = skyLight.autoExposureSpeedDown;
                sceneView.directionalLight.intensity *= std::max(0.0f, skyLight.sunIntensityMultiplier);

                if (input.resolveSkyAssetPath)
                {
                    if (skyLight.skyLightType == SceneSkyLightType::HDRI && !skyLight.environmentMap.empty())
                    {
                        sceneView.imageBasedLight.environmentTexture = input.resolveSkyAssetPath(skyLight.environmentMap);
                        sceneView.imageBasedLight.environmentRotationDegrees = skyLight.rotation;
                    }
                    if (!skyLight.irradianceMap.empty())
                    {
                        sceneView.imageBasedLight.irradianceTexture = input.resolveSkyAssetPath(skyLight.irradianceMap);
                    }
                    if (!skyLight.prefilteredReflectionMap.empty())
                    {
                        sceneView.imageBasedLight.prefilteredReflectionTexture =
                            input.resolveSkyAssetPath(skyLight.prefilteredReflectionMap);
                    }
                }

                const float lowerHemisphereMagnitude =
                    std::max(0.0f, skyLight.lowerHemisphereColor[0]) +
                    std::max(0.0f, skyLight.lowerHemisphereColor[1]) +
                    std::max(0.0f, skyLight.lowerHemisphereColor[2]);
                sceneView.imageBasedLight.lowerHemisphereIsSolidColor =
                    skyLight.lowerHemisphereIsBlack || lowerHemisphereMagnitude > 1.0e-4f;
                sceneView.imageBasedLight.lowerHemisphereColor =
                    skyLight.lowerHemisphereIsBlack
                        ? std::array<float, 3> { 0.0f, 0.0f, 0.0f }
                        : skyLight.lowerHemisphereColor;
                sceneView.imageBasedLight.forceRebuild = skyLight.rebuildIBLRequested;
                sceneView.imageBasedLight.ambientOcclusionStrength =
                    skyLight.affectAmbientOcclusion
                        ? std::clamp(skyLight.ambientOcclusionStrength, 0.0f, 1.0f)
                        : 0.0f;
            }
        }

        return result;
    }
}
