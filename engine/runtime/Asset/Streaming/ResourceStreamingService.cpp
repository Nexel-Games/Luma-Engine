#include "Luma/Asset/Streaming/ResourceStreamingService.h"

#include <algorithm>
#include <utility>

#include "Luma/Core/Foundation/Jobs.h"

namespace Luma::Assets
{
    namespace
    {
        constexpr std::uint64_t kUnlimitedBudget = 0;

        bool IsOverBudget(const std::uint64_t current, const std::uint64_t limit)
        {
            return limit != kUnlimitedBudget && current > limit;
        }
    }

    ResourceStreamingService::ResourceStreamingService() = default;

    ResourceStreamingService::~ResourceStreamingService()
    {
        Shutdown();
    }

    bool ResourceStreamingService::Initialize(const std::filesystem::path& projectRoot, std::string& outError)
    {
        std::scoped_lock lock(m_Mutex);
        if (m_Initialized)
        {
            return true;
        }

        std::error_code absoluteError;
        const std::filesystem::path normalizedRoot = std::filesystem::absolute(projectRoot, absoluteError);
        if (absoluteError)
        {
            outError = "Failed to resolve project root for streaming service.";
            return false;
        }

        m_ProjectRoot = normalizedRoot.lexically_normal();
        m_Initialized = true;
        m_ShuttingDown.store(false, std::memory_order_release);
        return true;
    }

    void ResourceStreamingService::Shutdown()
    {
        if (!m_Initialized)
        {
            return;
        }

        m_ShuttingDown.store(true, std::memory_order_release);
        WaitIdle();

        std::scoped_lock lock(m_Mutex);
        m_Providers.clear();
        m_Records.clear();
        m_KeyLookup.clear();
        m_QueuedHandles.clear();
        m_CompletedJobs.clear();
        m_NextHandle = 1;
        m_FrameIndex = 0;
        m_CompletedRequestCount = 0;
        m_EvictionCount = 0;
        m_ResidentCpuBytes = 0;
        m_ResidentGpuBytes = 0;
        m_InFlightRequests.store(0, std::memory_order_release);
        m_Initialized = false;
    }

    void ResourceStreamingService::Tick()
    {
        if (!m_Initialized)
        {
            return;
        }

        {
            std::scoped_lock lock(m_Mutex);
            ++m_FrameIndex;
        }

        ConsumeCompletedJobs();
        DispatchQueuedJobs();
    }

    void ResourceStreamingService::WaitIdle()
    {
        if (!m_Initialized)
        {
            return;
        }

        JobSystem::WaitIdle();
        ConsumeCompletedJobs();
    }

    void ResourceStreamingService::RegisterProvider(std::shared_ptr<IResourceStreamProvider> provider)
    {
        if (!provider)
        {
            return;
        }

        std::scoped_lock lock(m_Mutex);
        m_Providers.emplace_back(std::move(provider));
    }

    void ResourceStreamingService::SetEventCallback(StreamEventCallback callback)
    {
        std::scoped_lock lock(m_Mutex);
        m_EventCallback = std::move(callback);
    }

    StreamRequestHandle ResourceStreamingService::Request(const StreamRequestDesc& request, std::string& outError)
    {
        if (!m_Initialized)
        {
            outError = "Streaming service is not initialized.";
            return 0;
        }

        StreamRequestDesc normalizedRequest = request;
        if (normalizedRequest.sourcePath.empty())
        {
            outError = "Streaming request requires a source path.";
            return 0;
        }

        if (normalizedRequest.key.empty())
        {
            normalizedRequest.key = normalizedRequest.sourcePath.generic_string();
        }

        if (normalizedRequest.sourcePath.is_relative())
        {
            normalizedRequest.sourcePath = (m_ProjectRoot / normalizedRequest.sourcePath).lexically_normal();
        }
        else
        {
            normalizedRequest.sourcePath = normalizedRequest.sourcePath.lexically_normal();
        }

        normalizedRequest.lod.targetLod = std::clamp(
            normalizedRequest.lod.targetLod,
            normalizedRequest.lod.minLod,
            normalizedRequest.lod.maxLod);

        StreamRequestHandle handle = 0;
        bool emitQueued = false;
        bool emitRetargeted = false;
        std::string eventKey = normalizedRequest.key;
        {
            std::scoped_lock lock(m_Mutex);

            if (normalizedRequest.deduplicateByKey)
            {
                const auto existingIt = m_KeyLookup.find(normalizedRequest.key);
                if (existingIt != m_KeyLookup.end())
                {
                    const auto recordIt = m_Records.find(existingIt->second);
                    if (recordIt != m_Records.end())
                    {
                        RecordEntry& entry = recordIt->second;
                        handle = entry.record.handle;
                        entry.request.priority = normalizedRequest.priority;
                        entry.request.persistent = normalizedRequest.persistent;
                        entry.request.evictable = normalizedRequest.evictable;
                        entry.request.lod = normalizedRequest.lod;
                        entry.record.priority = normalizedRequest.priority;
                        entry.record.persistent = normalizedRequest.persistent;
                        entry.record.evictable = normalizedRequest.evictable;
                        entry.record.targetLod = normalizedRequest.lod.targetLod;
                        entry.record.lastTouchedFrame = m_FrameIndex;

                        if (entry.record.state == StreamState::Failed ||
                            entry.record.state == StreamState::Cancelled ||
                            entry.record.state == StreamState::Evicted)
                        {
                            entry.record.state = StreamState::Queued;
                            m_QueuedHandles.push_back(handle);
                            emitQueued = true;
                        }
                        else if (entry.record.state == StreamState::Resident &&
                                 entry.record.resolvedLod != normalizedRequest.lod.targetLod)
                        {
                            m_ResidentCpuBytes -= std::min(m_ResidentCpuBytes, entry.record.residentCpuBytes);
                            m_ResidentGpuBytes -= std::min(m_ResidentGpuBytes, entry.record.residentGpuBytes);
                            entry.record.state = StreamState::Queued;
                            entry.record.residentCpuBytes = 0;
                            entry.record.residentGpuBytes = 0;
                            entry.record.resolvedLod = 0;
                            entry.record.resolvedSourcePath.clear();
                            entry.record.fromFallbackLod = false;
                            entry.payload = {};
                            m_QueuedHandles.push_back(handle);
                            emitRetargeted = true;
                        }
                    }
                }
            }

            if (handle == 0)
            {
                handle = m_NextHandle++;
                RecordEntry entry {};
                entry.record.handle = handle;
                entry.record.key = normalizedRequest.key;
                entry.record.sourcePath = normalizedRequest.sourcePath;
                entry.record.resourceType = normalizedRequest.resourceType;
                entry.record.priority = normalizedRequest.priority;
                entry.record.state = StreamState::Queued;
                entry.record.targetLod = normalizedRequest.lod.targetLod;
                entry.record.lastTouchedFrame = m_FrameIndex;
                entry.record.persistent = normalizedRequest.persistent;
                entry.record.evictable = normalizedRequest.evictable;
                entry.request = normalizedRequest;
                m_KeyLookup[normalizedRequest.key] = handle;
                m_QueuedHandles.push_back(handle);
                m_Records.emplace(handle, std::move(entry));
                emitQueued = true;
            }
        }

        if (emitRetargeted)
        {
            EmitEvent(StreamEventType::Retargeted, handle, eventKey, StreamState::Queued, 0.0f, "LOD request updated.");
        }
        else if (emitQueued)
        {
            EmitEvent(StreamEventType::Queued, handle, eventKey, StreamState::Queued, 0.0f, "Streaming request queued.");
        }

        return handle;
    }

    bool ResourceStreamingService::RetargetLOD(
        const StreamRequestHandle handle,
        const std::uint32_t targetLod,
        std::string& outError)
    {
        if (!m_Initialized)
        {
            outError = "Streaming service is not initialized.";
            return false;
        }

        std::string key;
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Records.find(handle);
            if (it == m_Records.end())
            {
                outError = "Unknown streaming handle.";
                return false;
            }

            RecordEntry& entry = it->second;
            const std::uint32_t clampedLod = std::clamp(targetLod, entry.request.lod.minLod, entry.request.lod.maxLod);
            if (entry.record.targetLod == clampedLod)
            {
                return true;
            }

            entry.request.lod.targetLod = clampedLod;
            entry.record.targetLod = clampedLod;
            entry.record.lastTouchedFrame = m_FrameIndex;
            key = entry.record.key;

            if (entry.record.state == StreamState::Resident)
            {
                m_ResidentCpuBytes -= std::min(m_ResidentCpuBytes, entry.record.residentCpuBytes);
                m_ResidentGpuBytes -= std::min(m_ResidentGpuBytes, entry.record.residentGpuBytes);
                entry.record.residentCpuBytes = 0;
                entry.record.residentGpuBytes = 0;
                entry.record.resolvedLod = 0;
                entry.record.resolvedSourcePath.clear();
                entry.record.fromFallbackLod = false;
                entry.payload = {};
                entry.record.state = StreamState::Queued;
                m_QueuedHandles.push_back(handle);
            }
            else if (entry.record.state == StreamState::Evicted || entry.record.state == StreamState::Failed)
            {
                entry.record.state = StreamState::Queued;
                m_QueuedHandles.push_back(handle);
            }
        }

        EmitEvent(StreamEventType::Retargeted, handle, key, StreamState::Queued, 0.0f, "LOD target changed.");
        return true;
    }

    bool ResourceStreamingService::Cancel(const StreamRequestHandle handle)
    {
        if (!m_Initialized)
        {
            return false;
        }

        std::string key;
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Records.find(handle);
            if (it == m_Records.end())
            {
                return false;
            }

            RecordEntry& entry = it->second;
            key = entry.record.key;
            if (entry.record.state == StreamState::Resident)
            {
                m_ResidentCpuBytes -= std::min(m_ResidentCpuBytes, entry.record.residentCpuBytes);
                m_ResidentGpuBytes -= std::min(m_ResidentGpuBytes, entry.record.residentGpuBytes);
            }
            entry.record.state = StreamState::Cancelled;
            entry.record.residentCpuBytes = 0;
            entry.record.residentGpuBytes = 0;
            entry.record.resolvedLod = 0;
            entry.record.resolvedSourcePath.clear();
            entry.record.fromFallbackLod = false;
            entry.record.lastError.clear();
            entry.payload = {};
        }

        EmitEvent(StreamEventType::Cancelled, handle, key, StreamState::Cancelled, 1.0f, "Streaming request cancelled.");
        return true;
    }

    bool ResourceStreamingService::Release(const StreamRequestHandle handle)
    {
        if (!m_Initialized)
        {
            return false;
        }

        std::string key;
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_Records.find(handle);
            if (it == m_Records.end())
            {
                return false;
            }

            key = it->second.record.key;
            if (it->second.record.state == StreamState::Resident)
            {
                m_ResidentCpuBytes -= std::min(m_ResidentCpuBytes, it->second.record.residentCpuBytes);
                m_ResidentGpuBytes -= std::min(m_ResidentGpuBytes, it->second.record.residentGpuBytes);
            }

            m_KeyLookup.erase(key);
            m_Records.erase(it);
        }

        EmitEvent(StreamEventType::Released, handle, key, StreamState::Evicted, 1.0f, "Streaming record released.");
        return true;
    }

    bool ResourceStreamingService::ReleaseByKey(const std::string_view key)
    {
        StreamRequestHandle handle = 0;
        {
            std::scoped_lock lock(m_Mutex);
            const auto it = m_KeyLookup.find(std::string(key));
            if (it == m_KeyLookup.end())
            {
                return false;
            }
            handle = it->second;
        }

        return Release(handle);
    }

    bool ResourceStreamingService::TryGetRecord(const StreamRequestHandle handle, StreamRecord& outRecord) const
    {
        std::scoped_lock lock(m_Mutex);
        const auto it = m_Records.find(handle);
        if (it == m_Records.end())
        {
            return false;
        }

        outRecord = it->second.record;
        return true;
    }

    bool ResourceStreamingService::TryGetPayload(const StreamRequestHandle handle, StreamPayload& outPayload) const
    {
        std::scoped_lock lock(m_Mutex);
        const auto it = m_Records.find(handle);
        if (it == m_Records.end() || it->second.record.state != StreamState::Resident)
        {
            return false;
        }

        outPayload = it->second.payload;
        return true;
    }

    std::vector<StreamRecord> ResourceStreamingService::GetRecords() const
    {
        std::vector<StreamRecord> records;
        std::scoped_lock lock(m_Mutex);
        records.reserve(m_Records.size());
        for (const auto& [handle, entry] : m_Records)
        {
            (void)handle;
            records.push_back(entry.record);
        }

        std::sort(
            records.begin(),
            records.end(),
            [](const StreamRecord& a, const StreamRecord& b)
            {
                if (a.priority != b.priority)
                {
                    return static_cast<int>(a.priority) > static_cast<int>(b.priority);
                }
                if (a.state != b.state)
                {
                    return static_cast<int>(a.state) < static_cast<int>(b.state);
                }
                return a.key < b.key;
            });
        return records;
    }

    void ResourceStreamingService::SetBudget(const StreamingBudget& budget)
    {
        {
            std::scoped_lock lock(m_Mutex);
            m_Budget = budget;
        }

        EmitEvent(StreamEventType::BudgetUpdated, 0, {}, StreamState::Queued, 0.0f, "Streaming budget updated.");
        ApplyBudgetConstraints();
    }

    StreamingBudget ResourceStreamingService::GetBudget() const
    {
        std::scoped_lock lock(m_Mutex);
        return m_Budget;
    }

    StreamingStats ResourceStreamingService::GetStats() const
    {
        StreamingStats stats {};
        std::scoped_lock lock(m_Mutex);
        stats.completedRequestCount = m_CompletedRequestCount;
        stats.evictionCount = m_EvictionCount;
        stats.residentCpuBytes = m_ResidentCpuBytes;
        stats.residentGpuBytes = m_ResidentGpuBytes;
        stats.budget = m_Budget;

        for (const auto& [handle, entry] : m_Records)
        {
            (void)handle;
            switch (entry.record.state)
            {
            case StreamState::Queued:
                ++stats.queuedCount;
                break;
            case StreamState::Streaming:
                ++stats.inFlightCount;
                break;
            case StreamState::Resident:
                ++stats.residentCount;
                break;
            case StreamState::Evicted:
                ++stats.evictedCount;
                break;
            case StreamState::Failed:
                ++stats.failedCount;
                break;
            case StreamState::Cancelled:
                ++stats.cancelledCount;
                break;
            }
        }

        return stats;
    }

    void ResourceStreamingService::EmitEvent(
        const StreamEventType type,
        const StreamRequestHandle handle,
        std::string key,
        const StreamState state,
        const float progress01,
        std::string message) const
    {
        StreamEventCallback callback;
        {
            std::scoped_lock lock(m_Mutex);
            callback = m_EventCallback;
        }

        if (!callback)
        {
            return;
        }

        callback(StreamEvent {
            .type = type,
            .handle = handle,
            .key = std::move(key),
            .state = state,
            .progress01 = progress01,
            .message = std::move(message) });
    }

    void ResourceStreamingService::ConsumeCompletedJobs()
    {
        std::deque<CompletedJob> completedJobs;
        {
            std::scoped_lock lock(m_Mutex);
            completedJobs.swap(m_CompletedJobs);
        }

        for (CompletedJob& completed : completedJobs)
        {
            StreamEventType eventType = StreamEventType::Completed;
            StreamState eventState = StreamState::Resident;
            std::string key;
            std::string message;
            bool emit = false;
            bool needsBudgetPass = false;

            {
                std::scoped_lock lock(m_Mutex);
                const auto it = m_Records.find(completed.handle);
                if (it == m_Records.end())
                {
                    continue;
                }

                RecordEntry& entry = it->second;
                key = entry.record.key;

                if (entry.record.state == StreamState::Cancelled)
                {
                    continue;
                }

                if (!completed.error.empty())
                {
                    entry.record.state = StreamState::Failed;
                    entry.record.lastError = completed.error;
                    entry.record.residentCpuBytes = 0;
                    entry.record.residentGpuBytes = 0;
                    entry.record.resolvedSourcePath.clear();
                    entry.record.resolvedLod = 0;
                    entry.record.fromFallbackLod = false;
                    entry.payload = {};
                    eventType = StreamEventType::Failed;
                    eventState = StreamState::Failed;
                    message = completed.error;
                    emit = true;
                }
                else if (entry.request.lod.mode != LODStreamingMode::Disabled &&
                         completed.payload.resolvedLod != entry.record.targetLod &&
                         !(completed.payload.resolvedLod == 0 && entry.record.targetLod == 0))
                {
                    entry.record.state = StreamState::Queued;
                    entry.record.lastError.clear();
                    entry.payload = {};
                    m_QueuedHandles.push_back(completed.handle);
                    eventType = StreamEventType::Retargeted;
                    eventState = StreamState::Queued;
                    message = "Loaded an older LOD while a newer request was pending. Re-queued.";
                    emit = true;
                }
                else
                {
                    if (entry.record.state == StreamState::Resident)
                    {
                        m_ResidentCpuBytes -= std::min(m_ResidentCpuBytes, entry.record.residentCpuBytes);
                        m_ResidentGpuBytes -= std::min(m_ResidentGpuBytes, entry.record.residentGpuBytes);
                    }

                    entry.payload = std::move(completed.payload);
                    entry.record.state = StreamState::Resident;
                    entry.record.resolvedSourcePath = entry.payload.resolvedSourcePath;
                    entry.record.residentCpuBytes = entry.payload.residentCpuBytes;
                    entry.record.residentGpuBytes = entry.payload.residentGpuBytes;
                    entry.record.resolvedLod = entry.payload.resolvedLod;
                    entry.record.fromFallbackLod = entry.payload.fromFallbackLod;
                    entry.record.lastError.clear();
                    entry.record.lastTouchedFrame = m_FrameIndex;
                    m_ResidentCpuBytes += entry.record.residentCpuBytes;
                    m_ResidentGpuBytes += entry.record.residentGpuBytes;
                    ++m_CompletedRequestCount;
                    eventType = StreamEventType::Completed;
                    eventState = StreamState::Resident;
                    message = "Streaming request completed.";
                    emit = true;
                    needsBudgetPass = true;
                }
            }

            if (emit)
            {
                EmitEvent(eventType, completed.handle, key, eventState, 1.0f, message);
            }
            if (needsBudgetPass)
            {
                ApplyBudgetConstraints();
            }
        }
    }

    void ResourceStreamingService::DispatchQueuedJobs()
    {
        while (true)
        {
            StreamRequestHandle handle = 0;
            StreamRequestDesc request {};
            std::string key;
            {
                std::scoped_lock lock(m_Mutex);
                const std::uint32_t maxInFlight = std::max(1u, m_Budget.maxInFlightRequests);
                if (m_InFlightRequests.load(std::memory_order_acquire) >= maxInFlight || m_QueuedHandles.empty())
                {
                    break;
                }

                auto bestIt = m_QueuedHandles.end();
                for (auto it = m_QueuedHandles.begin(); it != m_QueuedHandles.end(); ++it)
                {
                    const auto recordIt = m_Records.find(*it);
                    if (recordIt == m_Records.end() || recordIt->second.record.state != StreamState::Queued)
                    {
                        continue;
                    }

                    if (bestIt == m_QueuedHandles.end())
                    {
                        bestIt = it;
                        continue;
                    }

                    const StreamRecord& candidate = recordIt->second.record;
                    const StreamRecord& best = m_Records.at(*bestIt).record;
                    if (candidate.priority != best.priority)
                    {
                        if (static_cast<int>(candidate.priority) > static_cast<int>(best.priority))
                        {
                            bestIt = it;
                        }
                        continue;
                    }

                    if (candidate.lastTouchedFrame < best.lastTouchedFrame)
                    {
                        bestIt = it;
                    }
                }

                if (bestIt == m_QueuedHandles.end())
                {
                    m_QueuedHandles.clear();
                    break;
                }

                handle = *bestIt;
                m_QueuedHandles.erase(bestIt);

                const auto recordIt = m_Records.find(handle);
                if (recordIt == m_Records.end() || recordIt->second.record.state != StreamState::Queued)
                {
                    continue;
                }

                RecordEntry& entry = recordIt->second;
                entry.record.state = StreamState::Streaming;
                entry.record.lastTouchedFrame = m_FrameIndex;
                entry.activeLod = entry.record.targetLod;
                request = entry.request;
                request.lod.targetLod = entry.record.targetLod;
                key = entry.record.key;
            }

            std::shared_ptr<IResourceStreamProvider> provider = FindProviderForRequest(request);
            if (!provider)
            {
                {
                    std::scoped_lock lock(m_Mutex);
                    const auto it = m_Records.find(handle);
                    if (it != m_Records.end())
                    {
                        it->second.record.state = StreamState::Failed;
                        it->second.record.lastError = "No streaming provider accepted this request.";
                    }
                }
                EmitEvent(
                    StreamEventType::Failed,
                    handle,
                    key,
                    StreamState::Failed,
                    1.0f,
                    "No streaming provider accepted this request.");
                continue;
            }

            m_InFlightRequests.fetch_add(1, std::memory_order_acq_rel);
            EmitEvent(StreamEventType::Started, handle, key, StreamState::Streaming, 0.0f, "Streaming request started.");

            JobSystem::Dispatch(
                [this, handle, request, provider](const std::uint32_t workerIndex)
                {
                    StreamPayload payload {};
                    std::string error;
                    if (!provider->Stream(request, workerIndex, payload, error))
                    {
                        payload = {};
                    }

                    {
                        std::scoped_lock lock(m_Mutex);
                        m_CompletedJobs.push_back(CompletedJob {
                            .handle = handle,
                            .payload = std::move(payload),
                            .error = std::move(error) });
                    }

                    m_InFlightRequests.fetch_sub(1, std::memory_order_acq_rel);
                });
        }
    }

    void ResourceStreamingService::ApplyBudgetConstraints()
    {
        std::vector<std::pair<StreamRequestHandle, std::string>> evictedHandles;
        {
            std::scoped_lock lock(m_Mutex);
            if (!m_Budget.enableEviction)
            {
                return;
            }

            std::vector<StreamRequestHandle> candidates;
            candidates.reserve(m_Records.size());
            std::uint32_t residentCount = 0;
            for (const auto& [handle, entry] : m_Records)
            {
                if (entry.record.state == StreamState::Resident)
                {
                    ++residentCount;
                    if (entry.record.evictable && !entry.record.persistent)
                    {
                        candidates.push_back(handle);
                    }
                }
            }

            const bool overCpu = IsOverBudget(m_ResidentCpuBytes, m_Budget.maxCpuResidentBytes);
            const bool overGpu = IsOverBudget(m_ResidentGpuBytes, m_Budget.maxGpuResidentBytes);
            const bool overCount =
                m_Budget.maxResidentRecords != 0 && residentCount > m_Budget.maxResidentRecords;
            if (!overCpu && !overGpu && !overCount)
            {
                return;
            }

            std::sort(
                candidates.begin(),
                candidates.end(),
                [this](const StreamRequestHandle a, const StreamRequestHandle b)
                {
                    const StreamRecord& lhs = m_Records.at(a).record;
                    const StreamRecord& rhs = m_Records.at(b).record;
                    if (lhs.priority != rhs.priority)
                    {
                        return static_cast<int>(lhs.priority) < static_cast<int>(rhs.priority);
                    }
                    return lhs.lastTouchedFrame < rhs.lastTouchedFrame;
                });

            for (const StreamRequestHandle handle : candidates)
            {
                const bool cpuOk = !IsOverBudget(m_ResidentCpuBytes, m_Budget.maxCpuResidentBytes);
                const bool gpuOk = !IsOverBudget(m_ResidentGpuBytes, m_Budget.maxGpuResidentBytes);
                const bool countOk =
                    m_Budget.maxResidentRecords == 0 || residentCount <= m_Budget.maxResidentRecords;
                if (cpuOk && gpuOk && countOk)
                {
                    break;
                }

                auto it = m_Records.find(handle);
                if (it == m_Records.end() || it->second.record.state != StreamState::Resident)
                {
                    continue;
                }

                RecordEntry& entry = it->second;
                m_ResidentCpuBytes -= std::min(m_ResidentCpuBytes, entry.record.residentCpuBytes);
                m_ResidentGpuBytes -= std::min(m_ResidentGpuBytes, entry.record.residentGpuBytes);
                entry.record.state = StreamState::Evicted;
                entry.record.residentCpuBytes = 0;
                entry.record.residentGpuBytes = 0;
                entry.record.resolvedLod = 0;
                entry.record.resolvedSourcePath.clear();
                entry.record.fromFallbackLod = false;
                entry.payload = {};
                ++m_EvictionCount;
                if (residentCount > 0)
                {
                    --residentCount;
                }
                evictedHandles.emplace_back(handle, entry.record.key);
            }
        }

        for (const auto& [handle, key] : evictedHandles)
        {
            EmitEvent(StreamEventType::Evicted, handle, key, StreamState::Evicted, 1.0f, "Evicted to satisfy streaming budget.");
        }
    }

    std::shared_ptr<IResourceStreamProvider> ResourceStreamingService::FindProviderForRequest(const StreamRequestDesc& request) const
    {
        std::scoped_lock lock(m_Mutex);
        for (const std::shared_ptr<IResourceStreamProvider>& provider : m_Providers)
        {
            if (provider && provider->CanStream(request))
            {
                return provider;
            }
        }

        return nullptr;
    }
}
