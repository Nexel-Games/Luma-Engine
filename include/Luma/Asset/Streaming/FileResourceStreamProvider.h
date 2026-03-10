#pragma once

#include "Luma/Asset/Streaming/IResourceStreamProvider.h"

namespace Luma::Assets
{
    class FileResourceStreamProvider final : public IResourceStreamProvider
    {
    public:
        std::string_view GetProviderId() const override;
        bool CanStream(const StreamRequestDesc& request) const override;
        bool Stream(
            const StreamRequestDesc& request,
            std::uint32_t workerIndex,
            StreamPayload& outPayload,
            std::string& outError) override;
    };
}
