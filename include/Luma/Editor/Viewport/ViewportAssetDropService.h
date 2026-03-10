#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <string>

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct ViewportAssetDropContext
    {
        std::function<std::array<float, 3>()> computeDropSpawnPosition;
        std::function<EntityID(const std::filesystem::path&, EntityID, const std::array<float, 3>*)> createEntityFromMeshAsset;
        std::function<bool(const std::filesystem::path&)> isMeshAssetPathCandidate;
        std::function<void(std::string)> setContentStatus;
    };

    class ViewportAssetDropService
    {
    public:
        void Handle(const ViewportAssetDropContext& context);
    };
}
