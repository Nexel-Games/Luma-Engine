#include "Luma/Asset/Import/ImportPipeline.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "Luma/Asset/Core/AssetHash.h"
#include "Luma/Asset/Core/AssetMetaIO.h"
#include "Luma/Core/Foundation/UUID.h"

namespace Luma::Assets
{
    namespace
    {
        std::filesystem::path MakeAbsolute(
            const std::filesystem::path& path,
            const std::filesystem::path& fallbackBase)
        {
            if (path.is_absolute())
            {
                return path.lexically_normal();
            }

            return (fallbackBase / path).lexically_normal();
        }

        std::filesystem::path MakeRelativeTo(
            const std::filesystem::path& absolutePath,
            const std::filesystem::path& basePath)
        {
            std::error_code ec;
            const std::filesystem::path relativePath = std::filesystem::relative(absolutePath, basePath, ec);
            if (!ec && !relativePath.empty())
            {
                return relativePath.lexically_normal();
            }
            return absolutePath.lexically_normal();
        }
    }

    ImportPipeline::ImportPipeline(
        AssetRegistry& assetRegistry,
        const ImporterRegistry& importerRegistry)
        : m_AssetRegistry(assetRegistry)
        , m_ImporterRegistry(importerRegistry)
    {
    }

    bool ImportPipeline::Initialize(const std::filesystem::path& projectRoot, std::string& outError)
    {
        if (projectRoot.empty())
        {
            outError = "Project root path is empty.";
            return false;
        }

        m_Context.projectRoot = projectRoot.lexically_normal();
        m_Context.assetsRoot = (m_Context.projectRoot / "Assets").lexically_normal();
        m_Context.cacheRoot = (m_Context.projectRoot / "Cache").lexically_normal();
        m_Context.importCacheRoot = (m_Context.cacheRoot / "Import").lexically_normal();

        std::error_code ec;
        std::filesystem::create_directories(m_Context.assetsRoot, ec);
        if (ec)
        {
            outError = "Failed to create Assets directory.";
            return false;
        }

        std::filesystem::create_directories(m_Context.importCacheRoot, ec);
        if (ec)
        {
            outError = "Failed to create import cache directory.";
            return false;
        }

        m_AssetRegistry.SetProjectRoot(m_Context.projectRoot);
        if (!m_AssetRegistry.Load(outError))
        {
            return false;
        }

        m_Initialized = true;
        outError.clear();
        return true;
    }

    ImportResult ImportPipeline::Import(const ImportRequest& request)
    {
        ImportResult result {};
        result.lastStage = ImportStage::SourceDiscovery;

        if (!m_Initialized)
        {
            result.message = "Import pipeline is not initialized.";
            result.lastStage = ImportStage::Failed;
            return result;
        }

        if (request.sourcePaths.empty())
        {
            result.message = "No source path specified.";
            result.lastStage = ImportStage::Failed;
            return result;
        }

        ImportRequest normalizedRequest = request;
        normalizedRequest.sourcePaths.clear();
        normalizedRequest.sourcePaths.reserve(request.sourcePaths.size());

        for (const auto& sourcePath : request.sourcePaths)
        {
            const std::filesystem::path absolutePath = MakeAbsolute(sourcePath, std::filesystem::current_path());
            std::error_code ec;
            if (!std::filesystem::exists(absolutePath, ec))
            {
                result.message = "Source file not found: " + absolutePath.string();
                result.lastStage = ImportStage::Failed;
                return result;
            }
            normalizedRequest.sourcePaths.push_back(absolutePath);
        }

        result.lastStage = ImportStage::ImporterSelection;
        const IAssetImporter* const importer = m_ImporterRegistry.ResolveBestImporter(normalizedRequest);
        if (importer == nullptr)
        {
            result.message = "No importer available for source: " + normalizedRequest.sourcePaths.front().string();
            result.lastStage = ImportStage::Failed;
            return result;
        }

        const AssetMeta* const existingMeta = m_AssetRegistry.FindBySourcePath(normalizedRequest.sourcePaths.front());
        ImportSettingsMap resolvedSettings = BuildDefaultSettings(importer->BuildSettingsSchema());
        if (existingMeta != nullptr && !existingMeta->importSettings.empty())
        {
            resolvedSettings = existingMeta->importSettings;
        }
        for (const auto& [key, value] : normalizedRequest.settingsOverrides)
        {
            resolvedSettings[key] = value;
        }

        const std::uint64_t sourceHash = ComputeSourceHash(normalizedRequest.sourcePaths);
        const std::uint64_t settingsHash = HashStringMap(resolvedSettings);
        std::uint64_t buildHash = CombineHash(sourceHash, settingsHash);
        buildHash = CombineHash(buildHash, HashString(importer->GetImporterID()));
        buildHash = CombineHash(buildHash, static_cast<std::uint64_t>(importer->GetVersion()));

        if (existingMeta != nullptr && existingMeta->buildHash == buildHash)
        {
            result.success = true;
            result.skipped = true;
            result.lastStage = ImportStage::Completed;
            result.assetID = existingMeta->id;
            result.assetPath = existingMeta->assetPath;
            result.metaPath = existingMeta->metaPath;
            result.message = "Skipped import (source and settings unchanged).";
            return result;
        }

        result.lastStage = ImportStage::ConvertToIntermediate;
        ImportOutput output {};
        std::string importError;
        if (!importer->Import(
                normalizedRequest,
                m_Context,
                resolvedSettings,
                output,
                importError))
        {
            result.message = "Importer failed: " + importError;
            result.lastStage = ImportStage::Failed;
            return result;
        }

        AssetMeta meta = output.meta;
        meta.id = meta.id != 0 ? meta.id : (existingMeta != nullptr ? existingMeta->id : GenerateUUID());
        meta.type = output.intermediate.type != AssetType::Unknown
                        ? output.intermediate.type
                        : importer->GetOutputType(normalizedRequest);
        meta.name = !meta.name.empty() ? meta.name : SanitizeAssetBaseName(normalizedRequest.sourcePaths.front());
        meta.sourcePaths = normalizedRequest.sourcePaths;
        meta.importerID = std::string(importer->GetImporterID());
        meta.importerVersion = importer->GetVersion();
        meta.importSettings = resolvedSettings;
        meta.sourceHash = sourceHash;
        meta.settingsHash = settingsHash;
        meta.buildHash = buildHash;
        meta.dependencies = output.intermediate.dependencies;
        meta.tags = output.intermediate.tags;
        meta.thumbnailID = output.intermediate.thumbnailID;
        meta.lastImportUtc = MakeUtcTimestampString();

        result.lastStage = ImportStage::Serialize;
        const std::filesystem::path assetPathAbsolute =
            ResolveAssetPathForImport(normalizedRequest, existingMeta, meta.type);
        const std::filesystem::path metaPathAbsolute = assetPathAbsolute.string() + ".meta";

        std::error_code ec;
        std::filesystem::create_directories(assetPathAbsolute.parent_path(), ec);
        if (ec)
        {
            result.message = "Failed to create asset output directory: " + assetPathAbsolute.parent_path().string();
            result.lastStage = ImportStage::Failed;
            return result;
        }

        {
            std::ofstream assetOutput(assetPathAbsolute, std::ios::binary | std::ios::trunc);
            if (!assetOutput.is_open())
            {
                result.message = "Failed to write asset file: " + assetPathAbsolute.string();
                result.lastStage = ImportStage::Failed;
                return result;
            }

            if (!output.intermediate.payload.empty())
            {
                assetOutput.write(
                    reinterpret_cast<const char*>(output.intermediate.payload.data()),
                    static_cast<std::streamsize>(output.intermediate.payload.size()));
            }

            if (!assetOutput.good())
            {
                result.message = "Failed while writing asset payload.";
                result.lastStage = ImportStage::Failed;
                return result;
            }
        }

        for (const auto& sidecar : output.intermediate.sidecarFiles)
        {
            if (sidecar.relativePath.empty())
            {
                continue;
            }

            const std::filesystem::path sidecarAbsolute =
                (assetPathAbsolute.parent_path() / sidecar.relativePath).lexically_normal();
            std::filesystem::create_directories(sidecarAbsolute.parent_path(), ec);
            if (ec)
            {
                result.message = "Failed to create mesh chunk directory: " + sidecarAbsolute.parent_path().string();
                result.lastStage = ImportStage::Failed;
                return result;
            }

            std::ofstream sidecarOutput(sidecarAbsolute, std::ios::binary | std::ios::trunc);
            if (!sidecarOutput.is_open())
            {
                result.message = "Failed to write sidecar asset file: " + sidecarAbsolute.string();
                result.lastStage = ImportStage::Failed;
                return result;
            }

            if (!sidecar.payload.empty())
            {
                sidecarOutput.write(
                    reinterpret_cast<const char*>(sidecar.payload.data()),
                    static_cast<std::streamsize>(sidecar.payload.size()));
            }

            if (!sidecarOutput.good())
            {
                result.message = "Failed while writing sidecar asset payload.";
                result.lastStage = ImportStage::Failed;
                return result;
            }
        }

        meta.assetPath = MakeRelativeTo(assetPathAbsolute, m_Context.projectRoot);
        meta.metaPath = MakeRelativeTo(metaPathAbsolute, m_Context.projectRoot);

        std::string metaWriteError;
        if (!WriteMetaFile(metaPathAbsolute, meta, metaWriteError))
        {
            result.message = metaWriteError;
            result.lastStage = ImportStage::Failed;
            return result;
        }

        result.lastStage = ImportStage::Register;
        if (!m_AssetRegistry.RegisterOrUpdate(meta))
        {
            result.message = "Failed to register imported asset.";
            result.lastStage = ImportStage::Failed;
            return result;
        }

        std::string indexError;
        if (!m_AssetRegistry.SaveIndex(indexError))
        {
            result.message = "Asset imported but failed to update registry index: " + indexError;
            result.lastStage = ImportStage::Failed;
            return result;
        }

        result.lastStage = ImportStage::Thumbnail;
        if (!normalizedRequest.generateThumbnails)
        {
            result.lastStage = ImportStage::Completed;
        }
        else
        {
            result.lastStage = ImportStage::Completed;
        }

        result.success = true;
        result.assetID = meta.id;
        result.assetPath = meta.assetPath;
        result.metaPath = meta.metaPath;
        result.message = "Imported successfully with importer '" + std::string(importer->GetImporterID()) + "'.";
        return result;
    }

    std::vector<ImportResult> ImportPipeline::ReimportChanged()
    {
        std::vector<ImportResult> results;
        std::vector<AssetMeta> snapshot;
        snapshot.reserve(m_AssetRegistry.GetAllAssets().size());
        for (const auto& [id, meta] : m_AssetRegistry.GetAllAssets())
        {
            snapshot.push_back(meta);
        }

        for (const auto& meta : snapshot)
        {
            if (meta.sourcePaths.empty())
            {
                continue;
            }

            const std::uint64_t currentSourceHash = ComputeSourceHash(meta.sourcePaths);
            if (currentSourceHash == meta.sourceHash)
            {
                continue;
            }

            ImportRequest request {};
            request.sourcePaths = meta.sourcePaths;
            request.targetDirectory = std::filesystem::path(meta.assetPath).parent_path();
            request.importPreset = "Reimport";
            request.generateThumbnails = false;
            request.settingsOverrides = meta.importSettings;
            results.push_back(Import(request));
        }

        return results;
    }

    const ImportContext& ImportPipeline::GetContext() const
    {
        return m_Context;
    }

    std::filesystem::path ImportPipeline::ResolveTargetDirectory(const ImportRequest& request) const
    {
        if (request.targetDirectory.empty())
        {
            return m_Context.assetsRoot / "Imported";
        }

        if (request.targetDirectory.is_absolute())
        {
            return request.targetDirectory;
        }

        return m_Context.assetsRoot / request.targetDirectory;
    }

    std::filesystem::path ImportPipeline::ResolveAssetPathForImport(
        const ImportRequest& request,
        const AssetMeta* existingMeta,
        const AssetType type) const
    {
        if (existingMeta != nullptr && !existingMeta->assetPath.empty())
        {
            return MakeAbsolute(existingMeta->assetPath, m_Context.projectRoot);
        }

        const std::filesystem::path targetDirectory = ResolveTargetDirectory(request);
        const std::string baseName = SanitizeAssetBaseName(request.sourcePaths.front());
        const std::string extension = AssetExtension(type);

        std::filesystem::path candidate = targetDirectory / (baseName + extension);
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec))
        {
            return candidate;
        }

        for (int i = 1; i < 1000; ++i)
        {
            candidate = targetDirectory / (baseName + "_" + std::to_string(i) + extension);
            if (!std::filesystem::exists(candidate, ec))
            {
                return candidate;
            }
        }

        return targetDirectory / (baseName + "_generated" + extension);
    }

    std::string ImportPipeline::SanitizeAssetBaseName(const std::filesystem::path& sourcePath) const
    {
        std::string name = sourcePath.stem().string();
        if (name.empty())
        {
            name = "Asset";
        }

        for (char& character : name)
        {
            const bool alphaNumeric = std::isalnum(static_cast<unsigned char>(character)) != 0;
            const bool allowed = alphaNumeric || character == '_' || character == '-';
            if (!allowed)
            {
                character = '_';
            }
        }

        return name;
    }

    std::uint64_t ImportPipeline::ComputeSourceHash(const std::vector<std::filesystem::path>& sourcePaths) const
    {
        std::vector<std::filesystem::path> sortedPaths = sourcePaths;
        std::sort(sortedPaths.begin(), sortedPaths.end());

        std::uint64_t hash = 0;
        for (const auto& sourcePath : sortedPaths)
        {
            hash = CombineHash(hash, HashString(sourcePath.generic_string()));
            hash = CombineHash(hash, HashFile(sourcePath));
        }
        return hash;
    }

    std::string ImportPipeline::AssetExtension(const AssetType type)
    {
        switch (type)
        {
        case AssetType::Texture2D:
            return ".lumatex";
        case AssetType::TextureCube:
        case AssetType::HDRI:
            return ".lumasky";
        case AssetType::MaterialGraph:
        case AssetType::MaterialInstance:
            return ".lumamat";
        case AssetType::StaticMesh:
        case AssetType::SkeletalMesh:
            return ".lumamesh";
        case AssetType::AnimationClip:
            return ".lumaanim";
        case AssetType::AudioClip:
            return ".lumaaudio";
        case AssetType::LuaScript:
            return ".lumascript";
        case AssetType::Procedural:
            return ".lumaproc";
        case AssetType::PackageManifest:
            return ".lumapkg";
        case AssetType::Prefab:
            return ".lumaprefab";
        case AssetType::Scene:
            return ".lumascene";
        case AssetType::Unknown:
        default:
            return ".lumaasset";
        }
    }
}
