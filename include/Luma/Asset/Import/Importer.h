#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Asset/Core/AssetMeta.h"
#include "Luma/Asset/Import/ImportSettings.h"

namespace Luma::Assets
{
    enum class ImportStage
    {
        SourceDiscovery = 0,
        ImporterSelection,
        ConvertToIntermediate,
        PostProcess,
        Serialize,
        Register,
        Thumbnail,
        Completed,
        Failed
    };

    struct ImportRequest
    {
        std::vector<std::filesystem::path> sourcePaths;
        std::filesystem::path targetDirectory;
        std::string importPreset;
        bool async = false;
        bool headless = false;
        bool generateThumbnails = true;
        ImportSettingsMap settingsOverrides;
    };

    struct ImportContext
    {
        std::filesystem::path projectRoot;
        std::filesystem::path assetsRoot;
        std::filesystem::path cacheRoot;
        std::filesystem::path importCacheRoot;
    };

    struct IntermediateAssetData
    {
        struct SidecarFile
        {
            std::filesystem::path relativePath;
            std::vector<std::uint8_t> payload;
        };

        AssetType type = AssetType::Unknown;
        std::vector<std::uint8_t> payload;
        std::vector<SidecarFile> sidecarFiles;
        std::vector<AssetID> dependencies;
        std::vector<std::string> tags;
        std::string thumbnailID;
    };

    struct ImportOutput
    {
        AssetMeta meta;
        IntermediateAssetData intermediate;
    };

    struct ImportResult
    {
        bool success = false;
        bool skipped = false;
        ImportStage lastStage = ImportStage::SourceDiscovery;
        std::string message;
        AssetID assetID = 0;
        std::filesystem::path assetPath;
        std::filesystem::path metaPath;
    };

    class IAssetImporter
    {
    public:
        virtual ~IAssetImporter() = default;

        virtual std::string_view GetImporterID() const = 0;
        virtual std::uint32_t GetVersion() const = 0;
        virtual int GetPriority() const = 0;
        virtual bool CanHandle(const ImportRequest& request) const = 0;
        virtual AssetType GetOutputType(const ImportRequest& request) const = 0;
        virtual ImportSettingsSchema BuildSettingsSchema() const = 0;
        virtual bool Import(
            const ImportRequest& request,
            const ImportContext& context,
            const ImportSettingsMap& resolvedSettings,
            ImportOutput& outOutput,
            std::string& outError) const = 0;
    };
}
