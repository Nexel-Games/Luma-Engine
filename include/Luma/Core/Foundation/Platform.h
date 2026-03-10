#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace Luma
{
    enum class PlatformType : std::uint8_t
    {
        Windows,
        Linux,
        MacOS,
        Unknown
    };

    struct PlatformInfo
    {
        PlatformType type = PlatformType::Unknown;
        std::string name = "Unknown";
        std::uint32_t logicalCoreCount = 0;
        std::uint64_t pageSizeBytes = 0;
    };

    class Platform
    {
    public:
        static const PlatformInfo& GetInfo();
        static std::filesystem::path GetExecutablePath();
        static std::uint32_t GetProcessId();
        static void SleepMilliseconds(std::uint32_t milliseconds);
    };
}
