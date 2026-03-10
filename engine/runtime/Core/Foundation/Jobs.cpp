#include "Luma/Core/Foundation/Jobs.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace Luma
{
    namespace
    {
        std::mutex g_QueueMutex;
        std::condition_variable g_WorkAvailableCondition;
        std::condition_variable g_IdleCondition;
        std::vector<std::thread> g_Workers;
        std::queue<std::function<void(std::uint32_t)>> g_JobQueue;
        std::atomic<std::uint64_t> g_PendingJobs { 0 };
        bool g_Running = false;
        bool g_StopRequested = false;

        void WorkerMain(const std::uint32_t workerIndex)
        {
            while (true)
            {
                std::function<void(std::uint32_t)> job;
                {
                    std::unique_lock lock(g_QueueMutex);
                    g_WorkAvailableCondition.wait(
                        lock,
                        []
                        {
                            return g_StopRequested || !g_JobQueue.empty();
                        });

                    if (g_StopRequested && g_JobQueue.empty())
                    {
                        return;
                    }

                    job = std::move(g_JobQueue.front());
                    g_JobQueue.pop();
                }

                job(workerIndex);

                const std::uint64_t remaining = g_PendingJobs.fetch_sub(1, std::memory_order_acq_rel) - 1;
                if (remaining == 0)
                {
                    std::scoped_lock lock(g_QueueMutex);
                    if (g_JobQueue.empty())
                    {
                        g_IdleCondition.notify_all();
                    }
                }
            }
        }
    }

    bool JobSystem::Start(std::uint32_t workerCount)
    {
        std::scoped_lock lock(g_QueueMutex);
        if (g_Running)
        {
            return true;
        }

        if (workerCount == 0)
        {
            const std::uint32_t hardwareCount = static_cast<std::uint32_t>(std::thread::hardware_concurrency());
            workerCount = hardwareCount > 1 ? hardwareCount - 1 : 1;
        }

        g_StopRequested = false;
        g_Running = true;
        g_PendingJobs.store(0, std::memory_order_release);
        while (!g_JobQueue.empty())
        {
            g_JobQueue.pop();
        }

        g_Workers.reserve(workerCount);
        for (std::uint32_t i = 0; i < workerCount; ++i)
        {
            g_Workers.emplace_back(
                [i]
                {
                    WorkerMain(i);
                });
        }

        return true;
    }

    void JobSystem::Stop()
    {
        {
            std::scoped_lock lock(g_QueueMutex);
            if (!g_Running)
            {
                return;
            }

            g_StopRequested = true;
        }

        g_WorkAvailableCondition.notify_all();
        for (std::thread& worker : g_Workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        g_Workers.clear();

        {
            std::scoped_lock lock(g_QueueMutex);
            g_Running = false;
            g_StopRequested = false;
            while (!g_JobQueue.empty())
            {
                g_JobQueue.pop();
            }
            g_PendingJobs.store(0, std::memory_order_release);
        }
    }

    bool JobSystem::IsRunning()
    {
        std::scoped_lock lock(g_QueueMutex);
        return g_Running;
    }

    std::uint32_t JobSystem::GetWorkerCount()
    {
        std::scoped_lock lock(g_QueueMutex);
        return static_cast<std::uint32_t>(g_Workers.size());
    }

    void JobSystem::Dispatch(const std::function<void(std::uint32_t workerIndex)>& job)
    {
        if (!job)
        {
            return;
        }

        bool runInline = false;
        {
            std::scoped_lock lock(g_QueueMutex);
            if (!g_Running)
            {
                runInline = true;
            }
            else
            {
                g_JobQueue.push(job);
                g_PendingJobs.fetch_add(1, std::memory_order_release);
            }
        }

        if (runInline)
        {
            job(0);
            return;
        }

        g_WorkAvailableCondition.notify_one();
    }

    void JobSystem::WaitIdle()
    {
        std::unique_lock lock(g_QueueMutex);
        g_IdleCondition.wait(
            lock,
            []
            {
                return g_PendingJobs.load(std::memory_order_acquire) == 0 && g_JobQueue.empty();
            });
    }
}
