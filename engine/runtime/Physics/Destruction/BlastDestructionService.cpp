#include "Luma/Physics/Destruction/BlastDestructionService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "Luma/Core/App/Project.h"
#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Scene/ColliderComponent.h"
#include "Luma/Scene/DestructibleComponent.h"
#include "Luma/Scene/DestructionChunkComponent.h"
#include "Luma/Scene/IDComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/RigidBodyComponent.h"
#include "Luma/Scene/TagComponent.h"
#include "Luma/Scene/TransformComponent.h"

#if defined(LUMA_ENABLE_BLAST) && LUMA_ENABLE_BLAST
#include <NvBlast.h>
#include <NvBlastExtDamageShaders.h>
#include <NvBlastTk.h>
#include <PxQuat.h>
#include <PxVec3.h>
#endif

namespace Luma
{
    namespace
    {
        using json = nlohmann::json;

        struct ParsedBlastChunk
        {
            NvBlastChunkDesc desc {};
            std::array<float, 3> halfExtents { 0.5f, 0.5f, 0.5f };
            std::string meshSource;
        };

        struct ParsedBlastAsset
        {
            std::vector<ParsedBlastChunk> chunks;
            std::vector<NvBlastBondDesc> bonds;
        };

        int ComputeGridCellCount(const std::array<int, 3>& dims)
        {
            return std::max(1, dims[0]) * std::max(1, dims[1]) * std::max(1, dims[2]);
        }

        std::array<int, 3> GetChunkGridDims(const DestructionChunkSize chunkSize)
        {
            switch (chunkSize)
            {
            case DestructionChunkSize::Large:
                return { 2, 1, 1 };
            case DestructionChunkSize::Medium:
                return { 2, 2, 1 };
            case DestructionChunkSize::Small:
                return { 2, 2, 2 };
            case DestructionChunkSize::Tiny:
                return { 4, 2, 2 };
            default:
                return { 2, 2, 1 };
            }
        }

        std::array<int, 3> BuildGridDimsForChunkCount(int chunkCount)
        {
            chunkCount = std::max(1, chunkCount);
            std::array<int, 3> dims { 1, 1, 1 };
            while (ComputeGridCellCount(dims) < chunkCount)
            {
                int axis = 0;
                if (dims[1] < dims[axis])
                {
                    axis = 1;
                }
                if (dims[2] < dims[axis])
                {
                    axis = 2;
                }
                ++dims[axis];
            }
            return dims;
        }

        std::array<int, 3> ResolveChunkGridDims(
            const DestructionChunkSize chunkSize,
            const int desiredChunkCount)
        {
            if (desiredChunkCount > 0)
            {
                return BuildGridDimsForChunkCount(desiredChunkCount);
            }
            return GetChunkGridDims(chunkSize);
        }

        std::uint32_t GetTargetActorCount(
            const DestructionChunkSize chunkSize,
            const int desiredChunkCount,
            const std::size_t availableChunks)
        {
            std::uint32_t target = 2u;
            switch (chunkSize)
            {
            case DestructionChunkSize::Large:
                target = 2u;
                break;
            case DestructionChunkSize::Medium:
                target = 3u;
                break;
            case DestructionChunkSize::Small:
                target = 5u;
                break;
            case DestructionChunkSize::Tiny:
                target = 8u;
                break;
            default:
                target = 2u;
                break;
            }

            if (desiredChunkCount > 0)
            {
                target = static_cast<std::uint32_t>(std::max(2, desiredChunkCount));
            }

            const std::uint32_t maxChunksFromAsset =
                static_cast<std::uint32_t>(std::max<std::size_t>(1, availableChunks));
            target = std::min(target, maxChunksFromAsset);
            target = std::min(target, 128u);
            return target;
        }

        std::filesystem::path ResolveBlastAssetPath(const std::string& blastAsset)
        {
            if (blastAsset.empty())
            {
                return {};
            }

            const std::filesystem::path assetPath(blastAsset);
            if (assetPath.is_absolute())
            {
                return std::filesystem::exists(assetPath) ? assetPath.lexically_normal() : std::filesystem::path {};
            }

            if (Project::IsLoaded())
            {
                const std::filesystem::path projectRoot = Project::GetProjectRoot();
                const std::filesystem::path projectRelative = (projectRoot / assetPath).lexically_normal();
                if (std::filesystem::exists(projectRelative))
                {
                    return projectRelative;
                }

                const std::filesystem::path assetsRelative = (projectRoot / "Assets" / assetPath).lexically_normal();
                if (std::filesystem::exists(assetsRelative))
                {
                    return assetsRelative;
                }
            }

            return std::filesystem::exists(assetPath) ? assetPath.lexically_normal() : std::filesystem::path {};
        }

        float ComputeSpeedDelta(
            const std::array<float, 3>& previousVelocity,
            const std::array<float, 3>& currentVelocity)
        {
            const float deltaX = currentVelocity[0] - previousVelocity[0];
            const float deltaY = currentVelocity[1] - previousVelocity[1];
            const float deltaZ = currentVelocity[2] - previousVelocity[2];
            return std::sqrt(deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ);
        }

        bool LoadParsedBlastAsset(
            const std::filesystem::path& assetPath,
            ParsedBlastAsset& outAsset,
            std::string& outError)
        {
            outAsset = {};
            outError.clear();

            std::ifstream stream(assetPath);
            if (!stream.is_open())
            {
                outError = "Could not open asset file";
                return false;
            }

            json root;
            try
            {
                stream >> root;
            }
            catch (const std::exception& ex)
            {
                outError = ex.what();
                return false;
            }

            const auto chunksIt = root.find("chunks");
            if (chunksIt == root.end() || !chunksIt->is_array() || chunksIt->empty())
            {
                outError = "Asset has no chunks array";
                return false;
            }

            outAsset.chunks.reserve(chunksIt->size());
            for (const auto& chunkJson : *chunksIt)
            {
                ParsedBlastChunk chunk {};
                const auto centroid = chunkJson.value("centroid", std::vector<float> { 0.0f, 0.0f, 0.0f });
                const auto halfExtents = chunkJson.value("halfExtents", std::vector<float> { 0.5f, 0.5f, 0.5f });
                if (centroid.size() != 3 || halfExtents.size() != 3)
                {
                    outError = "Chunk centroid/halfExtents must have three elements";
                    return false;
                }

                for (std::size_t axis = 0; axis < 3; ++axis)
                {
                    chunk.desc.centroid[axis] = centroid[axis];
                    chunk.halfExtents[axis] = std::max(0.01f, halfExtents[axis]);
                }

                chunk.desc.volume = std::max(0.001f, chunkJson.value("volume", 1.0f));
                const int parentIndex = chunkJson.value("parent", -1);
                chunk.desc.parentChunkIndex = parentIndex < 0 ? UINT32_MAX : static_cast<std::uint32_t>(parentIndex);
                chunk.desc.flags = chunkJson.value("support", false) ? NvBlastChunkDesc::SupportFlag : NvBlastChunkDesc::NoFlags;
                chunk.desc.userData = chunkJson.value("userData", 0u);
                chunk.meshSource = chunkJson.value("meshSource", std::string {});
                outAsset.chunks.push_back(chunk);
            }

            const auto bondsIt = root.find("bonds");
            if (bondsIt != root.end() && bondsIt->is_array())
            {
                outAsset.bonds.reserve(bondsIt->size());
                for (const auto& bondJson : *bondsIt)
                {
                    NvBlastBondDesc bondDesc {};
                    const auto normal = bondJson.value("normal", std::vector<float> { 0.0f, 1.0f, 0.0f });
                    const auto centroid = bondJson.value("centroid", std::vector<float> { 0.0f, 0.0f, 0.0f });
                    const auto chunks = bondJson.value("chunks", std::vector<std::uint32_t> {});
                    if (normal.size() != 3 || centroid.size() != 3 || chunks.size() != 2)
                    {
                        outError = "Bond normal/centroid/chunks format is invalid";
                        return false;
                    }

                    for (std::size_t axis = 0; axis < 3; ++axis)
                    {
                        bondDesc.bond.normal[axis] = normal[axis];
                        bondDesc.bond.centroid[axis] = centroid[axis];
                    }

                    bondDesc.bond.area = std::max(0.001f, bondJson.value("area", 1.0f));
                    bondDesc.bond.userData = bondJson.value("userData", 0u);
                    bondDesc.chunkIndices[0] = chunks[0];
                    bondDesc.chunkIndices[1] = chunks[1];
                    outAsset.bonds.push_back(bondDesc);
                }
            }

            return true;
        }

        ParsedBlastAsset BuildProceduralBlastAsset(
            const DestructibleComponent& destructible,
            const ColliderComponent* collider,
            const MeshRendererComponent* meshRenderer)
        {
            ParsedBlastAsset asset;

            std::array<float, 3> rootHalfExtents { 0.5f, 0.5f, 0.5f };
            if (collider != nullptr && collider->shape == ColliderShapeType::Box)
            {
                rootHalfExtents = collider->boxHalfExtents;
            }

            const std::array<int, 3> dims =
                ResolveChunkGridDims(destructible.chunkSize, destructible.desiredChunkCount);
            const int requestedChunkCount =
                destructible.desiredChunkCount > 0 ? destructible.desiredChunkCount : ComputeGridCellCount(dims);
            const int chunkCount = std::clamp(requestedChunkCount, 1, ComputeGridCellCount(dims));
            if (chunkCount <= 0)
            {
                return asset;
            }

            const std::array<float, 3> chunkHalfExtents {
                std::max(0.02f, (rootHalfExtents[0] / static_cast<float>(dims[0])) * 0.95f),
                std::max(0.02f, (rootHalfExtents[1] / static_cast<float>(dims[1])) * 0.95f),
                std::max(0.02f, (rootHalfExtents[2] / static_cast<float>(dims[2])) * 0.95f)
            };
            const float chunkVolume = std::max(
                0.001f,
                chunkHalfExtents[0] * chunkHalfExtents[1] * chunkHalfExtents[2] * 8.0f);

            const auto toCellIndex = [dims](const int x, const int y, const int z)
            {
                return x + y * dims[0] + z * dims[0] * dims[1];
            };

            asset.chunks.reserve(static_cast<std::size_t>(chunkCount));
            std::vector<int> cellToChunk(static_cast<std::size_t>(ComputeGridCellCount(dims)), -1);
            int nextChunkId = 0;
            for (int z = 0; z < dims[2] && nextChunkId < chunkCount; ++z)
            {
                for (int y = 0; y < dims[1] && nextChunkId < chunkCount; ++y)
                {
                    for (int x = 0; x < dims[0] && nextChunkId < chunkCount; ++x)
                    {
                        ParsedBlastChunk chunk {};
                        const std::array<float, 3> normalizedCenter {
                            (static_cast<float>(x) + 0.5f) / static_cast<float>(dims[0]),
                            (static_cast<float>(y) + 0.5f) / static_cast<float>(dims[1]),
                            (static_cast<float>(z) + 0.5f) / static_cast<float>(dims[2])
                        };

                        chunk.desc.centroid[0] = -rootHalfExtents[0] + normalizedCenter[0] * (rootHalfExtents[0] * 2.0f);
                        chunk.desc.centroid[1] = -rootHalfExtents[1] + normalizedCenter[1] * (rootHalfExtents[1] * 2.0f);
                        chunk.desc.centroid[2] = -rootHalfExtents[2] + normalizedCenter[2] * (rootHalfExtents[2] * 2.0f);
                        chunk.desc.volume = chunkVolume;
                        chunk.desc.parentChunkIndex = UINT32_MAX;
                        chunk.desc.flags = NvBlastChunkDesc::SupportFlag;
                        chunk.desc.userData = static_cast<std::uint32_t>(nextChunkId);
                        chunk.halfExtents = chunkHalfExtents;
                        if (meshRenderer != nullptr && !meshRenderer->usePrimitive)
                        {
                            chunk.meshSource = meshRenderer->meshSource;
                        }
                        asset.chunks.push_back(chunk);
                        cellToChunk[static_cast<std::size_t>(toCellIndex(x, y, z))] = nextChunkId;
                        ++nextChunkId;
                    }
                }
            }

            const auto appendBond = [&](const int ax, const int ay, const int az, const int bx, const int by, const int bz)
            {
                NvBlastBondDesc bond {};
                const int chunkA = cellToChunk[static_cast<std::size_t>(toCellIndex(ax, ay, az))];
                const int chunkB = cellToChunk[static_cast<std::size_t>(toCellIndex(bx, by, bz))];
                if (chunkA < 0 || chunkB < 0)
                {
                    return;
                }
                bond.chunkIndices[0] = static_cast<std::uint32_t>(chunkA);
                bond.chunkIndices[1] = static_cast<std::uint32_t>(chunkB);
                const auto& centroidA = asset.chunks[chunkA].desc.centroid;
                const auto& centroidB = asset.chunks[chunkB].desc.centroid;
                for (int axis = 0; axis < 3; ++axis)
                {
                    bond.bond.centroid[axis] = (centroidA[axis] + centroidB[axis]) * 0.5f;
                    bond.bond.normal[axis] = 0.0f;
                }

                int normalAxis = 0;
                float maxDelta = std::abs(centroidB[0] - centroidA[0]);
                for (int axis = 1; axis < 3; ++axis)
                {
                    const float delta = std::abs(centroidB[axis] - centroidA[axis]);
                    if (delta > maxDelta)
                    {
                        maxDelta = delta;
                        normalAxis = axis;
                    }
                }
                bond.bond.normal[normalAxis] = 1.0f;
                bond.bond.area = std::max(
                    0.001f,
                    chunkHalfExtents[(normalAxis + 1) % 3] *
                        chunkHalfExtents[(normalAxis + 2) % 3] * 4.0f);
                bond.bond.userData = static_cast<std::uint32_t>(asset.bonds.size());
                asset.bonds.push_back(bond);
            };

            asset.bonds.reserve(static_cast<std::size_t>(chunkCount) * 3u);
            for (int z = 0; z < dims[2]; ++z)
            {
                for (int y = 0; y < dims[1]; ++y)
                {
                    for (int x = 0; x < dims[0]; ++x)
                    {
                        if (x + 1 < dims[0])
                        {
                            appendBond(x, y, z, x + 1, y, z);
                        }
                        if (y + 1 < dims[1])
                        {
                            appendBond(x, y, z, x, y + 1, z);
                        }
                        if (z + 1 < dims[2])
                        {
                            appendBond(x, y, z, x, y, z + 1);
                        }
                    }
                }
            }

            return asset;
        }

        std::string BuildProceduralSignature(
            const DestructibleComponent& destructible,
            const ColliderComponent* collider,
            const MeshRendererComponent* meshRenderer)
        {
            std::ostringstream signature;
            signature
                << static_cast<int>(destructible.chunkSize)
                << "|" << destructible.desiredChunkCount
                << "|" << destructible.supportDepth
                << "|" << destructible.chunkMassScale;

            if (collider != nullptr)
            {
                signature << "|shape:" << static_cast<int>(collider->shape);
                if (collider->shape == ColliderShapeType::Box)
                {
                    signature
                        << "|box:" << collider->boxHalfExtents[0]
                        << "," << collider->boxHalfExtents[1]
                        << "," << collider->boxHalfExtents[2];
                }
            }
            else
            {
                signature << "|shape:none";
            }

            if (meshRenderer != nullptr)
            {
                if (meshRenderer->usePrimitive)
                {
                    signature << "|primitive:" << static_cast<int>(meshRenderer->primitive);
                }
                else
                {
                    signature << "|mesh:" << meshRenderer->meshSource;
                }
            }
            else
            {
                signature << "|mesh:none";
            }

            return signature.str();
        }

#if defined(LUMA_ENABLE_BLAST) && LUMA_ENABLE_BLAST
        float DegreesToRadians(const float degrees)
        {
            return degrees * 0.01745329251994329577f;
        }

        physx::PxQuat ToPxQuatDegrees(const std::array<float, 3>& rotationDegrees)
        {
            const physx::PxQuat qx(DegreesToRadians(rotationDegrees[0]), physx::PxVec3(1.0f, 0.0f, 0.0f));
            const physx::PxQuat qy(DegreesToRadians(rotationDegrees[1]), physx::PxVec3(0.0f, 1.0f, 0.0f));
            const physx::PxQuat qz(DegreesToRadians(rotationDegrees[2]), physx::PxVec3(0.0f, 0.0f, 1.0f));
            return qz * qy * qx;
        }

        std::array<float, 3> RotateByEulerDegrees(
            const std::array<float, 3>& vector,
            const std::array<float, 3>& rotationDegrees)
        {
            const physx::PxVec3 rotated = ToPxQuatDegrees(rotationDegrees).rotate(
                physx::PxVec3(vector[0], vector[1], vector[2]));
            return { rotated.x, rotated.y, rotated.z };
        }
#endif
    }

    bool BlastDestructionService::Initialize(
#if defined(LUMA_ENABLE_BLAST) && LUMA_ENABLE_BLAST
        physx::PxPhysics* physics
#else
        void* physics
#endif
    )
    {
        Shutdown();

#if defined(LUMA_ENABLE_BLAST) && LUMA_ENABLE_BLAST
        (void)physics;
        m_Framework = NvBlastTkFrameworkCreate();
        if (m_Framework == nullptr)
        {
            m_Status = "Blast framework creation failed";
            LUMA_LOG_ERROR("Physics", "Failed to create Blast toolkit framework.");
            return false;
        }

        m_Initialized = true;
        m_Status = "Blast toolkit ready";
        return true;
#else
        (void)physics;
        m_Status = "Blast disabled at build time";
        return false;
#endif
    }

    void BlastDestructionService::Shutdown()
    {
#if defined(LUMA_ENABLE_BLAST) && LUMA_ENABLE_BLAST
        for (auto& [id, state] : m_RuntimeStates)
        {
            if (state.tkGroup != nullptr)
            {
                state.tkGroup->release();
                state.tkGroup = nullptr;
            }
            if (state.tkFamily != nullptr)
            {
                state.tkFamily->release();
                state.tkFamily = nullptr;
                state.tkRootActor = nullptr;
            }
            if (state.tkAsset != nullptr)
            {
                state.tkAsset->release();
                state.tkAsset = nullptr;
            }
        }

        if (m_Framework != nullptr)
        {
            m_Framework->release();
            m_Framework = nullptr;
        }
#endif

        m_RuntimeStates.clear();
        m_Initialized = false;
        m_Status = "Disabled";
    }

    void BlastDestructionService::SyncScene(Scene& scene)
    {
        auto& registry = scene.GetRegistry();
        std::unordered_set<UUID> activeIds;

        const auto releaseNativeState = [&](RuntimeState& runtimeState)
        {
#if defined(LUMA_ENABLE_BLAST) && LUMA_ENABLE_BLAST
            if (runtimeState.tkGroup != nullptr)
            {
                runtimeState.tkGroup->release();
                runtimeState.tkGroup = nullptr;
            }
            if (runtimeState.tkFamily != nullptr)
            {
                runtimeState.tkFamily->release();
                runtimeState.tkFamily = nullptr;
                runtimeState.tkRootActor = nullptr;
            }
            if (runtimeState.tkAsset != nullptr)
            {
                runtimeState.tkAsset->release();
                runtimeState.tkAsset = nullptr;
            }
#else
            (void)runtimeState;
#endif
        };

        const auto destroySpawnedChunks = [&](const UUID sourceId, RuntimeState& runtimeState)
        {
            std::unordered_set<EntityID> chunksToDestroy;
            chunksToDestroy.reserve(runtimeState.spawnedChunkEntities.size() + 8u);
            for (const EntityID spawned : runtimeState.spawnedChunkEntities)
            {
                if (!registry.valid(spawned) || !registry.all_of<DestructionChunkComponent>(spawned))
                {
                    continue;
                }

                const auto& chunk = registry.get<DestructionChunkComponent>(spawned);
                if (chunk.sourceDestructible == sourceId)
                {
                    chunksToDestroy.insert(spawned);
                }
            }

            auto chunkView = registry.view<DestructionChunkComponent>();
            for (const EntityID entity : chunkView)
            {
                const auto& chunk = chunkView.get<DestructionChunkComponent>(entity);
                if (chunk.sourceDestructible == sourceId)
                {
                    chunksToDestroy.insert(entity);
                }
            }

            for (const EntityID entity : chunksToDestroy)
            {
                if (registry.valid(entity))
                {
                    scene.DestroyEntity(entity);
                }
            }

            runtimeState.spawnedChunkEntities.clear();
        };

        auto view = registry.view<IDComponent, DestructibleComponent>();
        for (const EntityID entity : view)
        {
            const auto& idComponent = view.get<IDComponent>(entity);
            const auto& destructible = view.get<DestructibleComponent>(entity);
            auto& runtimeState = m_RuntimeStates[idComponent.id];
            const std::string resolvedPath = ResolveBlastAssetPath(destructible.blastAsset).string();
            const auto* collider = registry.try_get<ColliderComponent>(entity);
            const auto* meshRenderer = registry.try_get<MeshRendererComponent>(entity);

            if (destructible.active || runtimeState.fractureProcessed)
            {
                activeIds.insert(idComponent.id);
            }

            if (!destructible.active)
            {
                continue;
            }

            if (runtimeState.fractureProcessed)
            {
                destroySpawnedChunks(idComponent.id, runtimeState);
                runtimeState.accumulatedDamage = 0.0f;
                runtimeState.fractureRequested = false;
                runtimeState.fractureProcessed = false;
                runtimeState.warnedMissingAsset = false;
                runtimeState.warnedLoadFailure = false;
                runtimeState.proceduralSignature.clear();
                runtimeState.chunkVisuals.clear();
                releaseNativeState(runtimeState);
            }

            if (!destructible.accumulateDamage)
            {
                runtimeState.accumulatedDamage = 0.0f;
            }

            if (runtimeState.resolvedAssetPath != resolvedPath)
            {
                runtimeState.resolvedAssetPath = resolvedPath;
                runtimeState.warnedMissingAsset = false;
                runtimeState.warnedLoadFailure = false;
                runtimeState.fractureRequested = false;
                runtimeState.fractureProcessed = false;
                runtimeState.proceduralSignature.clear();
                runtimeState.chunkVisuals.clear();
                releaseNativeState(runtimeState);
            }

            if (runtimeState.resolvedAssetPath.empty())
            {
                const std::string proceduralSignature = BuildProceduralSignature(destructible, collider, meshRenderer);
                if (runtimeState.proceduralSignature != proceduralSignature)
                {
                    runtimeState.proceduralSignature = proceduralSignature;
                    runtimeState.warnedLoadFailure = false;
                    runtimeState.fractureRequested = false;
                    runtimeState.fractureProcessed = false;
                    runtimeState.chunkVisuals.clear();
                    releaseNativeState(runtimeState);
                }
            }
            else
            {
                runtimeState.proceduralSignature.clear();
            }
        }

        for (auto it = m_RuntimeStates.begin(); it != m_RuntimeStates.end();)
        {
            if (!activeIds.contains(it->first))
            {
                destroySpawnedChunks(it->first, it->second);
                releaseNativeState(it->second);
                it = m_RuntimeStates.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void BlastDestructionService::SubmitVelocityDamage(
        Scene& scene,
        const EntityID entity,
        const std::array<float, 3>& previousVelocity,
        const std::array<float, 3>& currentVelocity,
        const float deltaTimeSeconds)
    {
        if (!m_Initialized || deltaTimeSeconds <= 0.0f)
        {
            return;
        }

        auto& registry = scene.GetRegistry();
        if (!registry.valid(entity) || !registry.all_of<IDComponent, DestructibleComponent>(entity))
        {
            return;
        }

        const auto& idComponent = registry.get<IDComponent>(entity);
        auto& destructible = registry.get<DestructibleComponent>(entity);
        if (!destructible.active || !destructible.fractureOnImpact)
        {
            return;
        }

        auto& runtimeState = m_RuntimeStates[idComponent.id];
        if (runtimeState.resolvedAssetPath.empty() && !destructible.blastAsset.empty() && !runtimeState.warnedMissingAsset)
        {
            runtimeState.warnedMissingAsset = true;
            LUMA_LOG_WARN(
                "Physics",
                "Blast asset could not be resolved for destructible entity '" +
                    (registry.all_of<TagComponent>(entity) ? registry.get<TagComponent>(entity).name : std::string("Entity")) +
                    "': " + destructible.blastAsset);
        }

        const float speedDelta = ComputeSpeedDelta(previousVelocity, currentVelocity);
        if (speedDelta <= 0.01f)
        {
            return;
        }

        const auto* rigidBody = registry.try_get<RigidBodyComponent>(entity);
        const float mass = rigidBody != nullptr ? std::max(0.001f, rigidBody->mass) : 1.0f;
        const float impactDamage = speedDelta * mass * std::max(0.0f, destructible.impactDamageScale);

        if (destructible.accumulateDamage)
        {
            runtimeState.accumulatedDamage += impactDamage;
        }
        else
        {
            runtimeState.accumulatedDamage = impactDamage;
        }

        const float threshold = std::max(0.001f, destructible.damageThreshold);
        if (!runtimeState.fractureRequested && !runtimeState.fractureProcessed && runtimeState.accumulatedDamage >= threshold)
        {
            runtimeState.fractureRequested = true;
        }
    }

    void BlastDestructionService::ProcessPendingFractures(Scene& scene)
    {
        if (!m_Initialized)
        {
            return;
        }

#if defined(LUMA_ENABLE_BLAST) && LUMA_ENABLE_BLAST
        auto& registry = scene.GetRegistry();

        for (auto& [uuid, runtimeState] : m_RuntimeStates)
        {
            if (!runtimeState.fractureRequested || runtimeState.fractureProcessed)
            {
                continue;
            }

            const EntityID entity = scene.FindByUUID(uuid);
            if (entity == entt::null || !registry.valid(entity) || !registry.all_of<DestructibleComponent, TransformComponent>(entity))
            {
                continue;
            }

            auto& destructible = registry.get<DestructibleComponent>(entity);
            auto& transform = registry.get<TransformComponent>(entity);
            const auto* tag = registry.try_get<TagComponent>(entity);
            auto* rigidBody = registry.try_get<RigidBodyComponent>(entity);
            auto* meshRenderer = registry.try_get<MeshRendererComponent>(entity);
            auto* collider = registry.try_get<ColliderComponent>(entity);

            if (runtimeState.tkAsset == nullptr)
            {
                ParsedBlastAsset parsedAsset;
                if (!runtimeState.resolvedAssetPath.empty())
                {
                    std::string error;
                    if (!LoadParsedBlastAsset(runtimeState.resolvedAssetPath, parsedAsset, error))
                    {
                        if (!runtimeState.warnedLoadFailure)
                        {
                            runtimeState.warnedLoadFailure = true;
                            LUMA_LOG_WARN(
                                "Physics",
                                "Failed to load Blast asset '" + runtimeState.resolvedAssetPath + "': " + error);
                        }
                        parsedAsset = BuildProceduralBlastAsset(destructible, collider, meshRenderer);
                    }
                }
                else
                {
                    parsedAsset = BuildProceduralBlastAsset(destructible, collider, meshRenderer);
                }

                std::vector<NvBlastChunkDesc> chunkDescs(parsedAsset.chunks.size());
                std::vector<std::uint32_t> reorderMap(parsedAsset.chunks.size(), 0u);
                std::vector<NvBlastBondDesc> bondDescs = parsedAsset.bonds;
                runtimeState.chunkVisuals.clear();
                runtimeState.chunkVisuals.reserve(parsedAsset.chunks.size());

                for (std::size_t i = 0; i < parsedAsset.chunks.size(); ++i)
                {
                    chunkDescs[i] = parsedAsset.chunks[i].desc;
                    RuntimeState::ChunkVisual visual;
                    visual.centroid = {
                        parsedAsset.chunks[i].desc.centroid[0],
                        parsedAsset.chunks[i].desc.centroid[1],
                        parsedAsset.chunks[i].desc.centroid[2]
                    };
                    visual.halfExtents = parsedAsset.chunks[i].halfExtents;
                    visual.meshSource = parsedAsset.chunks[i].meshSource;
                    runtimeState.chunkVisuals.push_back(std::move(visual));
                }

                m_Framework->ensureAssetExactSupportCoverage(chunkDescs.data(), static_cast<std::uint32_t>(chunkDescs.size()));
                m_Framework->reorderAssetDescChunks(
                    chunkDescs.data(),
                    static_cast<std::uint32_t>(chunkDescs.size()),
                    bondDescs.empty() ? nullptr : bondDescs.data(),
                    static_cast<std::uint32_t>(bondDescs.size()),
                    reorderMap.data(),
                    true);

                if (!reorderMap.empty())
                {
                    std::vector<RuntimeState::ChunkVisual> reordered(runtimeState.chunkVisuals.size());
                    for (std::size_t oldIndex = 0; oldIndex < reorderMap.size(); ++oldIndex)
                    {
                        const std::uint32_t newIndex = reorderMap[oldIndex];
                        if (newIndex < reordered.size())
                        {
                            reordered[newIndex] = runtimeState.chunkVisuals[oldIndex];
                        }
                    }
                    runtimeState.chunkVisuals = std::move(reordered);
                }

                Nv::Blast::TkAssetDesc assetDesc;
                assetDesc.chunkCount = static_cast<std::uint32_t>(chunkDescs.size());
                assetDesc.chunkDescs = chunkDescs.data();
                assetDesc.bondCount = static_cast<std::uint32_t>(bondDescs.size());
                assetDesc.bondDescs = bondDescs.empty() ? nullptr : bondDescs.data();
                assetDesc.bondFlags = nullptr;

                runtimeState.tkAsset = m_Framework->createAsset(assetDesc);
                if (runtimeState.tkAsset == nullptr)
                {
                    LUMA_LOG_WARN("Physics", "Failed to create Blast toolkit asset for " + runtimeState.resolvedAssetPath);
                    continue;
                }

                Nv::Blast::TkActorDesc actorDesc(runtimeState.tkAsset);
                runtimeState.tkRootActor = m_Framework->createActor(actorDesc);
                if (runtimeState.tkRootActor == nullptr)
                {
                    runtimeState.tkAsset->release();
                    runtimeState.tkAsset = nullptr;
                    LUMA_LOG_WARN("Physics", "Failed to create Blast toolkit actor for " + runtimeState.resolvedAssetPath);
                    continue;
                }

                runtimeState.tkFamily = &runtimeState.tkRootActor->getFamily();
                Nv::Blast::TkGroupDesc groupDesc {};
                groupDesc.workerCount = 1;
                runtimeState.tkGroup = m_Framework->createGroup(groupDesc);
                if (runtimeState.tkGroup == nullptr || !runtimeState.tkGroup->addActor(*runtimeState.tkRootActor))
                {
                    if (runtimeState.tkGroup != nullptr)
                    {
                        runtimeState.tkGroup->release();
                        runtimeState.tkGroup = nullptr;
                    }
                    runtimeState.tkFamily->release();
                    runtimeState.tkFamily = nullptr;
                    runtimeState.tkRootActor = nullptr;
                    runtimeState.tkAsset->release();
                    runtimeState.tkAsset = nullptr;
                    LUMA_LOG_WARN("Physics", "Failed to create Blast processing group for " + runtimeState.resolvedAssetPath);
                    continue;
                }
            }

            if (runtimeState.tkRootActor == nullptr || runtimeState.tkGroup == nullptr || runtimeState.tkFamily == nullptr)
            {
                continue;
            }

            const float threshold = std::max(0.001f, destructible.damageThreshold);
            const float normalizedDamage = std::clamp(runtimeState.accumulatedDamage / threshold, 0.0f, 1.0f);
            const float chunkCountScale = std::max(1.0f, static_cast<float>(runtimeState.chunkVisuals.size()) * 0.5f);
            float proceduralRadius = 0.25f;
            for (const auto& visual : runtimeState.chunkVisuals)
            {
                const float centroidLength = std::sqrt(
                    visual.centroid[0] * visual.centroid[0] +
                    visual.centroid[1] * visual.centroid[1] +
                    visual.centroid[2] * visual.centroid[2]);
                const float maxHalfExtent =
                    std::max(visual.halfExtents[0], std::max(visual.halfExtents[1], visual.halfExtents[2]));
                proceduralRadius = std::max(proceduralRadius, centroidLength + maxHalfExtent);
            }

            NvBlastExtRadialDamageDesc damageDesc {};
            damageDesc.damage = std::max(1.0f, normalizedDamage * chunkCountScale);
            damageDesc.position[0] = 0.0f;
            damageDesc.position[1] = 0.0f;
            damageDesc.position[2] = 0.0f;
            damageDesc.minRadius = 0.0f;
            damageDesc.maxRadius = std::max(proceduralRadius * 1.1f, std::max(0.25f, destructible.damageSpread));

            NvBlastDamageProgram damageProgram {};
            damageProgram.graphShaderFunction = NvBlastExtFalloffGraphShader;
            damageProgram.subgraphShaderFunction = NvBlastExtFalloffSubgraphShader;
            NvBlastExtProgramParams programParams(&damageDesc);

            std::uint32_t actorCount = runtimeState.tkFamily->getActorCount();
            const std::uint32_t targetActorCount =
                GetTargetActorCount(destructible.chunkSize, destructible.desiredChunkCount, runtimeState.chunkVisuals.size());
            constexpr std::uint32_t maxDamagePasses = 4;
            for (std::uint32_t pass = 0; pass < maxDamagePasses; ++pass)
            {
                if (actorCount == 0)
                {
                    break;
                }
                if (actorCount >= targetActorCount)
                {
                    break;
                }

                std::vector<Nv::Blast::TkActor*> passActors(actorCount, nullptr);
                runtimeState.tkFamily->getActors(passActors.data(), actorCount);
                for (Nv::Blast::TkActor* actor : passActors)
                {
                    if (actor != nullptr)
                    {
                        actor->damage(damageProgram, &programParams);
                    }
                }

                runtimeState.tkGroup->process();
                actorCount = runtimeState.tkFamily->getActorCount();

                // Escalate follow-up passes so smaller chunk presets don't stall as two-piece splits.
                damageDesc.damage *= 1.35f;
                damageDesc.maxRadius *= 1.1f;
            }

            if (actorCount <= 1)
            {
                runtimeState.fractureRequested = false;
                runtimeState.accumulatedDamage = threshold * 0.5f;
                continue;
            }

            std::vector<Nv::Blast::TkActor*> actors(actorCount, nullptr);
            runtimeState.tkFamily->getActors(actors.data(), actorCount);

            for (const EntityID spawned : runtimeState.spawnedChunkEntities)
            {
                scene.DestroyEntity(spawned);
            }
            runtimeState.spawnedChunkEntities.clear();

            const float originalMass = rigidBody != nullptr ? std::max(0.1f, rigidBody->mass) : 1.0f;
            const float chunkMass = std::max(
                0.05f,
                (originalMass / std::max(1u, actorCount)) * std::max(0.1f, destructible.chunkMassScale));

            const std::array<float, 3> entityScale = transform.scale;
            for (std::uint32_t actorIndex = 0; actorIndex < actorCount; ++actorIndex)
            {
                Nv::Blast::TkActor* actor = actors[actorIndex];
                if (actor == nullptr)
                {
                    continue;
                }

                const std::uint32_t visibleChunkCount = actor->getVisibleChunkCount();
                if (visibleChunkCount == 0)
                {
                    continue;
                }

                std::vector<std::uint32_t> visibleChunks(visibleChunkCount, 0u);
                actor->getVisibleChunkIndices(visibleChunks.data(), visibleChunkCount);

                std::array<float, 3> localMin {
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max()
                };
                std::array<float, 3> localMax {
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest()
                };
                std::string chunkMeshSource;

                for (const std::uint32_t chunkIndex : visibleChunks)
                {
                    if (chunkIndex >= runtimeState.chunkVisuals.size())
                    {
                        continue;
                    }

                    const auto& visual = runtimeState.chunkVisuals[chunkIndex];
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        localMin[axis] = std::min(localMin[axis], visual.centroid[axis] - visual.halfExtents[axis]);
                        localMax[axis] = std::max(localMax[axis], visual.centroid[axis] + visual.halfExtents[axis]);
                    }

                    if (chunkMeshSource.empty() && !visual.meshSource.empty())
                    {
                        chunkMeshSource = visual.meshSource;
                    }
                }

                std::array<float, 3> localCenter {
                    (localMin[0] + localMax[0]) * 0.5f,
                    (localMin[1] + localMax[1]) * 0.5f,
                    (localMin[2] + localMax[2]) * 0.5f
                };
                std::array<float, 3> localHalfExtents {
                    std::max(0.05f, (localMax[0] - localMin[0]) * 0.5f),
                    std::max(0.05f, (localMax[1] - localMin[1]) * 0.5f),
                    std::max(0.05f, (localMax[2] - localMin[2]) * 0.5f)
                };

                std::array<float, 3> scaledLocalCenter {
                    localCenter[0] * entityScale[0],
                    localCenter[1] * entityScale[1],
                    localCenter[2] * entityScale[2]
                };
                const auto rotatedOffset = RotateByEulerDegrees(scaledLocalCenter, transform.rotation);

                Entity chunkEntity = scene.CreateEntity(
                    (tag != nullptr ? tag->name : std::string("Destructible")) + "_Chunk_" + std::to_string(actorIndex));
                auto& chunkTransform = chunkEntity.GetComponent<TransformComponent>();
                chunkTransform.position = {
                    transform.position[0] + rotatedOffset[0],
                    transform.position[1] + rotatedOffset[1],
                    transform.position[2] + rotatedOffset[2]
                };
                chunkTransform.rotation = transform.rotation;
                chunkTransform.scale = {
                    localHalfExtents[0] * entityScale[0] * 2.0f,
                    localHalfExtents[1] * entityScale[1] * 2.0f,
                    localHalfExtents[2] * entityScale[2] * 2.0f
                };
                chunkTransform.dirty = true;

                auto& chunkMesh = chunkEntity.AddComponent<MeshRendererComponent>();
                chunkMesh.visible = true;
                if (!chunkMeshSource.empty())
                {
                    chunkMesh.usePrimitive = false;
                    chunkMesh.meshSource = chunkMeshSource;
                }
                else
                {
                    chunkMesh.usePrimitive = true;
                    chunkMesh.primitive = PrimitiveType::Cube;
                }

                auto& chunkBody = chunkEntity.AddComponent<RigidBodyComponent>();
                chunkBody.active = true;
                chunkBody.bodyType = RigidBodyType::Dynamic;
                chunkBody.enableGravity = true;
                chunkBody.startAwake = true;
                // Destruction chunks can be small and fast; force CCD to prevent tunneling through colliders.
                chunkBody.enableCCD = true;
                chunkBody.mass = chunkMass;
                chunkBody.linearDamping = rigidBody != nullptr ? rigidBody->linearDamping : 0.05f;
                chunkBody.angularDamping = rigidBody != nullptr ? rigidBody->angularDamping : 0.05f;
                chunkBody.maxLinearVelocity = std::max(10.0f, destructible.maxChunkSpeed);
                chunkBody.maxAngularVelocity = rigidBody != nullptr ? rigidBody->maxAngularVelocity : 360.0f;
                chunkBody.linearVelocity = rigidBody != nullptr ? rigidBody->linearVelocity : std::array<float, 3> { 0.0f, 0.0f, 0.0f };

                auto& chunkCollider = chunkEntity.AddComponent<ColliderComponent>();
                chunkCollider.active = true;
                chunkCollider.isTrigger = false;
                chunkCollider.shape = ColliderShapeType::Box;
                // Collider size is driven by transform scale in the PhysX backend.
                // Keep collider extents normalized so we don't double-apply scaling.
                chunkCollider.boxHalfExtents = { 0.5f, 0.5f, 0.5f };

                auto& chunkState = chunkEntity.AddComponent<DestructionChunkComponent>();
                chunkState.sourceDestructible = uuid;
                chunkState.ageSeconds = 0.0f;
                chunkState.lifetimeSeconds = std::max(0.0f, destructible.debrisLifetime);

                runtimeState.spawnedChunkEntities.push_back(chunkEntity.GetHandle());
            }

            if (meshRenderer != nullptr)
            {
                meshRenderer->visible = false;
            }
            if (rigidBody != nullptr)
            {
                rigidBody->active = false;
                rigidBody->linearVelocity = { 0.0f, 0.0f, 0.0f };
                rigidBody->angularVelocity = { 0.0f, 0.0f, 0.0f };
                rigidBody->sleeping = true;
            }
            if (collider != nullptr)
            {
                collider->active = false;
            }

            destructible.active = false;
            runtimeState.fractureRequested = false;
            runtimeState.fractureProcessed = true;
            runtimeState.accumulatedDamage = 0.0f;
            runtimeState.tkRootActor = nullptr;

            LUMA_LOG_INFO(
                "Physics",
                "Blast fractured '" +
                    (tag != nullptr ? tag->name : std::string("Destructible")) +
                    "' into " + std::to_string(runtimeState.spawnedChunkEntities.size()) + " chunk actors.");
        }
#else
        (void)scene;
#endif
    }

    void BlastDestructionService::Tick(Scene& scene, const float deltaTimeSeconds)
    {
        if (!m_Initialized || deltaTimeSeconds <= 0.0f)
        {
            return;
        }

        auto& registry = scene.GetRegistry();
        for (auto& [sourceId, runtimeState] : m_RuntimeStates)
        {
            std::vector<EntityID> survivingChunks;
            survivingChunks.reserve(runtimeState.spawnedChunkEntities.size());
            for (const EntityID entity : runtimeState.spawnedChunkEntities)
            {
                if (!registry.valid(entity) || !registry.all_of<DestructionChunkComponent>(entity))
                {
                    continue;
                }

                auto& chunk = registry.get<DestructionChunkComponent>(entity);
                if (chunk.sourceDestructible != sourceId)
                {
                    continue;
                }

                chunk.ageSeconds += deltaTimeSeconds;
                if (chunk.lifetimeSeconds > 0.0f && chunk.ageSeconds >= chunk.lifetimeSeconds)
                {
                    scene.DestroyEntity(entity);
                    continue;
                }

                survivingChunks.push_back(entity);
            }

            runtimeState.spawnedChunkEntities = std::move(survivingChunks);
        }
    }

    bool BlastDestructionService::IsInitialized() const
    {
        return m_Initialized;
    }

    bool BlastDestructionService::IsRuntimeAvailable() const
    {
#if defined(LUMA_ENABLE_BLAST) && LUMA_ENABLE_BLAST
        return m_Framework != nullptr;
#else
        return false;
#endif
    }

    std::size_t BlastDestructionService::GetTrackedActorCount() const
    {
        return m_RuntimeStates.size();
    }

    std::size_t BlastDestructionService::GetPendingFractureCount() const
    {
        return std::count_if(
            m_RuntimeStates.begin(),
            m_RuntimeStates.end(),
            [](const auto& entry)
            {
                return entry.second.fractureRequested;
            });
    }

    std::string_view BlastDestructionService::GetStatus() const
    {
        return m_Status;
    }
}
