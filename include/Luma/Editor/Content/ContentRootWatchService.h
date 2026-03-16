#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "Luma/Editor/Content/ContentBrowserController.h"

namespace Luma::Editor
{
    class ContentRootWatchService
    {
    public:
        void Reset();
        bool Poll(const std::vector<ContentBrowserRootState>& roots, float deltaTimeSeconds);

    private:
        static std::uint64_t ComputeRootFingerprint(const std::filesystem::path& rootPath);
        static std::string NormalizeRootKey(const std::filesystem::path& rootPath);

        float m_PollAccumulatorSeconds = 0.0f;
        bool m_HasSnapshot = false;
        std::unordered_map<std::string, std::uint64_t> m_RootFingerprints;
    };
}
