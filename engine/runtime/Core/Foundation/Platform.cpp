#include "Luma/Core/Foundation/Platform.h"

#include <chrono>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/types.h>
#include <unistd.h>
#elif defined(__linux__)
#include <sys/types.h>
#include <unistd.h>
#endif

namespace Luma
{
    namespace
    {
        PlatformInfo CreatePlatformInfo()
        {
            PlatformInfo info;
#ifdef _WIN32
            info.type = PlatformType::Windows;
            info.name = "Windows";
            SYSTEM_INFO systemInfo {};
            GetSystemInfo(&systemInfo);
            info.logicalCoreCount = systemInfo.dwNumberOfProcessors;
            info.pageSizeBytes = static_cast<std::uint64_t>(systemInfo.dwPageSize);
#elif defined(__APPLE__)
            info.type = PlatformType::MacOS;
            info.name = "macOS";
            info.logicalCoreCount = static_cast<std::uint32_t>(std::thread::hardware_concurrency());
            const long pageSize = sysconf(_SC_PAGESIZE);
            info.pageSizeBytes = pageSize > 0 ? static_cast<std::uint64_t>(pageSize) : 0;
#elif defined(__linux__)
            info.type = PlatformType::Linux;
            info.name = "Linux";
            info.logicalCoreCount = static_cast<std::uint32_t>(std::thread::hardware_concurrency());
            const long pageSize = sysconf(_SC_PAGESIZE);
            info.pageSizeBytes = pageSize > 0 ? static_cast<std::uint64_t>(pageSize) : 0;
#else
            info.type = PlatformType::Unknown;
            info.name = "Unknown";
            info.logicalCoreCount = static_cast<std::uint32_t>(std::thread::hardware_concurrency());
            info.pageSizeBytes = 0;
#endif
            if (info.logicalCoreCount == 0)
            {
                info.logicalCoreCount = 1;
            }

            return info;
        }
    }

    const PlatformInfo& Platform::GetInfo()
    {
        static const PlatformInfo info = CreatePlatformInfo();
        return info;
    }

    std::filesystem::path Platform::GetExecutablePath()
    {
#ifdef _WIN32
        char pathBuffer[MAX_PATH] {};
        const DWORD length = GetModuleFileNameA(nullptr, pathBuffer, MAX_PATH);
        if (length > 0)
        {
            return std::filesystem::path(std::string(pathBuffer, pathBuffer + length));
        }
        return {};
#elif defined(__APPLE__)
        std::uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        if (size == 0)
        {
            return {};
        }

        std::string buffer;
        buffer.resize(size);
        if (_NSGetExecutablePath(buffer.data(), &size) == 0)
        {
            return std::filesystem::path(buffer.c_str());
        }
        return {};
#elif defined(__linux__)
        char pathBuffer[4096] {};
        const ssize_t length = readlink("/proc/self/exe", pathBuffer, sizeof(pathBuffer));
        if (length > 0)
        {
            return std::filesystem::path(std::string(pathBuffer, pathBuffer + length));
        }
        return {};
#else
        return {};
#endif
    }

    std::uint32_t Platform::GetProcessId()
    {
#ifdef _WIN32
        return static_cast<std::uint32_t>(::GetCurrentProcessId());
#else
        return static_cast<std::uint32_t>(::getpid());
#endif
    }

    void Platform::SleepMilliseconds(const std::uint32_t milliseconds)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
}
