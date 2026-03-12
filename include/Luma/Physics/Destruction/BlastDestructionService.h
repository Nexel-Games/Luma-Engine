#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Luma/Scene/Scene.h"
#include "Luma/Scene/UUID.h"

#if defined(LUMA_ENABLE_BLAST) && LUMA_ENABLE_BLAST
namespace physx
{
    class PxPhysics;
}

namespace Nv::Blast
{
    class TkActor;
    class TkAsset;
    class TkFamily;
    class TkFramework;
    class TkGroup;
}
#endif

namespace Luma
{
    class BlastDestructionService
    {
    public:
        bool Initialize(
#if defined(LUMA_ENABLE_BLAST) && LUMA_ENABLE_BLAST
            physx::PxPhysics* physics
#else
            void* physics
#endif
        );
        void Shutdown();

        void SyncScene(Scene& scene);
        void SubmitVelocityDamage(
            Scene& scene,
            EntityID entity,
            const std::array<float, 3>& previousVelocity,
            const std::array<float, 3>& currentVelocity,
            float deltaTimeSeconds);
        void ProcessPendingFractures(Scene& scene);
        void Tick(Scene& scene, float deltaTimeSeconds);

        bool IsInitialized() const;
        bool IsRuntimeAvailable() const;
        std::size_t GetTrackedActorCount() const;
        std::size_t GetPendingFractureCount() const;
        std::string_view GetStatus() const;

    private:
        struct RuntimeState
        {
            struct ChunkVisual
            {
                std::array<float, 3> centroid { 0.0f, 0.0f, 0.0f };
                std::array<float, 3> halfExtents { 0.5f, 0.5f, 0.5f };
                std::string meshSource;
            };

            std::string resolvedAssetPath;
            float accumulatedDamage = 0.0f;
            bool fractureRequested = false;
            bool fractureProcessed = false;
            bool warnedMissingAsset = false;
            bool warnedLoadFailure = false;
            std::string proceduralSignature;
            std::vector<EntityID> spawnedChunkEntities;
            std::vector<ChunkVisual> chunkVisuals;

#if defined(LUMA_ENABLE_BLAST) && LUMA_ENABLE_BLAST
            Nv::Blast::TkAsset* tkAsset = nullptr;
            Nv::Blast::TkFamily* tkFamily = nullptr;
            Nv::Blast::TkActor* tkRootActor = nullptr;
            Nv::Blast::TkGroup* tkGroup = nullptr;
#endif
        };

        std::unordered_map<UUID, RuntimeState> m_RuntimeStates;
        std::string m_Status = "Disabled";
        bool m_Initialized = false;

#if defined(LUMA_ENABLE_BLAST) && LUMA_ENABLE_BLAST
        Nv::Blast::TkFramework* m_Framework = nullptr;
#endif
    };
}
