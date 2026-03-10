#include "Luma/Asset/Core/AssetHash.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace Luma::Assets
{
    namespace
    {
        constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
        constexpr std::uint64_t kFnvPrime = 1099511628211ull;
    }

    std::uint64_t HashBytes(const void* data, const std::size_t size)
    {
        if (data == nullptr || size == 0)
        {
            return kFnvOffsetBasis;
        }

        const auto* const bytes = static_cast<const std::uint8_t*>(data);
        std::uint64_t hash = kFnvOffsetBasis;
        for (std::size_t i = 0; i < size; ++i)
        {
            hash ^= static_cast<std::uint64_t>(bytes[i]);
            hash *= kFnvPrime;
        }

        return hash;
    }

    std::uint64_t HashString(const std::string_view value)
    {
        return HashBytes(value.data(), value.size());
    }

    std::uint64_t HashFile(const std::filesystem::path& filePath)
    {
        std::ifstream input(filePath, std::ios::binary);
        if (!input.is_open())
        {
            return 0;
        }

        std::array<char, 1 << 14> buffer {};
        std::uint64_t hash = kFnvOffsetBasis;
        while (input.good())
        {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize readCount = input.gcount();
            if (readCount <= 0)
            {
                break;
            }

            hash = CombineHash(hash, HashBytes(buffer.data(), static_cast<std::size_t>(readCount)));
        }

        return hash;
    }

    std::uint64_t HashStringMap(const std::unordered_map<std::string, std::string>& values)
    {
        std::vector<std::pair<std::string, std::string>> sortedValues;
        sortedValues.reserve(values.size());
        for (const auto& [key, value] : values)
        {
            sortedValues.emplace_back(key, value);
        }

        std::sort(
            sortedValues.begin(),
            sortedValues.end(),
            [](const auto& lhs, const auto& rhs)
            {
                return lhs.first < rhs.first;
            });

        std::uint64_t hash = kFnvOffsetBasis;
        for (const auto& [key, value] : sortedValues)
        {
            hash = CombineHash(hash, HashString(key));
            hash = CombineHash(hash, HashString(value));
        }

        return hash;
    }

    std::uint64_t HashPathVector(const std::vector<std::filesystem::path>& paths)
    {
        std::uint64_t hash = kFnvOffsetBasis;
        for (const auto& path : paths)
        {
            hash = CombineHash(hash, HashString(path.generic_string()));
        }
        return hash;
    }

    std::uint64_t CombineHash(const std::uint64_t seed, const std::uint64_t value)
    {
        std::uint64_t hash = seed;
        hash ^= value + 0x9e3779b97f4a7c15ull + (hash << 6) + (hash >> 2);
        return hash;
    }
}

