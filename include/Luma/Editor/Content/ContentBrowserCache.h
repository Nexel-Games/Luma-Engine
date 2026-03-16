#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

#include "Luma/Editor/Assets/ThumbnailService.h"

namespace Luma
{
    class IRenderBackend;
}

namespace Luma::Editor
{
    struct ContentBrowserThumbnailStats
    {
        ThumbnailServiceStats serviceStats {};
        std::size_t totalEntryCount = 0;
        std::size_t filteredEntryCount = 0;
        std::size_t newRequestsIssuedThisFrame = 0;
        std::size_t maxNewRequestsPerFrame = 0;
        bool folderThumbnailLoaded = false;
    };

    struct ContentBrowserEntry
    {
        std::filesystem::path path;
        std::string name;
        std::string lowerName;
        std::string genericPath;
        std::string tileId;
        std::uintmax_t byteSize = 0;
        ThumbnailHandle thumbnailHandle = 0;
        bool hasByteSize = false;
        bool supportsThumbnail = false;
        bool isDirectory = false;
        std::uint8_t type = 0;
    };

    struct ContentFolderTreeNode
    {
        std::filesystem::path relativePath;
        std::string relativePathString;
        std::string treeNodeId;
        std::string name;
        std::string lowerName;
        std::vector<std::size_t> children;
    };

    class ContentBrowserCache final
    {
    public:
        ContentBrowserCache() = default;
        ~ContentBrowserCache();

        void Shutdown(IRenderBackend* renderer);
        void Tick(IRenderBackend& renderer);
        void SetThumbnailBudgetMsPerFrame(double milliseconds);
        void SetThumbnailConcurrency(std::size_t concurrency);

        void InvalidateAllThumbnails();
        void PruneThumbnailsToPaths();
        void BeginThumbnailRequestFrame();
        void* GetOrCreateThumbnail(IRenderBackend* renderer, ContentBrowserEntry& entry);
        void* GetOrCreateThumbnail(IRenderBackend* renderer, const std::filesystem::path& entryPath, bool isDirectory);
        bool EnsureFolderThumbnailLoaded(IRenderBackend* renderer);
        bool EnsureScriptThumbnailLoaded(IRenderBackend* renderer);
        bool EnsureAudioThumbnailLoaded(IRenderBackend* renderer);
        bool EnsureAudioMp3ThumbnailLoaded(IRenderBackend* renderer);
        bool EnsureAudioWavThumbnailLoaded(IRenderBackend* renderer);
        void ReleaseFolderThumbnailTexture(IRenderBackend* renderer);
        void ReleaseScriptThumbnailTexture(IRenderBackend* renderer);
        void ReleaseAudioThumbnailTexture(IRenderBackend* renderer);
        void ReleaseAudioMp3ThumbnailTexture(IRenderBackend* renderer);
        void ReleaseAudioWavThumbnailTexture(IRenderBackend* renderer);

        void ClearEntries();
        void InvalidateFilterCache();
        void RebuildFilteredContentEntries();

        void InvalidateFolderTreeCache();
        void RebuildContentFolderTreeCache(const std::filesystem::path& rootPath);
        void PumpContentFolderTreeRebuild(const std::filesystem::path& activeRootPath);

        void RefreshContentEntries(const std::filesystem::path& targetDirectory, std::string& ioStatus);
        void PumpContentEntriesRefresh(
            const std::filesystem::path& currentDirectory,
            std::filesystem::path& ioSelectedEntry,
            std::string& ioStatus);
        ContentBrowserThumbnailStats GetThumbnailStats() const;

        const std::vector<std::filesystem::path>& GetContentEntries() const;
        std::vector<ContentBrowserEntry>& GetContentEntryMetadata();
        const std::vector<ContentBrowserEntry>& GetContentEntryMetadata() const;
        const std::vector<ContentFolderTreeNode>& GetContentFolderTreeNodes() const;
        const std::vector<std::size_t>& GetFilteredContentEntryIndices() const;

        const std::string& GetSearchQuery() const;
        void SetSearchQuery(std::string query);
        int GetTypeFilterIndex() const;
        void SetTypeFilterIndex(int filterIndex);

        const std::filesystem::path& GetContentFolderTreeRootPath() const;
        bool IsContentFolderTreeDirty() const;
        bool IsContentFolderTreeBuildInFlight() const;
        bool IsContentEntriesRefreshInFlight() const;

    private:
        void AdaptThumbnailRequestBudget();

        struct ContentEntriesRefreshResult
        {
            std::filesystem::path directory;
            std::vector<ContentBrowserEntry> entries;
            std::string status;
        };

        struct ContentFolderTreeBuildResult
        {
            std::filesystem::path rootPath;
            std::vector<ContentFolderTreeNode> nodes;
        };

        ThumbnailService m_ThumbnailService;
        std::vector<std::filesystem::path> m_ContentEntries;
        std::vector<ContentBrowserEntry> m_ContentEntryMetadata;
        std::vector<ContentFolderTreeNode> m_ContentFolderTreeNodes;
        std::vector<std::size_t> m_FilteredContentEntryIndices;
        bool m_ContentFilterCacheDirty = true;
        bool m_ContentFolderTreeDirty = true;
        bool m_ContentFolderTreeBuildInFlight = false;
        bool m_ContentFolderTreeBuildQueued = false;
        bool m_ContentEntriesRefreshInFlight = false;
        bool m_ContentEntriesRefreshQueued = false;
        std::filesystem::path m_ContentFolderTreeRootPath;
        std::filesystem::path m_ContentFolderTreeBuildingRootPath;
        std::filesystem::path m_ContentFolderTreeQueuedRootPath;
        std::filesystem::path m_ContentEntriesLoadedDirectory;
        std::filesystem::path m_ContentEntriesRefreshDirectory;
        std::filesystem::path m_ContentEntriesQueuedDirectory;
        std::future<ContentFolderTreeBuildResult> m_ContentFolderTreeFuture;
        std::future<ContentEntriesRefreshResult> m_ContentEntriesRefreshFuture;
        std::string m_SearchQuery;
        int m_TypeFilterIndex = 0;
        void* m_ContentFolderThumbnailTexture = nullptr;
        int m_ContentFolderThumbnailWidth = 0;
        int m_ContentFolderThumbnailHeight = 0;
        std::filesystem::path m_ContentFolderThumbnailSourcePath;
        bool m_ContentFolderThumbnailLookupComplete = false;
        void* m_ScriptThumbnailTexture = nullptr;
        int m_ScriptThumbnailWidth = 0;
        int m_ScriptThumbnailHeight = 0;
        std::filesystem::path m_ScriptThumbnailSourcePath;
        bool m_ScriptThumbnailLookupComplete = false;
        void* m_AudioThumbnailTexture = nullptr;
        int m_AudioThumbnailWidth = 0;
        int m_AudioThumbnailHeight = 0;
        std::filesystem::path m_AudioThumbnailSourcePath;
        bool m_AudioThumbnailLookupComplete = false;
        void* m_AudioMp3ThumbnailTexture = nullptr;
        int m_AudioMp3ThumbnailWidth = 0;
        int m_AudioMp3ThumbnailHeight = 0;
        std::filesystem::path m_AudioMp3ThumbnailSourcePath;
        bool m_AudioMp3ThumbnailLookupComplete = false;
        void* m_AudioWavThumbnailTexture = nullptr;
        int m_AudioWavThumbnailWidth = 0;
        int m_AudioWavThumbnailHeight = 0;
        std::filesystem::path m_AudioWavThumbnailSourcePath;
        bool m_AudioWavThumbnailLookupComplete = false;
        std::size_t m_MaxNewThumbnailRequestsPerFrame = 6;
        std::size_t m_NewThumbnailRequestsIssuedThisFrame = 0;
    };
}
