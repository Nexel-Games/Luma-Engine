#pragma once

#include <atomic>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "Luma/Asset/Streaming/IResourceStreamingService.h"

namespace Luma::Assets
{
    class ResourceStreamingService final : public IResourceStreamingService
    {
    public:
        ResourceStreamingService();
        ~ResourceStreamingService() override;

        bool Initialize(const std::filesystem::path& projectRoot, std::string& outError) override;
        void Shutdown() override;
        void Tick() override;
        void WaitIdle() override;

        void RegisterProvider(std::shared_ptr<IResourceStreamProvider> provider) override;
        void SetEventCallback(StreamEventCallback callback) override;

        StreamRequestHandle Request(const StreamRequestDesc& request, std::string& outError) override;
        bool RetargetLOD(StreamRequestHandle handle, std::uint32_t targetLod, std::string& outError) override;
        bool Cancel(StreamRequestHandle handle) override;
        bool Release(StreamRequestHandle handle) override;
        bool ReleaseByKey(std::string_view key) override;

        bool TryGetRecord(StreamRequestHandle handle, StreamRecord& outRecord) const override;
        bool TryGetPayload(StreamRequestHandle handle, StreamPayload& outPayload) const override;
        std::vector<StreamRecord> GetRecords() const override;

        void SetBudget(const StreamingBudget& budget) override;
        StreamingBudget GetBudget() const override;
        StreamingStats GetStats() const override;

    private:
        struct RecordEntry
        {
            StreamRecord record {};
            StreamRequestDesc request {};
            StreamPayload payload {};
            std::uint32_t activeLod = 0;
        };

        struct CompletedJob
        {
            StreamRequestHandle handle = 0;
            StreamPayload payload {};
            std::string error;
        };

        void EmitEvent(
            StreamEventType type,
            StreamRequestHandle handle,
            std::string key,
            StreamState state,
            float progress01,
            std::string message) const;
        void ConsumeCompletedJobs();
        void DispatchQueuedJobs();
        void ApplyBudgetConstraints();
        std::shared_ptr<IResourceStreamProvider> FindProviderForRequest(const StreamRequestDesc& request) const;

        std::filesystem::path m_ProjectRoot;
        bool m_Initialized = false;
        std::atomic<bool> m_ShuttingDown { false };
        StreamingBudget m_Budget {};
        StreamEventCallback m_EventCallback;
        std::vector<std::shared_ptr<IResourceStreamProvider>> m_Providers;

        mutable std::mutex m_Mutex;
        std::unordered_map<StreamRequestHandle, RecordEntry> m_Records;
        std::unordered_map<std::string, StreamRequestHandle> m_KeyLookup;
        std::deque<StreamRequestHandle> m_QueuedHandles;
        std::deque<CompletedJob> m_CompletedJobs;
        StreamRequestHandle m_NextHandle = 1;
        std::uint64_t m_FrameIndex = 0;
        std::atomic<std::uint32_t> m_InFlightRequests { 0 };
        std::uint64_t m_CompletedRequestCount = 0;
        std::uint64_t m_EvictionCount = 0;
        std::uint64_t m_ResidentCpuBytes = 0;
        std::uint64_t m_ResidentGpuBytes = 0;
    };
}
