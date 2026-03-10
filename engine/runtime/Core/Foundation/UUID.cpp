#include "Luma/Core/Foundation/UUID.h"

#include <atomic>
#include <random>

namespace Luma
{
    UUID GenerateUUID()
    {
        static std::atomic<UUID> fallbackCounter { 1 };
        static thread_local std::mt19937_64 generator { std::random_device {}() };
        static thread_local std::uniform_int_distribution<UUID> distribution;

        UUID id = distribution(generator);
        if (id == 0)
        {
            id = fallbackCounter.fetch_add(1, std::memory_order_relaxed);
        }

        return id;
    }
}
