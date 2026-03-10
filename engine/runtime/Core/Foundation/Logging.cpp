#include "Luma/Core/Foundation/Logging.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <vector>

namespace Luma
{
    namespace
    {
        std::mutex g_LogMutex;
        std::ofstream g_LogFile;
        LogLevel g_LogLevel = LogLevel::Info;
        bool g_Initialized = false;
        std::unordered_map<std::uint64_t, Logger::SinkCallback> g_Sinks;
        std::uint64_t g_NextSinkId = 1;

        const char* LogLevelLabel(const LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Trace:
                return "Trace";
            case LogLevel::Info:
                return "Info";
            case LogLevel::Warn:
                return "Warn";
            case LogLevel::Error:
                return "Error";
            case LogLevel::Fatal:
                return "Fatal";
            default:
                return "Info";
            }
        }

        std::string CurrentTimestamp()
        {
            const auto now = std::chrono::system_clock::now();
            const std::time_t time = std::chrono::system_clock::to_time_t(now);
            std::tm localTime {};
#ifdef _WIN32
            localtime_s(&localTime, &time);
#else
            localtime_r(&time, &localTime);
#endif

            std::ostringstream timestamp;
            timestamp << std::put_time(&localTime, "%H:%M:%S");
            return timestamp.str();
        }
    }

    bool Logger::Initialize(const std::string& outputFile)
    {
        std::scoped_lock lock(g_LogMutex);
        if (!outputFile.empty())
        {
            g_LogFile.open(outputFile, std::ios::app);
            if (!g_LogFile.is_open())
            {
                return false;
            }
        }

        g_Initialized = true;
        return true;
    }

    void Logger::Shutdown()
    {
        std::scoped_lock lock(g_LogMutex);
        if (g_LogFile.is_open())
        {
            g_LogFile.flush();
            g_LogFile.close();
        }

        g_Initialized = false;
    }

    void Logger::SetLevel(const LogLevel level)
    {
        std::scoped_lock lock(g_LogMutex);
        g_LogLevel = level;
    }

    LogLevel Logger::GetLevel()
    {
        std::scoped_lock lock(g_LogMutex);
        return g_LogLevel;
    }

    void Logger::Log(const LogLevel level, const std::string_view category, const std::string_view message)
    {
        std::vector<SinkCallback> sinks;
        std::string categoryCopy;
        std::string messageCopy;
        std::string lineString;
        {
            std::scoped_lock lock(g_LogMutex);
            if (static_cast<int>(level) < static_cast<int>(g_LogLevel))
            {
                return;
            }

            categoryCopy = std::string(category);
            messageCopy = std::string(message);

            std::ostringstream line;
            line
                << '[' << CurrentTimestamp() << ']'
                << '[' << LogLevelLabel(level) << ']'
                << '[' << categoryCopy << ']'
                << "[T" << std::this_thread::get_id() << "] "
                << messageCopy;
            lineString = line.str();

            std::ostream& outputStream =
                (level == LogLevel::Warn || level == LogLevel::Error || level == LogLevel::Fatal)
                    ? std::cerr
                    : std::cout;
            outputStream << lineString << '\n';

            if (g_LogFile.is_open())
            {
                g_LogFile << lineString << '\n';
                g_LogFile.flush();
            }

            if (!g_Initialized)
            {
                outputStream.flush();
            }

            sinks.reserve(g_Sinks.size());
            for (const auto& [sinkId, sink] : g_Sinks)
            {
                (void)sinkId;
                if (sink)
                {
                    sinks.push_back(sink);
                }
            }
        }

        for (const auto& sink : sinks)
        {
            sink(level, categoryCopy, messageCopy, lineString);
        }
    }

    std::uint64_t Logger::RegisterSink(SinkCallback callback)
    {
        if (!callback)
        {
            return 0;
        }

        std::scoped_lock lock(g_LogMutex);
        const std::uint64_t sinkId = g_NextSinkId++;
        g_Sinks.emplace(sinkId, std::move(callback));
        return sinkId;
    }

    bool Logger::UnregisterSink(const std::uint64_t sinkId)
    {
        if (sinkId == 0)
        {
            return false;
        }

        std::scoped_lock lock(g_LogMutex);
        const auto it = g_Sinks.find(sinkId);
        if (it == g_Sinks.end())
        {
            return false;
        }

        g_Sinks.erase(it);
        return true;
    }
}
