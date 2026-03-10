#include "Luma/Core/Foundation/Memory.h"

#include <cstdlib>
#include <mutex>
#include <unordered_map>

namespace Luma
{
    namespace
    {
        std::mutex g_MemoryMutex;
        std::unordered_map<void*, std::size_t> g_Allocations;
        MemoryStats g_Stats {};
    }

    void* Memory::Allocate(const std::size_t sizeInBytes)
    {
        if (sizeInBytes == 0)
        {
            return nullptr;
        }

        void* memory = std::malloc(sizeInBytes);
        if (memory == nullptr)
        {
            return nullptr;
        }

        std::scoped_lock lock(g_MemoryMutex);
        g_Allocations[memory] = sizeInBytes;
        g_Stats.totalAllocatedBytes += static_cast<std::uint64_t>(sizeInBytes);
        g_Stats.liveBytes += static_cast<std::uint64_t>(sizeInBytes);
        ++g_Stats.allocationCount;
        ++g_Stats.liveAllocations;
        return memory;
    }

    void Memory::Free(void* pointer)
    {
        if (pointer == nullptr)
        {
            return;
        }

        std::size_t sizeInBytes = 0;
        {
            std::scoped_lock lock(g_MemoryMutex);
            const auto it = g_Allocations.find(pointer);
            if (it != g_Allocations.end())
            {
                sizeInBytes = it->second;
                g_Allocations.erase(it);
            }

            if (sizeInBytes > 0)
            {
                g_Stats.totalFreedBytes += static_cast<std::uint64_t>(sizeInBytes);
                if (g_Stats.liveBytes >= sizeInBytes)
                {
                    g_Stats.liveBytes -= static_cast<std::uint64_t>(sizeInBytes);
                }
            }

            ++g_Stats.freeCount;
            if (g_Stats.liveAllocations > 0)
            {
                --g_Stats.liveAllocations;
            }
        }

        std::free(pointer);
    }

    MemoryStats Memory::GetStats()
    {
        std::scoped_lock lock(g_MemoryMutex);
        return g_Stats;
    }

    void Memory::ResetStats()
    {
        std::scoped_lock lock(g_MemoryMutex);
        g_Allocations.clear();
        g_Stats = {};
    }
}
