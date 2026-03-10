#include "Luma/Editor/Assets/MeshStreamingGeometryService.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

namespace
{
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
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

    float Length(const Vec3& value)
    {
        return std::sqrt(Dot(value, value));
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

        const Vec3 rotatedX {
            p.x,
            p.y * cosX - p.z * sinX,
            p.y * sinX + p.z * cosX
        };
        p = rotatedX;

        const Vec3 rotatedY {
            p.x * cosY + p.z * sinY,
            p.y,
            -p.x * sinY + p.z * cosY
        };
        p = rotatedY;

        const Vec3 rotatedZ {
            p.x * cosZ - p.y * sinZ,
            p.x * sinZ + p.y * cosZ,
            p.z
        };
        return rotatedZ;
    }

    std::string ToLowerString(std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](const unsigned char ch)
            {
                return static_cast<char>(std::tolower(ch));
            });
        return value;
    }

    bool IsRawSceneMeshPath(const std::filesystem::path& path)
    {
        const std::string extension = ToLowerString(path.extension().string());
        return extension == ".gltf" || extension == ".glb" || extension == ".fbx" || extension == ".obj";
    }

    std::filesystem::path ResolveRawSceneSourcePath(
        const Luma::Editor::MeshStreamingGeometryContext& context,
        const std::string& rawSource)
    {
        const std::filesystem::path sourcePath(rawSource);
        std::error_code ec;
        if (sourcePath.is_absolute() && std::filesystem::exists(sourcePath, ec))
        {
            const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(sourcePath, ec);
            return ec ? sourcePath.lexically_normal() : canonicalPath.lexically_normal();
        }

        if (context.projectLoaded)
        {
            const std::array<std::filesystem::path, 2> bases {
                context.projectAssetsPath,
                context.projectRoot
            };
            for (const std::filesystem::path& basePath : bases)
            {
                if (basePath.empty())
                {
                    continue;
                }

                const std::filesystem::path candidate = (basePath / sourcePath).lexically_normal();
                if (std::filesystem::exists(candidate, ec))
                {
                    const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(candidate, ec);
                    return ec ? candidate.lexically_normal() : canonicalPath.lexically_normal();
                }
            }
        }

        return {};
    }

    void LogMessage(
        const std::function<void(std::string_view)>& callback,
        std::string_view message)
    {
        if (callback && !message.empty())
        {
            callback(message);
        }
    }
}

namespace Luma::Editor
{
    std::uint32_t MeshStreamingGeometryService::ComputeRequestedMeshLod(
        const MeshStreamingGeometryContext& context,
        const TransformComponent& transform,
        const MeshRendererComponent& meshRenderer) const
    {
        if (meshRenderer.usePrimitive || !meshRenderer.autoStreamLod)
        {
            return meshRenderer.meshLod;
        }

        const float nearDistance = std::max(0.0f, meshRenderer.lodNearDistance);
        const float farDistance = std::max(nearDistance + 1.0f, meshRenderer.lodFarDistance);
        const std::uint32_t maxAutoLod = std::max<std::uint32_t>(meshRenderer.maxAutoLod, 1u);
        const Vec3 cameraPosition {
            context.cameraPosition[0],
            context.cameraPosition[1],
            context.cameraPosition[2]
        };
        const Vec3 entityPosition {
            transform.worldPosition[0],
            transform.worldPosition[1],
            transform.worldPosition[2]
        };
        const float distance = Length(entityPosition - cameraPosition);
        if (distance <= nearDistance)
        {
            return 0;
        }
        if (distance >= farDistance)
        {
            return maxAutoLod;
        }

        const float t = std::clamp((distance - nearDistance) / (farDistance - nearDistance), 0.0f, 1.0f);
        return static_cast<std::uint32_t>(std::round(t * static_cast<float>(maxAutoLod)));
    }

    MeshStreamingImportedScenePartsState* MeshStreamingGeometryService::ResolveImportedSceneParts(
        const MeshStreamingGeometryContext& context,
        const std::string& sourcePath) const
    {
        if (sourcePath.empty() || context.importedSceneParts == nullptr)
        {
            return nullptr;
        }

        const std::filesystem::path resolvedPath = ResolveRawSceneSourcePath(context, sourcePath);
        if (resolvedPath.empty() || !IsRawSceneMeshPath(resolvedPath))
        {
            return nullptr;
        }

        const std::string cacheKey = resolvedPath.generic_string();
        MeshStreamingImportedScenePartsState& partsState = (*context.importedSceneParts)[cacheKey];
        if (!partsState.loadAttempted || partsState.resolvedPath != resolvedPath)
        {
            partsState = {};
            partsState.resolvedPath = resolvedPath;
            partsState.loadAttempted = true;
            std::string loadError;
            if (!Assets::LoadMeshSceneParts(resolvedPath, partsState.parts, loadError))
            {
                partsState.loadFailed = true;
                LogMessage(context.logImportError, loadError);
            }
        }

        return partsState.loadFailed ? nullptr : &partsState;
    }

    const PrimitiveMeshData* MeshStreamingGeometryService::ResolveMeshRendererGeometry(
        const MeshStreamingGeometryContext& context,
        const TransformComponent& transform,
        const MeshRendererComponent& meshRenderer) const
    {
        if (meshRenderer.usePrimitive)
        {
            return &PrimitiveMeshFactory::GetPrimitive(meshRenderer.primitive, context.primitiveMeshLod);
        }

        if (meshRenderer.meshSource.empty())
        {
            return nullptr;
        }

        const std::filesystem::path resolvedPath = Assets::ResolveMeshAssetPath(meshRenderer.meshSource);
        if (resolvedPath.empty())
        {
            return nullptr;
        }

        if (meshRenderer.meshPartIndex != std::numeric_limits<std::uint32_t>::max() &&
            IsRawSceneMeshPath(resolvedPath))
        {
            const std::string cacheKey = resolvedPath.generic_string();
            if (context.importedSceneParts == nullptr)
            {
                return nullptr;
            }

            MeshStreamingImportedScenePartsState& partsState = (*context.importedSceneParts)[cacheKey];
            if (!partsState.loadAttempted || partsState.resolvedPath != resolvedPath)
            {
                partsState = {};
                partsState.resolvedPath = resolvedPath;
                partsState.loadAttempted = true;
                std::string loadError;
                if (!Assets::LoadMeshSceneParts(resolvedPath, partsState.parts, loadError))
                {
                    partsState.loadFailed = true;
                    LogMessage(context.logImportError, loadError);
                }
            }

            if (partsState.loadFailed || meshRenderer.meshPartIndex >= partsState.parts.size())
            {
                return nullptr;
            }

            return &partsState.parts[meshRenderer.meshPartIndex].mesh;
        }

        if (context.streamingService == nullptr || context.streamedMeshAssets == nullptr)
        {
            return nullptr;
        }

        const std::uint32_t targetLod = ComputeRequestedMeshLod(context, transform, meshRenderer);
        const std::string cacheKey = resolvedPath.generic_string() + "|lod:" + std::to_string(targetLod);
        MeshStreamingAssetState& state = (*context.streamedMeshAssets)[cacheKey];
        state.requestPath = std::filesystem::path(meshRenderer.meshSource);
        state.resolvedPath = resolvedPath;

        const auto releaseSectionStreams = [&](const bool clearManifest)
        {
            for (auto& sectionStream : state.sectionStreams)
            {
                if (sectionStream.streamHandle != 0)
                {
                    context.streamingService->Release(sectionStream.streamHandle);
                    sectionStream.streamHandle = 0;
                }
                sectionStream.mesh = {};
                sectionStream.residentCpuBytes = 0;
                sectionStream.decodeFailed = false;
            }
            state.sectionStreams.clear();
            state.activeSections.clear();
            if (clearManifest)
            {
                state.assetData = {};
            }
        };

        if (state.streamHandle == 0)
        {
            Assets::StreamRequestDesc request {};
            request.key = "mesh:" + cacheKey;
            request.sourcePath = resolvedPath;
            request.resourceType = Assets::StreamResourceType::Mesh;
            request.priority = Assets::StreamPriority::High;
            request.lod.mode = targetLod > 0 ? Assets::LODStreamingMode::Explicit : Assets::LODStreamingMode::Disabled;
            request.lod.targetLod = targetLod;
            request.evictable = false;
            request.estimatedCpuBytes = 1024ull * 1024ull;
            request.estimatedGpuBytes = 1024ull * 1024ull;

            std::string error;
            state.streamHandle = context.streamingService->Request(request, error);
            if (state.streamHandle == 0)
            {
                LogMessage(context.logStreamingError, error);
                state.decodeFailed = true;
                return nullptr;
            }
        }

        Assets::StreamRecord record {};
        if (!context.streamingService->TryGetRecord(state.streamHandle, record))
        {
            releaseSectionStreams(true);
            state.streamHandle = 0;
            state.mesh = {};
            state.decodeFailed = false;
            state.resolvedLod = std::numeric_limits<std::uint32_t>::max();
            state.residentCpuBytes = 0;
            return nullptr;
        }

        if (record.state != Assets::StreamState::Resident)
        {
            return state.mesh.vertices.empty() || state.mesh.indices.empty() ? nullptr : &state.mesh;
        }

        if (state.decodeFailed &&
            state.resolvedLod == record.resolvedLod &&
            state.residentCpuBytes == record.residentCpuBytes)
        {
            return nullptr;
        }

        Assets::StreamPayload payload {};
        if (!context.streamingService->TryGetPayload(state.streamHandle, payload))
        {
            return state.mesh.vertices.empty() || state.mesh.indices.empty() ? nullptr : &state.mesh;
        }

        Assets::MeshAssetData meshAsset;
        std::string error;
        const std::filesystem::path logicalPath =
            payload.resolvedSourcePath.empty() ? resolvedPath : payload.resolvedSourcePath;
        const auto loadFullCookedMeshFallback = [&](const std::string& fallbackReason) -> const PrimitiveMeshData*
        {
            Assets::MeshAssetData fullMeshAsset;
            std::string fullMeshError;
            if (!Assets::LoadMeshAssetData(logicalPath, fullMeshAsset, fullMeshError))
            {
                releaseSectionStreams(true);
                state.mesh = {};
                state.decodeFailed = true;
                state.resolvedLod = record.resolvedLod;
                state.residentCpuBytes = record.residentCpuBytes;
                LogMessage(context.logStreamingError, !fullMeshError.empty() ? fullMeshError : fallbackReason);
                return nullptr;
            }

            releaseSectionStreams(true);
            state.assetData = std::move(fullMeshAsset);
            state.mesh = state.assetData.mesh;
            state.activeSections.clear();
            state.decodeFailed = false;
            state.sectionStreamingDisabled = true;
            state.resolvedLod = record.resolvedLod;
            state.residentCpuBytes = record.residentCpuBytes;
            state.resolvedPath = logicalPath;
            LogMessage(context.logStreamingWarn, fallbackReason);
            return state.mesh.vertices.empty() || state.mesh.indices.empty() ? nullptr : &state.mesh;
        };
        const std::string logicalExtension = ToLowerString(logicalPath.extension().string());
        if (logicalExtension != ".lumamesh")
        {
            if (!Assets::LoadMeshAssetDataFromBytes(payload.bytes.data(), payload.bytes.size(), logicalPath, meshAsset, error))
            {
                releaseSectionStreams(true);
                state.mesh = {};
                state.decodeFailed = true;
                state.resolvedLod = record.resolvedLod;
                state.residentCpuBytes = record.residentCpuBytes;
                LogMessage(context.logStreamingError, error);
                return nullptr;
            }

            releaseSectionStreams(true);
            state.assetData = std::move(meshAsset);
            state.mesh = state.assetData.mesh;
            state.activeSections.clear();
            state.decodeFailed = false;
            state.sectionStreamingDisabled = false;
            state.resolvedLod = record.resolvedLod;
            state.residentCpuBytes = record.residentCpuBytes;
            state.resolvedPath = logicalPath;
            return state.mesh.vertices.empty() || state.mesh.indices.empty() ? nullptr : &state.mesh;
        }

        if (!Assets::LoadMeshAssetManifestFromBytes(payload.bytes.data(), payload.bytes.size(), logicalPath, meshAsset, error))
        {
            std::string legacyError;
            if (!Assets::LoadMeshAssetDataFromBytes(
                    payload.bytes.data(),
                    payload.bytes.size(),
                    logicalPath,
                    meshAsset,
                    legacyError))
            {
                releaseSectionStreams(true);
                state.mesh = {};
                state.decodeFailed = true;
                state.resolvedLod = record.resolvedLod;
                state.residentCpuBytes = record.residentCpuBytes;
                LogMessage(context.logStreamingError, !legacyError.empty() ? legacyError : error);
                return nullptr;
            }

            releaseSectionStreams(true);
            state.assetData = std::move(meshAsset);
            state.mesh = state.assetData.mesh;
            state.activeSections.clear();
            state.decodeFailed = false;
            state.sectionStreamingDisabled = false;
            state.resolvedLod = record.resolvedLod;
            state.residentCpuBytes = record.residentCpuBytes;
            state.resolvedPath = logicalPath;
            return state.mesh.vertices.empty() || state.mesh.indices.empty() ? nullptr : &state.mesh;
        }

        if (state.resolvedLod != record.resolvedLod || state.residentCpuBytes != record.residentCpuBytes)
        {
            releaseSectionStreams(false);
            state.assetData = std::move(meshAsset);
            state.sectionStreams.resize(state.assetData.sections.size());
            state.sectionStreamingDisabled = false;
        }

        if (state.assetData.sections.empty() || state.sectionStreamingDisabled)
        {
            state.mesh = state.assetData.mesh;
        }
        else
        {
            std::vector<std::uint32_t> requestedSections;
            const Vec3 cameraPosition {
                context.cameraPosition[0],
                context.cameraPosition[1],
                context.cameraPosition[2]
            };

            if (meshRenderer.streamSectionsByDistance)
            {
                const float sectionLoadDistance = std::max(
                    meshRenderer.sectionLoadDistance,
                    meshRenderer.lodFarDistance + 24.0f * static_cast<float>(targetLod + 1));
                float nearestDistance = std::numeric_limits<float>::max();
                std::uint32_t nearestSection = 0;
                for (std::uint32_t sectionIndex = 0;
                     sectionIndex < static_cast<std::uint32_t>(state.assetData.sections.size());
                     ++sectionIndex)
                {
                    const Assets::MeshAssetSection& section = state.assetData.sections[sectionIndex];
                    const Vec3 localCenter {
                        (section.boundsMin[0] + section.boundsMax[0]) * 0.5f,
                        (section.boundsMin[1] + section.boundsMax[1]) * 0.5f,
                        (section.boundsMin[2] + section.boundsMax[2]) * 0.5f
                    };
                    const Vec3 scaledCenter {
                        localCenter.x * transform.worldScale[0],
                        localCenter.y * transform.worldScale[1],
                        localCenter.z * transform.worldScale[2]
                    };
                    const Vec3 worldCenter = RotateByEulerDegrees(scaledCenter, transform.worldRotation) + Vec3 {
                        transform.worldPosition[0],
                        transform.worldPosition[1],
                        transform.worldPosition[2]
                    };
                    const float distance = Length(worldCenter - cameraPosition);
                    if (distance <= sectionLoadDistance)
                    {
                        requestedSections.push_back(sectionIndex);
                    }
                    if (distance < nearestDistance)
                    {
                        nearestDistance = distance;
                        nearestSection = sectionIndex;
                    }
                }

                if (requestedSections.empty() && !state.assetData.sections.empty())
                {
                    requestedSections.push_back(nearestSection);
                }
            }
            else
            {
                requestedSections.reserve(state.assetData.sections.size());
                for (std::uint32_t sectionIndex = 0;
                     sectionIndex < static_cast<std::uint32_t>(state.assetData.sections.size());
                     ++sectionIndex)
                {
                    requestedSections.push_back(sectionIndex);
                }
            }

            if (requestedSections != state.activeSections)
            {
                for (std::uint32_t sectionIndex = 0;
                     sectionIndex < static_cast<std::uint32_t>(state.sectionStreams.size());
                     ++sectionIndex)
                {
                    if (std::find(requestedSections.begin(), requestedSections.end(), sectionIndex) != requestedSections.end())
                    {
                        continue;
                    }

                    auto& sectionStream = state.sectionStreams[sectionIndex];
                    if (sectionStream.streamHandle != 0)
                    {
                        context.streamingService->Release(sectionStream.streamHandle);
                        sectionStream.streamHandle = 0;
                    }
                    sectionStream.mesh = {};
                    sectionStream.residentCpuBytes = 0;
                    sectionStream.decodeFailed = false;
                }
            }

            bool allRequestedReady = true;
            bool shouldFallbackToFullMesh = false;
            std::string fullMeshFallbackReason;
            for (const std::uint32_t sectionIndex : requestedSections)
            {
                if (sectionIndex >= state.assetData.sections.size())
                {
                    allRequestedReady = false;
                    continue;
                }

                const Assets::MeshAssetSection& section = state.assetData.sections[sectionIndex];
                if (!section.chunkPath.empty())
                {
                    auto& sectionStream = state.sectionStreams[sectionIndex];
                    const std::filesystem::path chunkPath =
                        section.chunkPath.is_absolute()
                            ? section.chunkPath
                            : (logicalPath.parent_path() / section.chunkPath).lexically_normal();
                    if (sectionStream.streamHandle == 0)
                    {
                        Assets::StreamRequestDesc sectionRequest {};
                        sectionRequest.key = "meshchunk:" + cacheKey + ":section:" + std::to_string(sectionIndex);
                        sectionRequest.sourcePath = chunkPath;
                        sectionRequest.resourceType = Assets::StreamResourceType::Mesh;
                        sectionRequest.priority = Assets::StreamPriority::High;
                        sectionRequest.evictable = false;
                        sectionRequest.estimatedCpuBytes = 256ull * 1024ull;

                        std::string sectionError;
                        sectionStream.streamHandle = context.streamingService->Request(sectionRequest, sectionError);
                        if (sectionStream.streamHandle == 0)
                        {
                            sectionStream.decodeFailed = true;
                            allRequestedReady = false;
                            LogMessage(context.logStreamingError, sectionError);
                            continue;
                        }
                    }

                    Assets::StreamRecord sectionRecord {};
                    if (!context.streamingService->TryGetRecord(sectionStream.streamHandle, sectionRecord) ||
                        sectionRecord.state != Assets::StreamState::Resident)
                    {
                        allRequestedReady = false;
                        continue;
                    }

                    if (sectionStream.decodeFailed &&
                        sectionStream.residentCpuBytes == sectionRecord.residentCpuBytes)
                    {
                        allRequestedReady = false;
                        continue;
                    }

                    if (sectionStream.mesh.vertices.empty() ||
                        sectionStream.mesh.indices.empty() ||
                        sectionStream.residentCpuBytes != sectionRecord.residentCpuBytes)
                    {
                        Assets::StreamPayload sectionPayload {};
                        if (!context.streamingService->TryGetPayload(sectionStream.streamHandle, sectionPayload))
                        {
                            allRequestedReady = false;
                            continue;
                        }

                        std::string sectionError;
                        if (!Assets::LoadMeshChunkPayloadFromBytes(
                                sectionPayload.bytes.data(),
                                sectionPayload.bytes.size(),
                                sectionStream.mesh,
                                sectionError))
                        {
                            sectionStream.mesh = {};
                            sectionStream.residentCpuBytes = sectionRecord.residentCpuBytes;
                            sectionStream.decodeFailed = true;
                            allRequestedReady = false;
                            shouldFallbackToFullMesh = true;
                            fullMeshFallbackReason =
                                "Falling back to full mesh for '" + logicalPath.filename().string() +
                                "' because section " + std::to_string(sectionIndex) + " failed to decode.";
                            LogMessage(context.logStreamingError, sectionError);
                            continue;
                        }

                        sectionStream.residentCpuBytes = sectionRecord.residentCpuBytes;
                        sectionStream.decodeFailed = false;
                    }
                }
            }

            if (shouldFallbackToFullMesh)
            {
                return loadFullCookedMeshFallback(fullMeshFallbackReason);
            }

            if (requestedSections != state.activeSections || state.mesh.vertices.empty() || state.mesh.indices.empty() || !allRequestedReady)
            {
                state.mesh = {};
                std::size_t totalVertices = 0;
                std::size_t totalIndices = 0;
                for (const std::uint32_t sectionIndex : requestedSections)
                {
                    if (sectionIndex >= state.assetData.sections.size())
                    {
                        continue;
                    }
                    const Assets::MeshAssetSection& section = state.assetData.sections[sectionIndex];
                    const PrimitiveMeshData* sectionMesh = &section.mesh;
                    if (!section.chunkPath.empty())
                    {
                        sectionMesh = &state.sectionStreams[sectionIndex].mesh;
                    }
                    totalVertices += sectionMesh->vertices.size();
                    totalIndices += sectionMesh->indices.size();
                }

                state.mesh.vertices.reserve(totalVertices);
                state.mesh.indices.reserve(totalIndices);
                std::uint32_t baseVertex = 0;
                for (const std::uint32_t sectionIndex : requestedSections)
                {
                    if (sectionIndex >= state.assetData.sections.size())
                    {
                        continue;
                    }
                    const Assets::MeshAssetSection& section = state.assetData.sections[sectionIndex];
                    const PrimitiveMeshData* sectionMesh = &section.mesh;
                    if (!section.chunkPath.empty())
                    {
                        sectionMesh = &state.sectionStreams[sectionIndex].mesh;
                    }
                    if (sectionMesh->vertices.empty() || sectionMesh->indices.empty())
                    {
                        continue;
                    }
                    state.mesh.vertices.insert(
                        state.mesh.vertices.end(),
                        sectionMesh->vertices.begin(),
                        sectionMesh->vertices.end());
                    for (const std::uint32_t index : sectionMesh->indices)
                    {
                        state.mesh.indices.push_back(baseVertex + index);
                    }
                    baseVertex += static_cast<std::uint32_t>(sectionMesh->vertices.size());
                }
                state.activeSections = std::move(requestedSections);

                if (state.mesh.vertices.empty() || state.mesh.indices.empty())
                {
                    return loadFullCookedMeshFallback(
                        "Falling back to full mesh for '" + logicalPath.filename().string() +
                        "' because streamed sections produced empty geometry.");
                }
            }
        }

        state.decodeFailed = false;
        state.resolvedLod = record.resolvedLod;
        state.residentCpuBytes = record.residentCpuBytes;
        state.resolvedPath = logicalPath;
        return state.mesh.vertices.empty() || state.mesh.indices.empty() ? nullptr : &state.mesh;
    }

    bool MeshStreamingGeometryService::InvalidateFromStreamingEvent(
        const Assets::StreamEvent& event,
        std::unordered_map<std::string, MeshStreamingAssetState>& streamedMeshAssets) const
    {
        if (event.handle == 0)
        {
            return false;
        }

        bool affectsSceneRenderCache = false;
        for (auto& [key, state] : streamedMeshAssets)
        {
            (void)key;
            auto invalidateSectionState = [&](MeshStreamingSectionStreamState& sectionState)
            {
                sectionState.mesh = {};
                sectionState.residentCpuBytes = 0;
                sectionState.decodeFailed = (event.type == Assets::StreamEventType::Failed);
                if (event.type == Assets::StreamEventType::Released ||
                    event.type == Assets::StreamEventType::Cancelled ||
                    event.type == Assets::StreamEventType::Evicted)
                {
                    sectionState.streamHandle = 0;
                }
            };

            if (state.streamHandle == event.handle)
            {
                affectsSceneRenderCache = true;
                switch (event.type)
                {
                case Assets::StreamEventType::Retargeted:
                case Assets::StreamEventType::Failed:
                case Assets::StreamEventType::Cancelled:
                case Assets::StreamEventType::Released:
                case Assets::StreamEventType::Evicted:
                    state.mesh = {};
                    state.assetData = {};
                    state.activeSections.clear();
                    state.residentCpuBytes = 0;
                    state.resolvedLod = std::numeric_limits<std::uint32_t>::max();
                    state.decodeFailed = (event.type == Assets::StreamEventType::Failed);
                    state.sectionStreamingDisabled = false;
                    for (auto& sectionStream : state.sectionStreams)
                    {
                        invalidateSectionState(sectionStream);
                    }
                    if (event.type == Assets::StreamEventType::Released ||
                        event.type == Assets::StreamEventType::Cancelled ||
                        event.type == Assets::StreamEventType::Evicted)
                    {
                        state.streamHandle = 0;
                    }
                    break;
                case Assets::StreamEventType::Queued:
                case Assets::StreamEventType::Started:
                case Assets::StreamEventType::Completed:
                case Assets::StreamEventType::BudgetUpdated:
                    break;
                }
                continue;
            }

            bool handledSectionEvent = false;
            for (auto& sectionStream : state.sectionStreams)
            {
                if (sectionStream.streamHandle != event.handle)
                {
                    continue;
                }

                switch (event.type)
                {
                case Assets::StreamEventType::Retargeted:
                case Assets::StreamEventType::Failed:
                case Assets::StreamEventType::Cancelled:
                case Assets::StreamEventType::Released:
                case Assets::StreamEventType::Evicted:
                    invalidateSectionState(sectionStream);
                    state.mesh = {};
                    state.sectionStreamingDisabled = false;
                    break;
                case Assets::StreamEventType::Queued:
                case Assets::StreamEventType::Started:
                case Assets::StreamEventType::Completed:
                case Assets::StreamEventType::BudgetUpdated:
                    break;
                }
                handledSectionEvent = true;
                break;
            }

            if (handledSectionEvent)
            {
                affectsSceneRenderCache = true;
            }
        }

        return affectsSceneRenderCache;
    }
}
