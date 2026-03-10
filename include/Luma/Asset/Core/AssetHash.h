#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Luma::Assets
{
    std::uint64_t HashBytes(const void* data, std::size_t size);
    std::uint64_t HashString(std::string_view value);
    std::uint64_t HashFile(const std::filesystem::path& filePath);
    std::uint64_t HashStringMap(const std::unordered_map<std::string, std::string>& values);
    std::uint64_t HashPathVector(const std::vector<std::filesystem::path>& paths);
    std::uint64_t CombineHash(std::uint64_t seed, std::uint64_t value);
}

