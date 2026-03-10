#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Luma/Asset/Streaming/ResourceStreamingTypes.h"

namespace Luma::Assets
{
    class IResourceStreamProvider
    {
    public:
        virtual ~IResourceStreamProvider() = default;

        virtual std::string_view GetProviderId() const = 0;
        virtual bool CanStream(const StreamRequestDesc& request) const = 0;
        virtual bool Stream(
            const StreamRequestDesc& request,
            std::uint32_t workerIndex,
            StreamPayload& outPayload,
            std::string& outError) = 0;
    };
}
