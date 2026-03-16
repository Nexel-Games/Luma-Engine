#include "Luma/Layers/TriangleLayer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "Luma/Scene/MaterialComponent.h"
#include "Luma/Scene/PointLightComponent.h"

namespace
{
    struct BakeVec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct BakeVec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    BakeVec3 operator+(const BakeVec3& lhs, const BakeVec3& rhs)
    {
        return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
    }

    BakeVec3 operator-(const BakeVec3& lhs, const BakeVec3& rhs)
    {
        return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
    }

    BakeVec3 operator*(const BakeVec3& value, const float scalar)
    {
        return { value.x * scalar, value.y * scalar, value.z * scalar };
    }

    BakeVec3 operator/(const BakeVec3& value, const float scalar)
    {
        return scalar == 0.0f ? value : BakeVec3 { value.x / scalar, value.y / scalar, value.z / scalar };
    }

    float Dot(const BakeVec3& lhs, const BakeVec3& rhs)
    {
        return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
    }

    BakeVec3 Cross(const BakeVec3& lhs, const BakeVec3& rhs)
    {
        return {
            lhs.y * rhs.z - lhs.z * rhs.y,
            lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x
        };
    }

    BakeVec3 Normalize(const BakeVec3& value)
    {
        const float lengthSq = Dot(value, value);
        if (lengthSq <= 1.0e-8f)
        {
            return {};
        }

        const float invLength = 1.0f / std::sqrt(lengthSq);
        return { value.x * invLength, value.y * invLength, value.z * invLength };
    }

    BakeVec3 RotateByEulerDegrees(const BakeVec3& point, const std::array<float, 3>& degrees)
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

        BakeVec3 p = point;
        p = { p.x, p.y * cosX - p.z * sinX, p.y * sinX + p.z * cosX };
        p = { p.x * cosY + p.z * sinY, p.y, -p.x * sinY + p.z * cosY };
        return {
            p.x * cosZ - p.y * sinZ,
            p.x * sinZ + p.y * cosZ,
            p.z
        };
    }

    BakeVec3 TransformPoint(
        const std::array<float, 3>& position,
        const std::array<float, 3>& rotation,
        const std::array<float, 3>& scale,
        const std::array<float, 3>& point)
    {
        const BakeVec3 scaled {
            point[0] * scale[0],
            point[1] * scale[1],
            point[2] * scale[2]
        };
        const BakeVec3 rotated = RotateByEulerDegrees(scaled, rotation);
        return {
            rotated.x + position[0],
            rotated.y + position[1],
            rotated.z + position[2]
        };
    }

    std::array<float, 3> KelvinToRgb(const float kelvin)
    {
        const float temperature = std::clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;
        float red = 255.0f;
        float green = 255.0f;
        float blue = 255.0f;

        if (temperature > 66.0f)
        {
            red = 329.698727446f * std::pow(temperature - 60.0f, -0.1332047592f);
            green = 288.1221695283f * std::pow(temperature - 60.0f, -0.0755148492f);
        }
        else
        {
            green = 99.4708025861f * std::log(std::max(temperature, 1.0f)) - 161.1195681661f;
        }

        if (temperature < 66.0f)
        {
            if (temperature <= 19.0f)
            {
                blue = 0.0f;
            }
            else
            {
                blue = 138.5177312231f * std::log(std::max(temperature - 10.0f, 1.0f)) - 305.0447927307f;
            }
        }

        auto normalize = [](const float value)
        {
            return std::clamp(value / 255.0f, 0.0f, 1.0f);
        };

        return { normalize(red), normalize(green), normalize(blue) };
    }

    float ComputeGodotOmniAttenuation(const float distance, const float range, const float attenuation)
    {
        const float invRange = 1.0f / std::max(range, 1.0e-4f);
        float nd = distance * invRange;
        nd *= nd;
        nd *= nd;
        nd = std::max(1.0f - nd, 0.0f);
        nd *= nd;
        return nd * std::pow(std::max(distance, 1.0e-4f), -std::max(attenuation, 1.0e-4f));
    }

    float EdgeFunction(const BakeVec2& a, const BakeVec2& b, const BakeVec2& c)
    {
        return (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x);
    }

    std::uint64_t HashBytes(const std::uint8_t* data, const std::size_t size)
    {
        constexpr std::uint64_t kOffset = 1469598103934665603ull;
        constexpr std::uint64_t kPrime = 1099511628211ull;
        std::uint64_t hash = kOffset;
        for (std::size_t index = 0; index < size; ++index)
        {
            hash ^= static_cast<std::uint64_t>(data[index]);
            hash *= kPrime;
        }
        return hash;
    }
}

namespace Luma
{
    Editor::MeshStreamingGeometryContext TriangleLayer::BuildMeshStreamingGeometryContext()
    {
        Editor::MeshStreamingGeometryContext context {};
        context.primitiveMeshLod = m_PrimitiveMeshLod;
        context.cameraPosition = m_ViewportController.Camera().position;
        context.projectLoaded = Project::IsLoaded();
        context.projectAssetsPath = Project::GetAssetsPath();
        context.projectRoot = Project::GetProjectRoot();
        context.streamingService = &m_ResourceStreamingService;
        context.importedSceneParts = &m_ImportedSceneParts;
        context.streamedMeshAssets = &m_StreamedMeshAssets;
        context.logImportError = [this](const std::string_view message)
        {
            AddConsoleLine(LogLevel::Error, "Import", message, message);
        };
        context.logStreamingError = [this](const std::string_view message)
        {
            AddConsoleLine(LogLevel::Error, "Streaming", message, message);
        };
        context.logStreamingWarn = [this](const std::string_view message)
        {
            AddConsoleLine(LogLevel::Warn, "Streaming", message, message);
        };
        return context;
    }

    Editor::RenderFrameCoordinatorContext TriangleLayer::BuildRenderFrameCoordinatorContext(IRenderBackend& renderer)
    {
        Editor::RenderFrameCoordinatorContext context {};
        context.renderer = &renderer;
        context.lastRenderer = &m_LastRenderer;
        context.gpuResourceManager = &m_GPUResourceManager;
        context.activePipeline = &m_ActivePipeline;
        context.activeProfile = &m_ActiveProfile;
        context.desiredProfile =
            Project::IsLoaded() ? Project::GetConfig().pipeline : RenderPipelineProfile::CoreLite;
        context.streamingService = &m_ResourceStreamingService;
        context.scene = &m_Scene;
        context.viewportController = &m_ViewportController;
        context.activeCameraEntity = IsPlayModeActive() ? EnsurePlayModeGameCameraEntity() : entt::null;
        context.selectedEntity = m_SelectedEntity;
        context.timeSeconds = m_Time;
        context.skyMesh = &m_SkyPrimitiveMeshDesc;
        context.skyMeshRevision = m_SkyPrimitiveMeshRevision;
        context.hasSkyMesh = m_HasSkyPrimitiveMesh;
        context.gridMesh = &m_ScenePrimitiveMeshDesc;
        context.gridMeshRevision = m_ScenePrimitiveMeshRevision;
        context.hasGridMesh = m_HasScenePrimitiveMesh;
        context.renderItems = &m_SceneRenderItems;
        context.renderItemsRevision = m_SceneRenderItemsRevision;
        context.skyAverageColor = m_SkyAverageColor;
        context.renderSceneCacheDirtyFlags = m_RenderSceneCacheDirtyFlags;
        context.lastViewportGridEnabled = m_LastViewportGridEnabled;
        context.hasScenePrimitiveMesh = m_HasScenePrimitiveMesh;
        context.hasSkyPrimitiveMesh = m_HasSkyPrimitiveMesh;
        context.lastSkyMeshSignature = &m_LastSkyMeshSignature;
        context.skyboxSourcePath = &m_SkyboxSourcePath;
        context.lastSceneRebuildMs = &m_LastSceneRebuildMs;
        context.lastSceneViewBuildMs = &m_LastSceneViewBuildMs;
        context.lastRenderFrameMs = &m_LastRenderFrameMs;
        context.onRendererChanged = [this]()
        {
            ReleaseGizmoToolbarIcons();
            m_ContentBrowserCache.Shutdown(m_LastRenderer);
        };
        context.syncSkyEnvironmentResources = [this]()
        {
            Editor::SkyPreviewTextureHostContext skyPreviewContext = BuildSkyPreviewTextureHostContext();
            m_SkyPreviewTextureHostService.Sync(skyPreviewContext);
        };
        context.findPrimarySkyEntity = [this]()
        {
            return FindPrimarySkyEntity();
        };
        context.buildSkyMeshSignature = [this](const EntityID)
        {
            Editor::SceneRenderCacheStateContext cacheStateContext = BuildSceneRenderCacheStateContext();
            return m_SceneRenderCacheStateService.BuildActiveSkyMeshSignature(cacheStateContext);
        };
        context.rebuildScenePrimitiveMesh = [this]()
        {
            RebuildScenePrimitiveMesh();
        };
        context.resolveSkyAssetPath = [this](const std::string& assetPath)
        {
            return ResolveSkyAssetPath(assetPath);
        };
        context.setLensSourceEntity = [this](const EntityID lensSourceEntity)
        {
            m_ViewportController.SetLensSourceEntity(lensSourceEntity);
        };
        context.clearSkyRebuildRequested = [this](const EntityID skyEntity)
        {
            auto& registry = m_Scene.GetRegistry();
            if (skyEntity == entt::null ||
                !registry.valid(skyEntity) ||
                !registry.all_of<SkyLightComponent>(skyEntity))
            {
                return;
            }

            auto& skyLight = registry.get<SkyLightComponent>(skyEntity);
            if (skyLight.active)
            {
                skyLight.rebuildIBLRequested = false;
            }
        };
        context.buildBlendedPostProcessView = [this](
            const std::array<float, 3>& cameraWorldPosition,
            ScenePostProcessView& outPostProcess)
        {
            BuildBlendedPostProcessView(cameraWorldPosition, outPostProcess);
        };
        return context;
    }

    void TriangleLayer::OnRender(IRenderBackend& renderer)
    {
        Editor::RenderFrameCoordinatorContext renderContext = BuildRenderFrameCoordinatorContext(renderer);
        m_RenderFrameCoordinatorService.Render(renderContext);
    }

    std::uint32_t TriangleLayer::ComputeRequestedMeshLod(
        const TransformComponent& transform,
        const MeshRendererComponent& meshRenderer) const
    {
        Editor::MeshStreamingGeometryContext context =
            const_cast<TriangleLayer*>(this)->BuildMeshStreamingGeometryContext();
        return m_MeshStreamingGeometryService.ComputeRequestedMeshLod(context, transform, meshRenderer);
    }

    const PrimitiveMeshData* TriangleLayer::ResolveMeshRendererGeometry(
        const TransformComponent& transform,
        const MeshRendererComponent& meshRenderer)
    {
        Editor::MeshStreamingGeometryContext context = BuildMeshStreamingGeometryContext();
        return m_MeshStreamingGeometryService.ResolveMeshRendererGeometry(context, transform, meshRenderer);
    }

    bool TriangleLayer::TryBuildBakedLightmap(
        const EntityID entity,
        const TransformComponent& transform,
        const MeshRendererComponent& meshRenderer,
        const PrimitiveMeshData& geometry,
        const MaterialRenderProxy& material,
        BakedLightmapData& outLightmap) const
    {
        if (!meshRenderer.staticLighting ||
            geometry.vertices.empty() ||
            geometry.indices.size() < 3 ||
            material.globalIlluminationMode == static_cast<std::uint32_t>(MaterialGlobalIlluminationMode::None))
        {
            return false;
        }

        struct BakedLight
        {
            BakeVec3 position {};
            std::array<float, 3> color { 1.0f, 1.0f, 1.0f };
            float range = 1.0f;
            float intensity = 1.0f;
            float attenuation = 1.0f;
            float bakeScale = 1.0f;
        };

        std::vector<BakedLight> bakedLights;
        const auto pointLightView = m_Scene.GetRegistry().view<TransformComponent, PointLightComponent>();
        bakedLights.reserve(pointLightView.size_hint());
        for (const EntityID lightEntity : pointLightView)
        {
            const auto& lightTransform = pointLightView.get<TransformComponent>(lightEntity);
            const auto& pointLight = pointLightView.get<PointLightComponent>(lightEntity);
            if (!pointLight.active)
            {
                continue;
            }

            float bakeScale = 0.0f;
            switch (pointLight.mode)
            {
            case PointLightMode::Baked:
                bakeScale = 1.0f;
                break;
            case PointLightMode::Mixed:
                bakeScale = std::max(pointLight.indirectMultiplier, 0.0f) * 0.2f;
                break;
            case PointLightMode::Realtime:
            default:
                break;
            }

            if (bakeScale <= 0.0f)
            {
                continue;
            }

            const std::array<float, 3> temperatureColor = KelvinToRgb(pointLight.temperature);
            bakedLights.push_back({
                { lightTransform.worldPosition[0], lightTransform.worldPosition[1], lightTransform.worldPosition[2] },
                {
                    pointLight.color[0] * temperatureColor[0],
                    pointLight.color[1] * temperatureColor[1],
                    pointLight.color[2] * temperatureColor[2]
                },
                std::max(pointLight.range, 0.05f),
                std::max(pointLight.intensity, 0.0f),
                std::max(pointLight.attenuation, 0.001f),
                bakeScale
            });
        }

        if (bakedLights.empty())
        {
            return false;
        }

        constexpr std::uint32_t kLightmapResolution = 64u;
        const std::size_t pixelCount =
            static_cast<std::size_t>(kLightmapResolution) * static_cast<std::size_t>(kLightmapResolution);
        std::vector<float> accum(pixelCount * 3ull, 0.0f);
        std::vector<std::uint8_t> coverage(pixelCount, 0u);

        for (std::size_t triangle = 0; triangle + 2 < geometry.indices.size(); triangle += 3)
        {
            const PrimitiveVertex& vertex0 = geometry.vertices[geometry.indices[triangle + 0]];
            const PrimitiveVertex& vertex1 = geometry.vertices[geometry.indices[triangle + 1]];
            const PrimitiveVertex& vertex2 = geometry.vertices[geometry.indices[triangle + 2]];

            const BakeVec3 world0 =
                TransformPoint(transform.worldPosition, transform.worldRotation, transform.worldScale, vertex0.position);
            const BakeVec3 world1 =
                TransformPoint(transform.worldPosition, transform.worldRotation, transform.worldScale, vertex1.position);
            const BakeVec3 world2 =
                TransformPoint(transform.worldPosition, transform.worldRotation, transform.worldScale, vertex2.position);
            const BakeVec3 faceNormal = Normalize(Cross(world1 - world0, world2 - world0));
            if (Dot(faceNormal, faceNormal) <= 1.0e-8f)
            {
                continue;
            }

            const BakeVec2 uv0 { vertex0.uv[0], vertex0.uv[1] };
            const BakeVec2 uv1 { vertex1.uv[0], vertex1.uv[1] };
            const BakeVec2 uv2 { vertex2.uv[0], vertex2.uv[1] };
            const float area = EdgeFunction(uv0, uv1, uv2);
            if (std::abs(area) <= 1.0e-8f)
            {
                continue;
            }

            const float minU = std::clamp(std::min({ uv0.x, uv1.x, uv2.x }), 0.0f, 1.0f);
            const float maxU = std::clamp(std::max({ uv0.x, uv1.x, uv2.x }), 0.0f, 1.0f);
            const float minV = std::clamp(std::min({ uv0.y, uv1.y, uv2.y }), 0.0f, 1.0f);
            const float maxV = std::clamp(std::max({ uv0.y, uv1.y, uv2.y }), 0.0f, 1.0f);

            const std::uint32_t startX = std::min<std::uint32_t>(
                kLightmapResolution - 1u,
                static_cast<std::uint32_t>(std::floor(minU * static_cast<float>(kLightmapResolution))));
            const std::uint32_t endX = std::min<std::uint32_t>(
                kLightmapResolution - 1u,
                static_cast<std::uint32_t>(std::ceil(maxU * static_cast<float>(kLightmapResolution))));
            const std::uint32_t startY = std::min<std::uint32_t>(
                kLightmapResolution - 1u,
                static_cast<std::uint32_t>(std::floor(minV * static_cast<float>(kLightmapResolution))));
            const std::uint32_t endY = std::min<std::uint32_t>(
                kLightmapResolution - 1u,
                static_cast<std::uint32_t>(std::ceil(maxV * static_cast<float>(kLightmapResolution))));

            for (std::uint32_t y = startY; y <= endY; ++y)
            {
                for (std::uint32_t x = startX; x <= endX; ++x)
                {
                    const BakeVec2 sampleUv {
                        (static_cast<float>(x) + 0.5f) / static_cast<float>(kLightmapResolution),
                        (static_cast<float>(y) + 0.5f) / static_cast<float>(kLightmapResolution)
                    };
                    const float w0 = EdgeFunction(uv1, uv2, sampleUv);
                    const float w1 = EdgeFunction(uv2, uv0, sampleUv);
                    const float w2 = EdgeFunction(uv0, uv1, sampleUv);
                    const bool inside =
                        (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) || (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
                    if (!inside)
                    {
                        continue;
                    }

                    const float invArea = 1.0f / area;
                    const float bary0 = w0 * invArea;
                    const float bary1 = w1 * invArea;
                    const float bary2 = w2 * invArea;
                    const BakeVec3 worldSample = world0 * bary0 + world1 * bary1 + world2 * bary2;

                    BakeVec3 bakedLighting {};
                    for (const BakedLight& light : bakedLights)
                    {
                        const BakeVec3 toLight = light.position - worldSample;
                        const float distanceSq = Dot(toLight, toLight);
                        if (distanceSq <= 1.0e-8f)
                        {
                            continue;
                        }

                        const float distance = std::sqrt(distanceSq);
                        if (distance >= light.range)
                        {
                            continue;
                        }

                        const BakeVec3 lightDir = toLight / distance;
                        const float nDotL = std::max(Dot(faceNormal, lightDir), 0.0f);
                        if (nDotL <= 0.0f)
                        {
                            continue;
                        }

                        const float attenuation = ComputeGodotOmniAttenuation(distance, light.range, light.attenuation);
                        const float intensity = attenuation * light.intensity * light.bakeScale * nDotL;
                        bakedLighting.x += light.color[0] * intensity;
                        bakedLighting.y += light.color[1] * intensity;
                        bakedLighting.z += light.color[2] * intensity;
                    }

                    const std::size_t pixelIndex =
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(kLightmapResolution) +
                        static_cast<std::size_t>(x);
                    accum[pixelIndex * 3ull + 0ull] += bakedLighting.x;
                    accum[pixelIndex * 3ull + 1ull] += bakedLighting.y;
                    accum[pixelIndex * 3ull + 2ull] += bakedLighting.z;
                    coverage[pixelIndex] = 255u;
                }
            }
        }

        bool hasBakedPixels = false;
        outLightmap.width = kLightmapResolution;
        outLightmap.height = kLightmapResolution;
        outLightmap.pixels.assign(pixelCount * 4ull, 0u);
        for (std::size_t pixelIndex = 0; pixelIndex < pixelCount; ++pixelIndex)
        {
            if (coverage[pixelIndex] == 0u)
            {
                continue;
            }

            const float r = accum[pixelIndex * 3ull + 0ull];
            const float g = accum[pixelIndex * 3ull + 1ull];
            const float b = accum[pixelIndex * 3ull + 2ull];
            const float mappedR = 1.0f - std::exp(-std::max(r, 0.0f));
            const float mappedG = 1.0f - std::exp(-std::max(g, 0.0f));
            const float mappedB = 1.0f - std::exp(-std::max(b, 0.0f));
            outLightmap.pixels[pixelIndex * 4ull + 0ull] =
                static_cast<std::uint8_t>(std::clamp(mappedR, 0.0f, 1.0f) * 255.0f);
            outLightmap.pixels[pixelIndex * 4ull + 1ull] =
                static_cast<std::uint8_t>(std::clamp(mappedG, 0.0f, 1.0f) * 255.0f);
            outLightmap.pixels[pixelIndex * 4ull + 2ull] =
                static_cast<std::uint8_t>(std::clamp(mappedB, 0.0f, 1.0f) * 255.0f);
            outLightmap.pixels[pixelIndex * 4ull + 3ull] = 255u;
            hasBakedPixels |=
                outLightmap.pixels[pixelIndex * 4ull + 0ull] > 0u ||
                outLightmap.pixels[pixelIndex * 4ull + 1ull] > 0u ||
                outLightmap.pixels[pixelIndex * 4ull + 2ull] > 0u;
        }

        if (!hasBakedPixels)
        {
            outLightmap = {};
            return false;
        }

        outLightmap.revision = HashBytes(outLightmap.pixels.data(), outLightmap.pixels.size());
        return true;
    }

    void TriangleLayer::HandleStreamingEvent(const Assets::StreamEvent& event)
    {
        const bool affectsSceneRenderCache =
            m_MeshStreamingGeometryService.InvalidateFromStreamingEvent(event, m_StreamedMeshAssets);

        switch (event.type)
        {
        case Assets::StreamEventType::Queued:
        case Assets::StreamEventType::Started:
        case Assets::StreamEventType::Retargeted:
            if (event.handle != 0)
            {
                m_StreamingActiveHandles.insert(event.handle);
                m_StreamingTaskBatchSize = std::max<std::uint32_t>(
                    m_StreamingTaskBatchSize,
                    static_cast<std::uint32_t>(m_StreamingActiveHandles.size()));
            }
            break;
        case Assets::StreamEventType::Completed:
        case Assets::StreamEventType::Failed:
        case Assets::StreamEventType::Cancelled:
        case Assets::StreamEventType::Released:
            if (event.handle != 0)
            {
                m_StreamingActiveHandles.erase(event.handle);
            }
            break;
        case Assets::StreamEventType::Evicted:
        case Assets::StreamEventType::BudgetUpdated:
            break;
        }

        if (!event.message.empty())
        {
            m_StreamingTaskLastMessage = event.message;
        }

        if (event.type == Assets::StreamEventType::Failed)
        {
            AddConsoleLine(LogLevel::Error, "Streaming", event.message, event.message);
        }
        else if (event.type == Assets::StreamEventType::Evicted)
        {
            AddConsoleLine(LogLevel::Warn, "Streaming", event.message, event.message);
        }

        if (affectsSceneRenderCache)
        {
            MarkSceneRenderCacheDirty(Editor::SceneRenderCacheDirtyFlags::Geometry);
        }
    }

    std::filesystem::path TriangleLayer::ResolveSkyAssetPath(const std::string& path) const
    {
        if (path.empty())
        {
            return {};
        }

        std::filesystem::path inputPath(path);
        std::error_code ec;
        if (inputPath.is_absolute())
        {
            const std::filesystem::path absolute = std::filesystem::weakly_canonical(inputPath, ec);
            return ec ? inputPath.lexically_normal() : absolute;
        }

        for (const Editor::ContentBrowserRootState& root : m_ContentRoots)
        {
            if (root.path.empty())
            {
                continue;
            }

            const std::filesystem::path candidate = root.path / inputPath;
            if (std::filesystem::exists(candidate, ec) && !ec)
            {
                const std::filesystem::path resolved = std::filesystem::weakly_canonical(candidate, ec);
                return ec ? candidate.lexically_normal() : resolved;
            }
        }

        return m_SkyEnvironmentService.ResolveAssetPath(
            path,
            Project::IsLoaded(),
            Project::GetProjectRoot(),
            Project::GetAssetsPath());
    }

    Editor::SceneRenderCacheStateContext TriangleLayer::BuildSceneRenderCacheStateContext() const
    {
        Editor::SceneRenderCacheStateContext context {};
        context.scene = &m_Scene;
        context.viewportGridEnabled = m_ViewportController.ShowGrid();
        context.skyboxSourcePath = &m_SkyboxSourcePath;
        context.renderSceneCacheDirtyFlags =
            const_cast<Editor::SceneRenderCacheDirtyFlags*>(&m_RenderSceneCacheDirtyFlags);
        context.lastViewportGridEnabled = const_cast<bool*>(&m_LastViewportGridEnabled);
        context.lastSkyMeshSignature = const_cast<std::string*>(&m_LastSkyMeshSignature);
        context.findPrimarySkyEntity = [this]()
        {
            return FindPrimarySkyEntity();
        };
        return context;
    }
}
