#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Luma/Asset/Core/AssetRegistry.h"
#include "Luma/Asset/Import/ImporterRegistry.h"

namespace Luma::Assets
{
    class ImportPipeline
    {
    public:
        ImportPipeline(AssetRegistry& assetRegistry, const ImporterRegistry& importerRegistry);

        bool Initialize(const std::filesystem::path& projectRoot, std::string& outError);
        ImportResult Import(const ImportRequest& request);
        std::vector<ImportResult> ReimportChanged();

        const ImportContext& GetContext() const;

    private:
        std::filesystem::path ResolveTargetDirectory(const ImportRequest& request) const;
        std::filesystem::path ResolveAssetPathForImport(
            const ImportRequest& request,
            const AssetMeta* existingMeta,
            AssetType type) const;
        std::string SanitizeAssetBaseName(const std::filesystem::path& sourcePath) const;
        std::uint64_t ComputeSourceHash(const std::vector<std::filesystem::path>& sourcePaths) const;
        static std::string AssetExtension(AssetType type);

        AssetRegistry& m_AssetRegistry;
        const ImporterRegistry& m_ImporterRegistry;
        ImportContext m_Context;
        bool m_Initialized = false;
    };
}

