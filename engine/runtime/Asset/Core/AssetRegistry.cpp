#include "Luma/Asset/Core/AssetRegistry.h"

#include <algorithm>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

#include "Luma/Asset/Core/AssetMetaIO.h"

namespace Luma::Assets
{
    namespace
    {
        std::string Trim(std::string value)
        {
            const auto isSpace = [](const char c)
            {
                return c == ' ' || c == '\t' || c == '\r' || c == '\n';
            };

            while (!value.empty() && isSpace(value.front()))
            {
                value.erase(value.begin());
            }
            while (!value.empty() && isSpace(value.back()))
            {
                value.pop_back();
            }
            return value;
        }
    }

    void AssetRegistry::SetProjectRoot(const std::filesystem::path& projectRoot)
    {
        m_ProjectRoot = projectRoot.lexically_normal();
    }

    const std::filesystem::path& AssetRegistry::GetProjectRoot() const
    {
        return m_ProjectRoot;
    }

    std::filesystem::path AssetRegistry::GetAssetsRoot() const
    {
        return m_ProjectRoot / "Assets";
    }

    std::filesystem::path AssetRegistry::GetRegistryIndexPath() const
    {
        return GetAssetsRoot() / ".asset_registry.json";
    }

    bool AssetRegistry::Load(std::string& outError)
    {
        Clear();
        if (m_ProjectRoot.empty())
        {
            outError = "Project root is not set.";
            return false;
        }

        const std::filesystem::path indexPath = GetRegistryIndexPath();
        std::ifstream input(indexPath);
        if (input.is_open())
        {
            std::string line;
            while (std::getline(input, line))
            {
                const std::size_t separator = line.find('=');
                if (separator == std::string::npos)
                {
                    continue;
                }

                const std::string key = Trim(line.substr(0, separator));
                const std::string value = Trim(line.substr(separator + 1));
                if (key != "Meta" || value.empty())
                {
                    continue;
                }

                AssetMeta meta {};
                std::string readError;
                std::filesystem::path metaPath(value);
                if (metaPath.is_relative())
                {
                    metaPath = m_ProjectRoot / metaPath;
                }

                if (!ReadMetaFile(metaPath, meta, readError))
                {
                    continue;
                }
                RegisterOrUpdate(meta);
            }
        }

        if (m_AssetsByID.empty())
        {
            return RebuildFromMetaFiles(outError);
        }

        outError.clear();
        return true;
    }

    bool AssetRegistry::SaveIndex(std::string& outError) const
    {
        const std::filesystem::path indexPath = GetRegistryIndexPath();
        std::error_code ec;
        std::filesystem::create_directories(indexPath.parent_path(), ec);
        if (ec)
        {
            outError = "Failed to create index directory: " + indexPath.parent_path().string();
            return false;
        }

        std::vector<std::filesystem::path> metaPaths;
        metaPaths.reserve(m_AssetsByID.size());
        for (const auto& [id, meta] : m_AssetsByID)
        {
            if (!meta.metaPath.empty())
            {
                metaPaths.push_back(meta.metaPath);
            }
        }

        std::sort(metaPaths.begin(), metaPaths.end());

        std::ofstream output(indexPath, std::ios::trunc);
        if (!output.is_open())
        {
            outError = "Failed to open asset index for write: " + indexPath.string();
            return false;
        }

        output << "SchemaVersion=1\n";
        output << "Count=" << metaPaths.size() << '\n';
        for (const auto& metaPath : metaPaths)
        {
            std::filesystem::path toWrite = metaPath;
            if (!m_ProjectRoot.empty() && metaPath.is_absolute())
            {
                std::error_code relError;
                const std::filesystem::path relativePath = std::filesystem::relative(metaPath, m_ProjectRoot, relError);
                if (!relError && !relativePath.empty())
                {
                    toWrite = relativePath;
                }
            }
            output << "Meta=" << toWrite.generic_string() << '\n';
        }

        if (!output.good())
        {
            outError = "Failed writing asset index: " + indexPath.string();
            return false;
        }

        outError.clear();
        return true;
    }

    bool AssetRegistry::RebuildFromMetaFiles(std::string& outError)
    {
        Clear();

        std::error_code ec;
        const std::filesystem::path assetsRoot = GetAssetsRoot();
        if (!std::filesystem::exists(assetsRoot, ec))
        {
            outError.clear();
            return true;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                 assetsRoot,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec))
        {
            if (ec)
            {
                outError = "Failed while scanning asset metadata under Assets.";
                break;
            }

            if (!entry.is_regular_file(ec))
            {
                continue;
            }

            if (entry.path().extension() != ".meta")
            {
                continue;
            }

            AssetMeta meta {};
            std::string readError;
            if (!ReadMetaFile(entry.path(), meta, readError))
            {
                continue;
            }

            RegisterOrUpdate(meta);
        }

        std::string saveError;
        if (!SaveIndex(saveError) && outError.empty())
        {
            outError = saveError;
        }

        return outError.empty();
    }

    bool AssetRegistry::RegisterOrUpdate(const AssetMeta& meta)
    {
        if (meta.id == 0 || meta.assetPath.empty())
        {
            return false;
        }

        AssetMeta storedMeta = meta;
        if (storedMeta.metaPath.empty())
        {
            storedMeta.metaPath = storedMeta.assetPath;
            storedMeta.metaPath += ".meta";
        }

        m_AssetsByID[storedMeta.id] = std::move(storedMeta);
        RebuildLookupTables();
        return true;
    }

    bool AssetRegistry::Remove(const AssetID id)
    {
        const auto it = m_AssetsByID.find(id);
        if (it == m_AssetsByID.end())
        {
            return false;
        }

        m_AssetsByID.erase(it);
        RebuildLookupTables();
        return true;
    }

    const AssetMeta* AssetRegistry::FindByID(const AssetID id) const
    {
        const auto it = m_AssetsByID.find(id);
        if (it == m_AssetsByID.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    const AssetMeta* AssetRegistry::FindBySourcePath(const std::filesystem::path& sourcePath) const
    {
        const auto mapIt = m_SourcePathToID.find(NormalizePathKeyAbsolute(sourcePath));
        if (mapIt == m_SourcePathToID.end())
        {
            return nullptr;
        }
        return FindByID(mapIt->second);
    }

    const AssetMeta* AssetRegistry::FindByAssetPath(const std::filesystem::path& assetPath) const
    {
        const auto mapIt = m_AssetPathToID.find(NormalizePathKey(assetPath));
        if (mapIt == m_AssetPathToID.end())
        {
            return nullptr;
        }
        return FindByID(mapIt->second);
    }

    std::vector<const AssetMeta*> AssetRegistry::ListByType(const AssetType type) const
    {
        std::vector<const AssetMeta*> assets;
        assets.reserve(m_AssetsByID.size());
        for (const auto& [id, meta] : m_AssetsByID)
        {
            if (meta.type == type)
            {
                assets.push_back(&meta);
            }
        }

        std::sort(
            assets.begin(),
            assets.end(),
            [](const AssetMeta* lhs, const AssetMeta* rhs)
            {
                return lhs->name < rhs->name;
            });
        return assets;
    }

    const std::unordered_map<AssetID, AssetMeta>& AssetRegistry::GetAllAssets() const
    {
        return m_AssetsByID;
    }

    void AssetRegistry::Clear()
    {
        m_AssetsByID.clear();
        m_AssetPathToID.clear();
        m_SourcePathToID.clear();
    }

    std::string AssetRegistry::NormalizePathKey(const std::filesystem::path& path) const
    {
        std::filesystem::path normalizedPath = path;
        if (!m_ProjectRoot.empty() && path.is_absolute())
        {
            std::error_code relError;
            const std::filesystem::path relativePath = std::filesystem::relative(path, m_ProjectRoot, relError);
            if (!relError)
            {
                normalizedPath = relativePath;
            }
        }

        std::string key = normalizedPath.lexically_normal().generic_string();
#if defined(_WIN32)
        std::transform(
            key.begin(),
            key.end(),
            key.begin(),
            [](const unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
#endif
        return key;
    }

    std::string AssetRegistry::NormalizePathKeyAbsolute(const std::filesystem::path& path) const
    {
        std::filesystem::path absolutePath = path;
        if (!absolutePath.is_absolute() && !m_ProjectRoot.empty())
        {
            absolutePath = m_ProjectRoot / absolutePath;
        }

        std::error_code ec;
        absolutePath = std::filesystem::weakly_canonical(absolutePath, ec);
        if (ec)
        {
            absolutePath = absolutePath.lexically_normal();
        }

        std::string key = absolutePath.generic_string();
#if defined(_WIN32)
        std::transform(
            key.begin(),
            key.end(),
            key.begin(),
            [](const unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
#endif
        return key;
    }

    void AssetRegistry::RebuildLookupTables()
    {
        m_AssetPathToID.clear();
        m_SourcePathToID.clear();

        for (const auto& [id, meta] : m_AssetsByID)
        {
            m_AssetPathToID[NormalizePathKey(meta.assetPath)] = id;
            for (const auto& sourcePath : meta.sourcePaths)
            {
                m_SourcePathToID[NormalizePathKeyAbsolute(sourcePath)] = id;
            }
        }
    }
}

