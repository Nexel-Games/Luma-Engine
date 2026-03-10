#include "Luma/Asset/Streaming/FileResourceStreamProvider.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace Luma::Assets
{
    namespace
    {
        std::string ToLowerCopy(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });
            return value;
        }

        void AppendUniqueCandidate(
            std::vector<std::pair<std::filesystem::path, std::uint32_t>>& candidates,
            const std::filesystem::path& candidatePath,
            const std::uint32_t lod)
        {
            const std::filesystem::path normalized = candidatePath.lexically_normal();
            const auto existing = std::find_if(
                candidates.begin(),
                candidates.end(),
                [&normalized](const auto& entry)
                {
                    return entry.first.lexically_normal() == normalized;
                });
            if (existing == candidates.end())
            {
                candidates.emplace_back(normalized, lod);
            }
        }

        std::vector<std::pair<std::filesystem::path, std::uint32_t>> BuildCandidates(const StreamRequestDesc& request)
        {
            std::vector<std::pair<std::filesystem::path, std::uint32_t>> candidates;
            const std::filesystem::path basePath = request.sourcePath.lexically_normal();
            const std::filesystem::path parent = basePath.parent_path();
            const std::string stem = basePath.stem().string();
            const std::string extension = basePath.extension().string();

            if (request.lod.mode == LODStreamingMode::Disabled || request.lod.targetLod == 0)
            {
                AppendUniqueCandidate(candidates, basePath, 0);
                return candidates;
            }

            const std::uint32_t clampedTarget = std::clamp(
                request.lod.targetLod,
                request.lod.minLod,
                request.lod.maxLod);

            auto appendLodPatterns =
                [&](const std::uint32_t lod)
                {
                    const std::string lodSuffix = std::to_string(lod);
                    AppendUniqueCandidate(candidates, parent / (stem + "_LOD" + lodSuffix + extension), lod);
                    AppendUniqueCandidate(candidates, parent / (stem + "_lod" + lodSuffix + extension), lod);
                    AppendUniqueCandidate(candidates, parent / (stem + ".LOD" + lodSuffix + extension), lod);
                    AppendUniqueCandidate(candidates, parent / (stem + ".lod" + lodSuffix + extension), lod);
                    AppendUniqueCandidate(candidates, parent / ("LOD" + lodSuffix) / (stem + extension), lod);
                    AppendUniqueCandidate(candidates, parent / ("lod" + lodSuffix) / (stem + extension), lod);
                };

            appendLodPatterns(clampedTarget);
            if (request.lod.allowLowerDetailFallback)
            {
                for (std::uint32_t lod = clampedTarget; lod > request.lod.minLod; --lod)
                {
                    appendLodPatterns(lod - 1);
                }
            }

            AppendUniqueCandidate(candidates, basePath, 0);
            return candidates;
        }
    }

    std::string_view FileResourceStreamProvider::GetProviderId() const
    {
        return "file";
    }

    bool FileResourceStreamProvider::CanStream(const StreamRequestDesc& request) const
    {
        return !request.sourcePath.empty();
    }

    bool FileResourceStreamProvider::Stream(
        const StreamRequestDesc& request,
        const std::uint32_t workerIndex,
        StreamPayload& outPayload,
        std::string& outError)
    {
        (void)workerIndex;

        const std::vector<std::pair<std::filesystem::path, std::uint32_t>> candidates = BuildCandidates(request);
        for (const auto& [candidatePath, lod] : candidates)
        {
            std::error_code existsError;
            const bool exists = std::filesystem::exists(candidatePath, existsError);
            const bool regularFile = exists && std::filesystem::is_regular_file(candidatePath, existsError);
            if (!regularFile)
            {
                continue;
            }

            std::ifstream input(candidatePath, std::ios::binary | std::ios::ate);
            if (!input.is_open())
            {
                continue;
            }

            const std::streamsize size = input.tellg();
            if (size < 0)
            {
                outError = "Unable to determine file size for " + candidatePath.string() + ".";
                return false;
            }

            input.seekg(0, std::ios::beg);
            outPayload.bytes.resize(static_cast<std::size_t>(size));
            if (size > 0 && !input.read(reinterpret_cast<char*>(outPayload.bytes.data()), size))
            {
                outError = "Failed to read file bytes from " + candidatePath.string() + ".";
                return false;
            }

            outPayload.resolvedSourcePath = candidatePath;
            outPayload.residentCpuBytes =
                request.estimatedCpuBytes > 0 ? request.estimatedCpuBytes : static_cast<std::uint64_t>(outPayload.bytes.size());
            outPayload.residentGpuBytes = request.estimatedGpuBytes;
            outPayload.resolvedLod = lod;
            outPayload.fromFallbackLod = lod != request.lod.targetLod;
            outPayload.contentTag = ToLowerCopy(candidatePath.extension().string());
            if (!outPayload.contentTag.empty() && outPayload.contentTag.front() == '.')
            {
                outPayload.contentTag.erase(outPayload.contentTag.begin());
            }
            return true;
        }

        outError = "No matching stream source found for " + request.sourcePath.string() + ".";
        return false;
    }
}
