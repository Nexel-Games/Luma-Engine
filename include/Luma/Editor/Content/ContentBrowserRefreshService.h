#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Luma/Editor/Content/ContentBrowserCache.h"
#include "Luma/Editor/Content/ContentBrowserController.h"

namespace Luma::Editor
{
    class ContentBrowserRefreshService
    {
    public:
        void InvalidateFilterCache(ContentBrowserCache& cache) const;
        void InvalidateFolderTreeCache(ContentBrowserCache& cache) const;
        void RebuildFilteredContentEntries(ContentBrowserCache& cache) const;
        void RebuildContentFolderTreeCache(ContentBrowserCache& cache, const std::filesystem::path& rootPath) const;
        void PumpContentFolderTreeRebuild(
            ContentBrowserCache& cache,
            const std::vector<ContentBrowserRootState>& roots,
            int activeRootIndex) const;
        void RefreshContentEntries(
            ContentBrowserCache& cache,
            const std::filesystem::path& currentDirectory,
            std::string& contentStatus) const;
        void PumpContentEntriesRefresh(
            ContentBrowserCache& cache,
            const std::filesystem::path& currentDirectory,
            std::filesystem::path& selectedEntry,
            std::string& contentStatus) const;
    };
}
