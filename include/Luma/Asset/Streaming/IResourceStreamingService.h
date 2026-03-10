#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Luma/Asset/Streaming/IResourceStreamProvider.h"

namespace Luma::Assets
{
    class IResourceStreamingService
    {
    public:
        virtual ~IResourceStreamingService() = default;

        virtual bool Initialize(const std::filesystem::path& projectRoot, std::string& outError) = 0;
        virtual void Shutdown() = 0;
        virtual void Tick() = 0;
        virtual void WaitIdle() = 0;

        virtual void RegisterProvider(std::shared_ptr<IResourceStreamProvider> provider) = 0;
        virtual void SetEventCallback(StreamEventCallback callback) = 0;

        virtual StreamRequestHandle Request(const StreamRequestDesc& request, std::string& outError) = 0;
        virtual bool RetargetLOD(StreamRequestHandle handle, std::uint32_t targetLod, std::string& outError) = 0;
        virtual bool Cancel(StreamRequestHandle handle) = 0;
        virtual bool Release(StreamRequestHandle handle) = 0;
        virtual bool ReleaseByKey(std::string_view key) = 0;

        virtual bool TryGetRecord(StreamRequestHandle handle, StreamRecord& outRecord) const = 0;
        virtual bool TryGetPayload(StreamRequestHandle handle, StreamPayload& outPayload) const = 0;
        virtual std::vector<StreamRecord> GetRecords() const = 0;

        virtual void SetBudget(const StreamingBudget& budget) = 0;
        virtual StreamingBudget GetBudget() const = 0;
        virtual StreamingStats GetStats() const = 0;
    };
}
