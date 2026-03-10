#include "Luma/Editor/Content/ContentBrowserRefreshService.h"

namespace Luma::Editor
{
    namespace
    {
        const std::filesystem::path& ResolveActiveRootPath(
            const std::vector<ContentBrowserRootState>& roots,
            const int activeRootIndex)
        {
            static const std::filesystem::path emptyPath;

            if (activeRootIndex < 0 || activeRootIndex >= static_cast<int>(roots.size()))
            {
                return emptyPath;
            }

            return roots[static_cast<std::size_t>(activeRootIndex)].path;
        }
    }

    void ContentBrowserRefreshService::InvalidateFilterCache(ContentBrowserCache& cache) const
    {
        cache.InvalidateFilterCache();
    }

    void ContentBrowserRefreshService::InvalidateFolderTreeCache(ContentBrowserCache& cache) const
    {
        cache.InvalidateFolderTreeCache();
    }

    void ContentBrowserRefreshService::RebuildFilteredContentEntries(ContentBrowserCache& cache) const
    {
        cache.RebuildFilteredContentEntries();
    }

    void ContentBrowserRefreshService::RebuildContentFolderTreeCache(
        ContentBrowserCache& cache,
        const std::filesystem::path& rootPath) const
    {
        cache.RebuildContentFolderTreeCache(rootPath);
    }

    void ContentBrowserRefreshService::PumpContentFolderTreeRebuild(
        ContentBrowserCache& cache,
        const std::vector<ContentBrowserRootState>& roots,
        const int activeRootIndex) const
    {
        cache.PumpContentFolderTreeRebuild(ResolveActiveRootPath(roots, activeRootIndex));
    }

    void ContentBrowserRefreshService::RefreshContentEntries(
        ContentBrowserCache& cache,
        const std::filesystem::path& currentDirectory,
        std::string& contentStatus) const
    {
        cache.RefreshContentEntries(currentDirectory, contentStatus);
    }

    void ContentBrowserRefreshService::PumpContentEntriesRefresh(
        ContentBrowserCache& cache,
        const std::filesystem::path& currentDirectory,
        std::filesystem::path& selectedEntry,
        std::string& contentStatus) const
    {
        cache.PumpContentEntriesRefresh(currentDirectory, selectedEntry, contentStatus);
    }
}
