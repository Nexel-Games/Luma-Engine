#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Asset/Core/MeshAssetIO.h"
#include "Luma/Scene/Scene.h"

namespace Luma
{
    struct MaterialComponent;
}

namespace Luma::Editor
{
    struct MeshEntityImportContext
    {
        Scene* scene = nullptr;
        std::filesystem::path activeContentRoot;
        bool projectLoaded = false;
        std::filesystem::path projectAssetsPath;
        std::filesystem::path projectRoot;
        std::function<std::string(const std::string&)> generateUniqueEntityName;
        std::function<void(EntityID)> selectSingleEntity;
        std::function<void(std::string_view)> logImportWarning;
        std::function<void(MaterialComponent&)> initializeDefaultMaterial;
        std::function<void(
            const std::string&,
            const std::filesystem::path&,
            const std::vector<Assets::MeshScenePart>&)> cacheImportedSceneParts;
    };

    class MeshEntityImportService
    {
    public:
        EntityID CreateEntityFromMeshAsset(
            const MeshEntityImportContext& context,
            const std::filesystem::path& assetPath,
            EntityID parentEntity = entt::null,
            const std::array<float, 3>* worldPosition = nullptr) const;
    };
}
