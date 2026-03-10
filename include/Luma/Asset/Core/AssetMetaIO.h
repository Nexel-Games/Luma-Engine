#pragma once

#include <filesystem>
#include <string>

#include "Luma/Asset/Core/AssetMeta.h"

namespace Luma::Assets
{
    bool WriteMetaFile(const std::filesystem::path& metaPath, const AssetMeta& meta, std::string& outError);
    bool ReadMetaFile(const std::filesystem::path& metaPath, AssetMeta& outMeta, std::string& outError);
    std::string MakeUtcTimestampString();
}

