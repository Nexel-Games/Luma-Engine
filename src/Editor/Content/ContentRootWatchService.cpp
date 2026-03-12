#include "Luma/Editor/Content/ContentRootWatchService.h"

#include <algorithm>
#include <array>
#include <system_error>
#include <utility>

namespace Luma::Editor
{
    namespace
    {
        constexpr float kPollIntervalSeconds = 1.0f;
        constexpr std::uint64_t kFnvOffsetBasis = 1469598103934665603ull;
        constexpr std::uint64_t kFnvPrime = 1099511628211ull;

        void HashBytes(std::uint64_t& hash, const void* bytes, const std::size_t byteCount)
        {
            const auto* data = static_cast<const std::uint8_t*>(bytes);
            for (std::size_t index = 0; index < byteCount; ++index)
            {
                hash ^= static_cast<std::uint64_t>(data[index]);
                hash *= kFnvPrime;
            }
        }

        void HashString(std::uint64_t& hash, std::string value)
        {
#if defined(_WIN32)
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
#endif
            HashBytes(hash, value.data(), value.size());
        }

        std::filesystem::path NormalizePath(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return {};
            }

            std::error_code ec;
            std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
            if (ec)
            {
                normalized = path.lexically_normal();
            }

            return normalized.lexically_normal();
        }
    }

    void ContentRootWatchService::Reset()
    {
        m_PollAccumulatorSeconds = 0.0f;
        m_HasSnapshot = false;
        m_RootFingerprints.clear();
    }

    bool ContentRootWatchService::Poll(const std::vector<ContentBrowserRootState>& roots, const float deltaTimeSeconds)
    {
        if (roots.empty())
        {
            Reset();
            return false;
        }

        m_PollAccumulatorSeconds += std::max(deltaTimeSeconds, 0.0f);
        if (m_PollAccumulatorSeconds < kPollIntervalSeconds)
        {
            return false;
        }
        m_PollAccumulatorSeconds = 0.0f;

        std::unordered_map<std::string, std::uint64_t> nextFingerprints;
        nextFingerprints.reserve(roots.size());
        for (const ContentBrowserRootState& root : roots)
        {
            if (root.path.empty())
            {
                continue;
            }

            nextFingerprints.emplace(NormalizeRootKey(root.path), ComputeRootFingerprint(root.path));
        }

        if (!m_HasSnapshot)
        {
            m_RootFingerprints = std::move(nextFingerprints);
            m_HasSnapshot = true;
            return false;
        }

        if (nextFingerprints == m_RootFingerprints)
        {
            return false;
        }

        m_RootFingerprints = std::move(nextFingerprints);
        return true;
    }

    std::uint64_t ContentRootWatchService::ComputeRootFingerprint(const std::filesystem::path& rootPath)
    {
        const std::filesystem::path normalizedRoot = NormalizePath(rootPath);
        std::uint64_t hash = kFnvOffsetBasis;
        HashString(hash, normalizedRoot.generic_string());

        std::error_code ec;
        if (normalizedRoot.empty() || !std::filesystem::exists(normalizedRoot, ec))
        {
            static constexpr std::array<char, 7> missingMarker = { 'm', 'i', 's', 's', 'i', 'n', 'g' };
            HashBytes(hash, missingMarker.data(), missingMarker.size());
            return hash;
        }

        std::vector<std::string> entrySignatures;
        for (std::filesystem::recursive_directory_iterator iterator(
                 normalizedRoot,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec);
             !ec && iterator != std::filesystem::recursive_directory_iterator();
             iterator.increment(ec))
        {
            if (ec)
            {
                break;
            }

            const std::filesystem::directory_entry& entry = *iterator;
            std::error_code entryEc;
            const std::filesystem::path relativePath =
                std::filesystem::relative(entry.path(), normalizedRoot, entryEc).lexically_normal();
            if (entryEc)
            {
                continue;
            }

            std::string signature = relativePath.generic_string();
#if defined(_WIN32)
            std::transform(
                signature.begin(),
                signature.end(),
                signature.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
#endif

            if (entry.is_directory(entryEc))
            {
                signature += "|d|";
            }
            else if (entry.is_regular_file(entryEc))
            {
                signature += "|f|";
                const auto size = entry.file_size(entryEc);
                if (!entryEc)
                {
                    signature += std::to_string(size);
                }
                signature += "|";
            }
            else
            {
                signature += "|o|";
            }

            const auto writeTime = entry.last_write_time(entryEc);
            if (!entryEc)
            {
                signature += std::to_string(writeTime.time_since_epoch().count());
            }

            entrySignatures.push_back(std::move(signature));
        }

        std::sort(entrySignatures.begin(), entrySignatures.end());
        for (const std::string& entrySignature : entrySignatures)
        {
            HashString(hash, entrySignature);
        }

        return hash;
    }

    std::string ContentRootWatchService::NormalizeRootKey(const std::filesystem::path& rootPath)
    {
        return NormalizePath(rootPath).generic_string();
    }
}
