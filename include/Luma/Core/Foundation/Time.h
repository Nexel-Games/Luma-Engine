#pragma once

#include <cstdint>

namespace Luma
{
    class Time
    {
    public:
        static void Initialize(double fixedDeltaSeconds = 1.0 / 60.0);
        static void BeginFrame();
        static double GetDeltaSeconds();
        static double GetUnscaledDeltaSeconds();
        static double GetElapsedSeconds();
        static std::uint64_t GetFrameIndex();
        static double GetFixedDeltaSeconds();
        static bool ConsumeFixedStep();
    };
}
