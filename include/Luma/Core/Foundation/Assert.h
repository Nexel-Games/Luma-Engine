#pragma once

#include <cstdlib>
#include <sstream>

#include "Luma/Core/Foundation/Logging.h"

namespace Luma
{
    namespace Detail
    {
        [[noreturn]] inline void HandleAssertFailure(
            const char* expression,
            const char* file,
            int line,
            const char* function,
            const char* message)
        {
            std::ostringstream out;
            out << "Assertion failed: " << expression;
            if (message != nullptr && message[0] != '\0')
            {
                out << " | " << message;
            }
            out << " @ " << file << ':' << line << " (" << function << ')';
            LUMA_LOG_FATAL("Assert", out.str());
#ifdef _WIN32
            __debugbreak();
#endif
            std::abort();
        }
    }
}

#define LUMA_ASSERT(condition, message)                                                                      \
    do                                                                                                       \
    {                                                                                                        \
        if (!(condition))                                                                                    \
        {                                                                                                    \
            ::Luma::Detail::HandleAssertFailure(#condition, __FILE__, __LINE__, __func__, (message));      \
        }                                                                                                    \
    } while (false)

#define LUMA_CORE_ASSERT(condition, message) LUMA_ASSERT(condition, message)
