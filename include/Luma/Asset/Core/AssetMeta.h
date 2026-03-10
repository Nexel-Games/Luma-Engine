#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "Luma/Asset/Core/AssetTypes.h"

namespace Luma::Assets
{
    struct AssetMeta
    {
        std::uint32_t schemaVersion = 1;
        AssetID id = 0;
        AssetType type = AssetType::Unknown;
        std::string name;
        std::filesystem::path assetPath;
        std::filesystem::path metaPath;
        std::vector<std::filesystem::path> sourcePaths;
        std::string importerID;
        std::uint32_t importerVersion = 1;
        std::unordered_map<std::string, std::string> importSettings;
        std::uint64_t sourceHash = 0;
        std::uint64_t settingsHash = 0;
        std::uint64_t buildHash = 0;
        std::vector<AssetID> dependencies;
        std::vector<std::string> tags;
        std::string thumbnailID;
        std::string lastImportUtc;
    };
}

