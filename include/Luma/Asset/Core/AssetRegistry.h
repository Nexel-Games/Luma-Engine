#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "Luma/Asset/Core/AssetMeta.h"

namespace Luma::Assets
{
    class AssetRegistry
    {
    public:
        void SetProjectRoot(const std::filesystem::path& projectRoot);
        const std::filesystem::path& GetProjectRoot() const;
        std::filesystem::path GetAssetsRoot() const;
        std::filesystem::path GetRegistryIndexPath() const;

        bool Load(std::string& outError);
        bool SaveIndex(std::string& outError) const;
        bool RebuildFromMetaFiles(std::string& outError);

        bool RegisterOrUpdate(const AssetMeta& meta);
        bool Remove(AssetID id);

        const AssetMeta* FindByID(AssetID id) const;
        const AssetMeta* FindBySourcePath(const std::filesystem::path& sourcePath) const;
        const AssetMeta* FindByAssetPath(const std::filesystem::path& assetPath) const;
        std::vector<const AssetMeta*> ListByType(AssetType type) const;

        const std::unordered_map<AssetID, AssetMeta>& GetAllAssets() const;
        void Clear();

    private:
        std::string NormalizePathKey(const std::filesystem::path& path) const;
        std::string NormalizePathKeyAbsolute(const std::filesystem::path& path) const;
        void RebuildLookupTables();

        std::filesystem::path m_ProjectRoot;
        std::unordered_map<AssetID, AssetMeta> m_AssetsByID;
        std::unordered_map<std::string, AssetID> m_AssetPathToID;
        std::unordered_map<std::string, AssetID> m_SourcePathToID;
    };
}

