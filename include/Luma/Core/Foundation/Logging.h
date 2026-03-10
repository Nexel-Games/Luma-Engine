#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace Luma
{
    enum class LogLevel : int
    {
        Trace = 0,
        Info,
        Warn,
        Error,
        Fatal
    };

    class Logger
    {
    public:
        using SinkCallback = std::function<void(LogLevel level, std::string_view category, std::string_view message, std::string_view line)>;

        static bool Initialize(const std::string& outputFile = {});
        static void Shutdown();
        static void SetLevel(LogLevel level);
        static LogLevel GetLevel();
        static void Log(LogLevel level, std::string_view category, std::string_view message);
        static std::uint64_t RegisterSink(SinkCallback callback);
        static bool UnregisterSink(std::uint64_t sinkId);
    };
}

#define LUMA_LOG_TRACE(category, message) ::Luma::Logger::Log(::Luma::LogLevel::Trace, category, message)
#define LUMA_LOG_INFO(category, message) ::Luma::Logger::Log(::Luma::LogLevel::Info, category, message)
#define LUMA_LOG_WARN(category, message) ::Luma::Logger::Log(::Luma::LogLevel::Warn, category, message)
#define LUMA_LOG_ERROR(category, message) ::Luma::Logger::Log(::Luma::LogLevel::Error, category, message)
#define LUMA_LOG_FATAL(category, message) ::Luma::Logger::Log(::Luma::LogLevel::Fatal, category, message)
