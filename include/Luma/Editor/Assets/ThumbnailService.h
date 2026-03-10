#pragma once

#include <cstddef>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Luma/Asset/Core/AssetTypes.h"

namespace Luma
{
    class IRenderBackend;

    namespace Editor
    {
        using ThumbnailHandle = std::uint64_t;

        struct ThumbnailSize
        {
            int width = 128;
            int height = 128;
        };

        struct ThumbnailRequestOptions
        {
            bool highPriority = false;
            bool forceRegenerate = false;
        };

        enum class ThumbnailState
        {
            Missing = 0,
            Pending,
            Ready,
            Failed
        };

        enum class ThumbnailAssetClass
        {
            Unknown = 0,
            Image,
            Mesh
        };

        struct ThumbnailQueryResult
        {
            void* texture = nullptr;
            ThumbnailState state = ThumbnailState::Missing;
            int width = 0;
            int height = 0;
        };

        struct ThumbnailRenderContext
        {
            std::filesystem::path assetPath;
            ThumbnailAssetClass assetClass = ThumbnailAssetClass::Unknown;
            ThumbnailSize size {};
        };

        struct ThumbnailRenderOutput
        {
            void* texture = nullptr;
            int width = 0;
            int height = 0;
        };

        struct ThumbnailServiceStats
        {
            std::size_t cacheEntryCount = 0;
            std::size_t readyCount = 0;
            std::size_t pendingCount = 0;
            std::size_t failedCount = 0;
            std::size_t pendingQueueDepth = 0;
            std::size_t backgroundJobQueueDepth = 0;
            std::size_t backgroundResultQueueDepth = 0;
            std::size_t lastProcessedCount = 0;
            std::size_t lastUploadCount = 0;
            std::size_t maxUploadsPerTick = 0;
            double lastTickMs = 0.0;
            double lastUploadMs = 0.0;
        };

        class IThumbnailRenderer
        {
        public:
            virtual ~IThumbnailRenderer() = default;

            virtual int Priority() const
            {
                return 0;
            }

            virtual bool CanRender(const ThumbnailRenderContext& context) const = 0;
            virtual ThumbnailRenderOutput Render(IRenderBackend& renderer, const ThumbnailRenderContext& context) = 0;
        };

        class ThumbnailService
        {
        public:
            ThumbnailService();
            ~ThumbnailService();

            ThumbnailHandle RequestThumbnail(
                const std::filesystem::path& assetPath,
                ThumbnailSize size = {},
                const ThumbnailRequestOptions& options = {});
            ThumbnailHandle RequestThumbnail(
                Assets::AssetID assetId,
                const std::filesystem::path& assetPath,
                ThumbnailSize size = {},
                const ThumbnailRequestOptions& options = {});

            ThumbnailQueryResult GetThumbnail(ThumbnailHandle handle) const;
            ThumbnailQueryResult GetThumbnail(const std::filesystem::path& assetPath, ThumbnailSize size = {}) const;
            ThumbnailQueryResult GetThumbnailByAssetID(Assets::AssetID assetId, ThumbnailSize size = {}) const;

            void Invalidate(const std::filesystem::path& assetPath);
            void Invalidate(Assets::AssetID assetId);
            void InvalidateAll();
            void InvalidateAll(ThumbnailAssetClass assetClass);

            void Prewarm(const std::vector<std::filesystem::path>& assetPaths, ThumbnailSize size = {});
            void PrewarmFolder(const std::filesystem::path& folderPath, ThumbnailSize size = {});
            void PruneToPaths(const std::vector<std::filesystem::path>& keepPaths);

            void SetBudgetMsPerFrame(double milliseconds);
            void SetConcurrency(std::size_t concurrency);

            void RegisterRenderer(std::unique_ptr<IThumbnailRenderer> renderer);

            void Tick(IRenderBackend& renderer);
            void Shutdown(IRenderBackend* renderer);
            ThumbnailServiceStats GetStats() const;

        private:
            struct CacheEntry
            {
                ThumbnailHandle handle = 0;
                Assets::AssetID assetId = 0;
                std::filesystem::path assetPath;
                std::string normalizedPathKey;
                ThumbnailAssetClass assetClass = ThumbnailAssetClass::Unknown;
                ThumbnailSize size {};
                std::uint64_t sourceStamp = 0;
                void* texture = nullptr;
                int width = 0;
                int height = 0;
                ThumbnailState state = ThumbnailState::Missing;
                bool queued = false;
                bool backgroundJobQueued = false;
                std::uint64_t backgroundJobSourceStamp = 0;
            };

            struct BackgroundThumbnailJob
            {
                ThumbnailHandle handle = 0;
                std::filesystem::path assetPath;
                ThumbnailAssetClass assetClass = ThumbnailAssetClass::Unknown;
                ThumbnailSize size {};
                std::uint64_t sourceStamp = 0;
            };

            struct BackgroundThumbnailResult
            {
                ThumbnailHandle handle = 0;
                std::uint64_t sourceStamp = 0;
                std::vector<std::uint8_t> rgbaPixels;
                int width = 0;
                int height = 0;
                bool succeeded = false;
            };

            ThumbnailSize SanitizeSize(ThumbnailSize size) const;
            ThumbnailHandle EnsureRequest(
                Assets::AssetID assetId,
                const std::filesystem::path& assetPath,
                const ThumbnailSize& size,
                const ThumbnailRequestOptions& options);
            ThumbnailAssetClass DetectAssetClass(const std::filesystem::path& assetPath) const;
            std::string NormalizePathKey(const std::filesystem::path& path) const;
            std::string BuildPathSizeKey(const std::filesystem::path& path, const ThumbnailSize& size) const;
            std::uint64_t ComputeSourceStamp(const std::filesystem::path& path) const;
            void QueueHandle(ThumbnailHandle handle, bool highPriority);
            void RemoveHandle(ThumbnailHandle handle);
            void DestroyEntryTexture(CacheEntry& entry, IRenderBackend* renderer);
            void RegisterBuiltInRenderers();
            void StartMeshWorkerThreads();
            void StopMeshWorkerThreads();
            void RestartMeshWorkerThreads();
            void MeshWorkerLoop();
            void EnqueueBackgroundMeshJob(const CacheEntry& entry);
            void DrainBackgroundMeshResults(IRenderBackend& renderer);
            void AdaptUploadBudget(std::size_t deferredCount);

            ThumbnailHandle m_NextHandle = 1;
            double m_BudgetMsPerFrame = 2.0;
            std::size_t m_Concurrency = 1;
            std::size_t m_MaxUploadsPerTick = 2;
            IRenderBackend* m_LastRenderer = nullptr;

            std::unordered_map<ThumbnailHandle, CacheEntry> m_Entries;
            std::unordered_map<std::string, ThumbnailHandle> m_PathSizeToHandle;
            std::unordered_map<Assets::AssetID, std::vector<ThumbnailHandle>> m_AssetToHandles;
            std::deque<ThumbnailHandle> m_PendingQueue;
            std::unordered_set<ThumbnailHandle> m_PendingSet;
            std::vector<std::unique_ptr<IThumbnailRenderer>> m_Renderers;
            std::vector<std::thread> m_MeshWorkerThreads;
            std::deque<BackgroundThumbnailJob> m_BackgroundMeshJobs;
            std::deque<BackgroundThumbnailResult> m_BackgroundMeshResults;
            mutable std::mutex m_BackgroundMeshMutex;
            std::condition_variable m_BackgroundMeshCv;
            bool m_StopBackgroundMeshWorkers = false;
            std::size_t m_LastProcessedCount = 0;
            std::size_t m_LastUploadCount = 0;
            double m_LastTickMs = 0.0;
            double m_LastUploadMs = 0.0;
        };
    }
}
