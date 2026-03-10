#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Luma::Assets
{
    using StreamRequestHandle = std::uint64_t;

    enum class StreamResourceType : std::uint8_t
    {
        Unknown = 0,
        Texture,
        Mesh,
        Audio,
        Buffer,
        PackageBlob,
        Procedural
    };

    enum class StreamPriority : std::uint8_t
    {
        Background = 0,
        Low,
        Normal,
        High,
        Critical
    };

    enum class StreamState : std::uint8_t
    {
        Queued = 0,
        Streaming,
        Resident,
        Evicted,
        Failed,
        Cancelled
    };

    enum class StreamEventType : std::uint8_t
    {
        Queued = 0,
        Started,
        Completed,
        Retargeted,
        Failed,
        Cancelled,
        Evicted,
        Released,
        BudgetUpdated
    };

    enum class LODStreamingMode : std::uint8_t
    {
        Disabled = 0,
        Explicit,
        Auto
    };

    struct StreamingBudget
    {
        std::uint64_t maxCpuResidentBytes = 256ull * 1024ull * 1024ull;
        std::uint64_t maxGpuResidentBytes = 384ull * 1024ull * 1024ull;
        std::uint32_t maxInFlightRequests = 2;
        std::uint32_t maxResidentRecords = 1024;
        bool enableEviction = true;
    };

    struct StreamingLODSettings
    {
        LODStreamingMode mode = LODStreamingMode::Explicit;
        std::uint32_t targetLod = 0;
        std::uint32_t minLod = 0;
        std::uint32_t maxLod = 4;
        bool allowLowerDetailFallback = true;
    };

    struct StreamRequestDesc
    {
        std::string key;
        std::filesystem::path sourcePath;
        StreamResourceType resourceType = StreamResourceType::Unknown;
        StreamPriority priority = StreamPriority::Normal;
        StreamingLODSettings lod {};
        bool persistent = false;
        bool evictable = true;
        bool deduplicateByKey = true;
        std::uint64_t estimatedCpuBytes = 0;
        std::uint64_t estimatedGpuBytes = 0;
    };

    struct StreamPayload
    {
        std::filesystem::path resolvedSourcePath;
        std::vector<std::uint8_t> bytes;
        std::uint64_t residentCpuBytes = 0;
        std::uint64_t residentGpuBytes = 0;
        std::uint32_t resolvedLod = 0;
        bool fromFallbackLod = false;
        std::string contentTag;
    };

    struct StreamRecord
    {
        StreamRequestHandle handle = 0;
        std::string key;
        std::filesystem::path sourcePath;
        std::filesystem::path resolvedSourcePath;
        StreamResourceType resourceType = StreamResourceType::Unknown;
        StreamPriority priority = StreamPriority::Normal;
        StreamState state = StreamState::Queued;
        std::uint32_t targetLod = 0;
        std::uint32_t resolvedLod = 0;
        std::uint64_t residentCpuBytes = 0;
        std::uint64_t residentGpuBytes = 0;
        std::uint64_t lastTouchedFrame = 0;
        bool persistent = false;
        bool evictable = true;
        bool fromFallbackLod = false;
        std::string lastError;
    };

    struct StreamEvent
    {
        StreamEventType type = StreamEventType::Queued;
        StreamRequestHandle handle = 0;
        std::string key;
        StreamState state = StreamState::Queued;
        float progress01 = 0.0f;
        std::string message;
    };

    struct StreamingStats
    {
        std::uint32_t queuedCount = 0;
        std::uint32_t inFlightCount = 0;
        std::uint32_t residentCount = 0;
        std::uint32_t evictedCount = 0;
        std::uint32_t failedCount = 0;
        std::uint32_t cancelledCount = 0;
        std::uint64_t completedRequestCount = 0;
        std::uint64_t evictionCount = 0;
        std::uint64_t residentCpuBytes = 0;
        std::uint64_t residentGpuBytes = 0;
        StreamingBudget budget {};
    };

    using StreamEventCallback = std::function<void(const StreamEvent&)>;
}
