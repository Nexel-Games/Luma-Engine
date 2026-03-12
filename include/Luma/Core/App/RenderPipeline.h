#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Luma/Core/App/Project.h"
#include "Luma/Renderer/PostProcessSettings.h"
#include "Luma/RHI/GPUResourceManager.h"
#include "Luma/RHI/LightingSystem.h"

namespace Luma
{
    namespace Assets
    {
        class IResourceStreamingService;
    }

    class IRenderBackend;

    enum MaterialFeatureFlags : std::uint32_t
    {
        MaterialFeature_None = 0u,
        MaterialFeature_TwoSided = 1u << 0u,
        MaterialFeature_CastShadows = 1u << 1u,
        MaterialFeature_ReceiveShadows = 1u << 2u,
        MaterialFeature_ReceiveDecals = 1u << 3u,
        MaterialFeature_UseVertexColor = 1u << 4u,
        MaterialFeature_UseWorldPositionOffset = 1u << 5u,
        MaterialFeature_UseDitheredLodTransition = 1u << 6u
    };

    struct MaterialRenderProxy
    {
        std::filesystem::path sourcePath;
        std::string name;
        std::string shaderTemplate = "Builtin.Triangle";
        std::uint32_t domain = 0;
        std::uint32_t blendMode = 0;
        std::uint32_t shadingModel = 0;
        std::uint32_t featureFlags = MaterialFeature_None;
        std::array<float, 4> baseColor { 1.0f, 1.0f, 1.0f, 1.0f };
        std::array<float, 4> subsurfaceColor { 1.0f, 1.0f, 1.0f, 1.0f };
        std::array<float, 4> emissiveColor { 0.0f, 0.0f, 0.0f, 1.0f };
        float emissiveIntensity = 0.0f;
        float metallic = 0.0f;
        float roughness = 0.5f;
        float specular = 0.5f;
        float ambientOcclusion = 1.0f;
        float normalStrength = 1.0f;
        float clearCoat = 0.0f;
        float clearCoatRoughness = 0.1f;
        float opacity = 1.0f;
        float opacityMaskClipValue = 0.333f;
        float refraction = 1.0f;
        float displacementScale = 0.0f;
        std::array<float, 2> uvTiling { 1.0f, 1.0f };
        std::array<float, 2> uvOffset { 0.0f, 0.0f };
        float uvRotation = 0.0f;
        std::filesystem::path albedoTexture;
        std::filesystem::path normalTexture;
        std::filesystem::path ormTexture;
        std::filesystem::path metallicTexture;
        std::filesystem::path roughnessTexture;
        std::filesystem::path ambientOcclusionTexture;
        std::filesystem::path emissiveTexture;
        std::filesystem::path opacityTexture;
        std::filesystem::path heightTexture;
    };

    struct SceneRenderItem
    {
        std::string key;
        std::string meshKey;
        MeshDesc mesh;
        std::uint64_t revision = 0;
        std::uint64_t meshRevision = 0;
        std::array<float, 3> worldPosition { 0.0f, 0.0f, 0.0f };
        std::array<float, 16> worldTransform = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        MaterialRenderProxy material;
    };

    struct SceneImageBasedLightView
    {
        bool enabled = false;
        std::array<float, 3> diffuseColor { 0.0f, 0.0f, 0.0f };
        float diffuseIntensity = 0.0f;
        std::array<float, 3> specularColor { 0.0f, 0.0f, 0.0f };
        float specularIntensity = 0.0f;
        float ambientOcclusionStrength = 1.0f;
        std::filesystem::path environmentTexture;
        std::filesystem::path irradianceTexture;
        std::filesystem::path prefilteredReflectionTexture;
        float environmentRotationDegrees = 0.0f;
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
        bool forceRebuild = false;
    };

    struct ScenePostProcessView
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

    struct SceneView
    {
        float timeSeconds = 0.0f;
        std::uint32_t outputWidth = 1;
        std::uint32_t outputHeight = 1;
        std::array<float, 3> cameraWorldPosition { 0.0f, 0.0f, 5.0f };
        std::array<float, 4> clearColor { 0.05f, 0.07f, 0.12f, 1.0f };
        std::array<float, 3> ambientLightColor { 0.09f, 0.11f, 0.14f };
        float ambientLightIntensity = 1.0f;
        DirectionalLightDesc directionalLight {};
        std::array<PointLightDesc, LightingSystem::kMaxPointLights> pointLights {};
        std::size_t pointLightCount = 0;
        std::array<SpotLightDesc, LightingSystem::kMaxSpotLights> spotLights {};
        std::size_t spotLightCount = 0;
        SceneImageBasedLightView imageBasedLight {};
        ScenePostProcessView postProcess {};
        std::array<float, 16> viewProjection = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        const MeshDesc* skyMesh = nullptr;
        std::uint64_t skyMeshRevision = 0;
        bool hasSkyMesh = false;
        const MeshDesc* gridMesh = nullptr;
        std::uint64_t gridMeshRevision = 0;
        bool hasGridMesh = false;
        const MeshDesc* overrideMesh = nullptr;
        std::uint64_t overrideMeshRevision = 0;
        bool hasOverrideMesh = false;
        const std::vector<SceneRenderItem>* renderItems = nullptr;
        std::uint64_t renderItemsRevision = 0;
    };

    class IRenderPipeline
    {
    public:
        virtual ~IRenderPipeline() = default;

        virtual bool Init(IRenderBackend& renderer, GPUResourceManager& resourceManager) = 0;
        virtual void Shutdown(IRenderBackend& renderer, GPUResourceManager& resourceManager) = 0;
        virtual void RenderFrame(IRenderBackend& renderer, const SceneView& sceneView) = 0;
        virtual void OnResize(IRenderBackend& renderer, std::uint32_t width, std::uint32_t height) = 0;
        virtual void SetStreamingService(Assets::IResourceStreamingService* streamingService)
        {
            (void)streamingService;
        }
        virtual const char* GetDebugName() const = 0;
    };

    std::unique_ptr<IRenderPipeline> CreateRenderPipeline(RenderPipelineProfile profile);
}
