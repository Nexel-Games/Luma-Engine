#include "Luma/Editor/Content/ContentThumbnailHostService.h"

namespace Luma::Editor
{
    void ContentThumbnailHostService::InvalidateAll(ContentBrowserCache& cache) const
    {
        cache.InvalidateAllThumbnails();
    }

    void ContentThumbnailHostService::Prune(ContentBrowserCache& cache) const
    {
        cache.PruneThumbnailsToPaths();
    }

    bool ContentThumbnailHostService::EnsureFolderThumbnailLoaded(
        ContentBrowserCache& cache,
        IRenderBackend* renderer) const
    {
        return cache.EnsureFolderThumbnailLoaded(renderer);
    }

    void ContentThumbnailHostService::ReleaseFolderThumbnailTexture(
        ContentBrowserCache& cache,
        IRenderBackend* renderer) const
    {
        cache.ReleaseFolderThumbnailTexture(renderer);
    }

    void* ContentThumbnailHostService::GetOrCreateThumbnail(
        ContentBrowserCache& cache,
        IRenderBackend* renderer,
        ContentBrowserEntry& entry) const
    {
        return cache.GetOrCreateThumbnail(renderer, entry);
    }

    void* ContentThumbnailHostService::GetOrCreateThumbnail(
        ContentBrowserCache& cache,
        IRenderBackend* renderer,
        const std::filesystem::path& entryPath,
        const bool isDirectory) const
    {
        return cache.GetOrCreateThumbnail(renderer, entryPath, isDirectory);
    }
}
