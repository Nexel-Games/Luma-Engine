#pragma once

#include <cstddef>
#include <cstdint>

namespace Luma
{
    struct MemoryStats
    {
        std::uint64_t totalAllocatedBytes = 0;
        std::uint64_t totalFreedBytes = 0;
        std::uint64_t liveBytes = 0;
        std::uint64_t allocationCount = 0;
        std::uint64_t freeCount = 0;
        std::uint64_t liveAllocations = 0;
    };

    class Memory
    {
    public:
        static void* Allocate(std::size_t sizeInBytes);
        static void Free(void* pointer);
        static MemoryStats GetStats();
        static void ResetStats();
    };
}
