#include "Luma/Core/Foundation/Time.h"

#include <algorithm>
#include <chrono>

namespace Luma
{
    namespace
    {
        using clock = std::chrono::steady_clock;

        bool g_Initialized = false;
        clock::time_point g_LastFrameTime {};
        double g_UnscaledDeltaSeconds = 0.0;
        double g_DeltaSeconds = 0.0;
        double g_ElapsedSeconds = 0.0;
        double g_FixedDeltaSeconds = 1.0 / 60.0;
        double g_FixedAccumulatorSeconds = 0.0;
        std::uint64_t g_FrameIndex = 0;
    }

    void Time::Initialize(const double fixedDeltaSeconds)
    {
        g_FixedDeltaSeconds = std::max(fixedDeltaSeconds, 1.0e-6);
        g_FixedAccumulatorSeconds = 0.0;
        g_UnscaledDeltaSeconds = 0.0;
        g_DeltaSeconds = 0.0;
        g_ElapsedSeconds = 0.0;
        g_FrameIndex = 0;
        g_LastFrameTime = clock::now();
        g_Initialized = true;
    }

    void Time::BeginFrame()
    {
        if (!g_Initialized)
        {
            Initialize(g_FixedDeltaSeconds);
        }

        const clock::time_point now = clock::now();
        const std::chrono::duration<double> frameDelta = now - g_LastFrameTime;
        g_LastFrameTime = now;

        g_UnscaledDeltaSeconds = std::clamp(frameDelta.count(), 0.0, 0.25);
        g_DeltaSeconds = g_UnscaledDeltaSeconds;
        g_ElapsedSeconds += g_DeltaSeconds;
        g_FixedAccumulatorSeconds += g_DeltaSeconds;
        ++g_FrameIndex;
    }

    double Time::GetDeltaSeconds()
    {
        return g_DeltaSeconds;
    }

    double Time::GetUnscaledDeltaSeconds()
    {
        return g_UnscaledDeltaSeconds;
    }

    double Time::GetElapsedSeconds()
    {
        return g_ElapsedSeconds;
    }

    std::uint64_t Time::GetFrameIndex()
    {
        return g_FrameIndex;
    }

    double Time::GetFixedDeltaSeconds()
    {
        return g_FixedDeltaSeconds;
    }

    bool Time::ConsumeFixedStep()
    {
        if (g_FixedAccumulatorSeconds + 1.0e-9 < g_FixedDeltaSeconds)
        {
            return false;
        }

        g_FixedAccumulatorSeconds -= g_FixedDeltaSeconds;
        return true;
    }
}
