#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

#include "Luma/Scene/VehicleComponent.h"

namespace Luma
{
    class VehicleTuningAssetCacheService
    {
    public:
        bool TryApply(const std::string& assetPath, VehicleComponent& inOutVehicle);
        void Clear();

    private:
        struct CacheEntry
        {
            std::filesystem::file_time_type lastWriteTime {};
            VehicleComponent vehicle {};
            bool valid = false;
        };

        std::filesystem::path ResolveAssetPath(const std::string& assetPath) const;
        bool LoadVehicleTuningAsset(const std::filesystem::path& assetPath, VehicleComponent& outVehicle) const;

        std::unordered_map<std::string, CacheEntry> m_Cache;
    };
}
