#pragma once

#include <cstdint>
#include <functional>

namespace Luma
{
    class JobSystem
    {
    public:
        static bool Start(std::uint32_t workerCount = 0);
        static void Stop();
        static bool IsRunning();
        static std::uint32_t GetWorkerCount();

        static void Dispatch(const std::function<void(std::uint32_t workerIndex)>& job);
        static void WaitIdle();
    };
}
