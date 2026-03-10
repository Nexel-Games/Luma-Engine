#include "Luma/Editor/Content/ContentBrowserRootService.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <string>

namespace Luma::Editor
{
    namespace
    {
        std::string ToLowerString(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }

        std::filesystem::path NormalizePathForComparison(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return {};
            }

            std::error_code ec;
            std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
            if (ec)
            {
                normalized = path.lexically_normal();
            }

            return normalized.lexically_normal();
        }

        bool PathIsWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root)
        {
            if (path.empty() || root.empty())
            {
                return false;
            }

            std::string normalizedPath = NormalizePathForComparison(path).generic_string();
            std::string normalizedRoot = NormalizePathForComparison(root).generic_string();
#if defined(_WIN32)
            normalizedPath = ToLowerString(normalizedPath);
            normalizedRoot = ToLowerString(normalizedRoot);
#endif

            if (normalizedPath.size() < normalizedRoot.size())
            {
                return false;
            }

            if (normalizedPath.compare(0, normalizedRoot.size(), normalizedRoot) != 0)
            {
                return false;
            }

            if (normalizedPath.size() == normalizedRoot.size())
            {
                return true;
            }

            const char separator = normalizedPath[normalizedRoot.size()];
            return separator == '/' || separator == '\\';
        }
    }

    std::filesystem::path ContentBrowserRootService::ResolveInitialContentRoot(
        const bool projectLoaded,
        const std::filesystem::path& assetsPath) const
    {
        if (projectLoaded && !assetsPath.empty())
        {
            return assetsPath;
        }

        const std::filesystem::path current = std::filesystem::current_path();
        const std::array<std::filesystem::path, 4> candidates = {
            current / "Assets",
            current / "assets",
            current.parent_path() / "Assets",
            current.parent_path() / "assets"
        };

        for (const auto& path : candidates)
        {
            if (std::filesystem::exists(path))
            {
                return path;
            }
        }

        return current;
    }

    void ContentBrowserRootService::RefreshRoots(ContentBrowserRootRefreshContext& context) const
    {
        if (context.contentRoot == nullptr ||
            context.currentDirectory == nullptr ||
            context.selectedEntry == nullptr ||
            context.roots == nullptr ||
            context.activeRootIndex == nullptr ||
            context.mountRoots == nullptr ||
            !context.invalidateFolderTreeCache ||
            !context.clearEntries)
        {
            return;
        }

        const std::filesystem::path previousRoot =
            GetActiveRoot(*context.roots, *context.activeRootIndex) != nullptr
                ? GetActiveRoot(*context.roots, *context.activeRootIndex)->path
                : std::filesystem::path {};

        context.roots->clear();
        context.invalidateFolderTreeCache();
        if (!context.contentRoot->empty())
        {
            ContentBrowserRootState root;
            root.id = "project-content";
            root.label = "Content";
            root.badge = "RW";
            root.source = "Project Assets";
            root.path = context.contentRoot->lexically_normal();
            root.readOnly = false;
            root.packageRoot = false;
            context.roots->push_back(std::move(root));
        }

        for (const Assets::PackageMountRoot& mountRoot : *context.mountRoots)
        {
            if (mountRoot.path.empty())
            {
                continue;
            }

            ContentBrowserRootState root;
            root.id = mountRoot.packageId + ":" + mountRoot.version + ":" + mountRoot.kind + ":" + mountRoot.path.generic_string();
            root.label = mountRoot.displayName.empty() ? mountRoot.packageId : mountRoot.displayName;
            root.badge = mountRoot.readOnly ? "RO" : "RW";
            root.source = mountRoot.packageId;
            if (!mountRoot.version.empty())
            {
                root.source += "@" + mountRoot.version;
            }
            if (!mountRoot.kind.empty())
            {
                root.source += " - " + mountRoot.kind;
            }
            root.path = mountRoot.path.lexically_normal();
            root.readOnly = mountRoot.readOnly;
            root.packageRoot = true;
            context.roots->push_back(std::move(root));
        }

        *context.activeRootIndex = 0;
        if (!previousRoot.empty())
        {
            for (std::size_t index = 0; index < context.roots->size(); ++index)
            {
                if (NormalizePathForComparison((*context.roots)[index].path) == NormalizePathForComparison(previousRoot))
                {
                    *context.activeRootIndex = static_cast<int>(index);
                    break;
                }
            }
        }

        const ContentBrowserRootState* activeRoot = GetActiveRoot(*context.roots, *context.activeRootIndex);
        if (activeRoot == nullptr)
        {
            context.currentDirectory->clear();
            context.selectedEntry->clear();
            context.clearEntries();
            return;
        }

        if (context.currentDirectory->empty() || !PathIsWithinRoot(*context.currentDirectory, activeRoot->path))
        {
            *context.currentDirectory = activeRoot->path;
        }

        if (!context.selectedEntry->empty() && !PathIsWithinRoot(*context.selectedEntry, activeRoot->path))
        {
            context.selectedEntry->clear();
        }
    }

    const ContentBrowserRootState* ContentBrowserRootService::GetActiveRoot(
        const std::vector<ContentBrowserRootState>& roots,
        const int activeRootIndex) const
    {
        if (activeRootIndex < 0 || activeRootIndex >= static_cast<int>(roots.size()))
        {
            return nullptr;
        }

        return &roots[static_cast<std::size_t>(activeRootIndex)];
    }
}
