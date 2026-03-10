#include "Luma/Asset/Cook/AssetCooker.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace Luma::Assets
{
    bool AssetCooker::Initialize(const std::filesystem::path& projectRoot, std::string& outError)
    {
        if (projectRoot.empty())
        {
            outError = "Project root path is empty.";
            return false;
        }

        m_ProjectRoot = projectRoot.lexically_normal();
        m_Registry.SetProjectRoot(m_ProjectRoot);
        if (!m_Registry.Load(outError))
        {
            return false;
        }

        m_Initialized = true;
        outError.clear();
        return true;
    }

    bool AssetCooker::CookAll(const std::string_view platformName, std::string& outReport)
    {
        if (!m_Initialized)
        {
            outReport = "AssetCooker is not initialized.";
            return false;
        }

        if (platformName.empty())
        {
            outReport = "Platform name cannot be empty.";
            return false;
        }

        const std::filesystem::path cookRoot = m_ProjectRoot / "Build" / "Cooked" / std::string(platformName);
        std::error_code ec;
        std::filesystem::create_directories(cookRoot, ec);
        if (ec)
        {
            outReport = "Failed to create cook output directory.";
            return false;
        }

        std::vector<const AssetMeta*> assets;
        assets.reserve(m_Registry.GetAllAssets().size());
        for (const auto& [id, meta] : m_Registry.GetAllAssets())
        {
            assets.push_back(&meta);
        }

        std::sort(
            assets.begin(),
            assets.end(),
            [](const AssetMeta* lhs, const AssetMeta* rhs)
            {
                return lhs->assetPath.generic_string() < rhs->assetPath.generic_string();
            });

        std::ofstream manifest(cookRoot / "cooked_manifest.txt", std::ios::trunc);
        if (!manifest.is_open())
        {
            outReport = "Failed to open cooked manifest output.";
            return false;
        }

        std::size_t cookedCount = 0;
        for (const AssetMeta* meta : assets)
        {
            const std::filesystem::path sourceAssetPath = ResolveAbsolute(meta->assetPath);
            if (!std::filesystem::exists(sourceAssetPath, ec))
            {
                continue;
            }

            std::filesystem::path relativeAssetPath = meta->assetPath;
            if (relativeAssetPath.is_absolute())
            {
                relativeAssetPath = std::filesystem::relative(relativeAssetPath, m_ProjectRoot, ec);
                if (ec)
                {
                    relativeAssetPath = std::filesystem::path(sourceAssetPath.filename());
                }
            }

            const std::filesystem::path cookedAssetPath = cookRoot / relativeAssetPath;
            std::filesystem::create_directories(cookedAssetPath.parent_path(), ec);
            if (ec)
            {
                continue;
            }

            std::filesystem::copy_file(
                sourceAssetPath,
                cookedAssetPath,
                std::filesystem::copy_options::overwrite_existing,
                ec);
            if (ec)
            {
                continue;
            }

            manifest
                << "AssetID=" << meta->id
                << "|Type=" << ToString(meta->type)
                << "|Path=" << relativeAssetPath.generic_string()
                << "|BuildHash=" << meta->buildHash
                << '\n';
            ++cookedCount;
        }

        outReport =
            "Cooked " + std::to_string(cookedCount) +
            " asset(s) to " + cookRoot.string();
        return true;
    }

    const std::filesystem::path& AssetCooker::GetProjectRoot() const
    {
        return m_ProjectRoot;
    }

    std::filesystem::path AssetCooker::ResolveAbsolute(const std::filesystem::path& path) const
    {
        if (path.is_absolute())
        {
            return path.lexically_normal();
        }
        return (m_ProjectRoot / path).lexically_normal();
    }
}

