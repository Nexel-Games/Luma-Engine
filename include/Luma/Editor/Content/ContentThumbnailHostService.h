#pragma once

#include <filesystem>

#include "Luma/Editor/Content/ContentBrowserCache.h"

namespace Luma
{
    class IRenderBackend;
}

namespace Luma::Editor
{
    class ContentThumbnailHostService final
    {
    public:
        void InvalidateAll(ContentBrowserCache& cache) const;
        void Prune(ContentBrowserCache& cache) const;
        bool EnsureFolderThumbnailLoaded(ContentBrowserCache& cache, IRenderBackend* renderer) const;
        void ReleaseFolderThumbnailTexture(ContentBrowserCache& cache, IRenderBackend* renderer) const;
        void* GetOrCreateThumbnail(
            ContentBrowserCache& cache,
            IRenderBackend* renderer,
            ContentBrowserEntry& entry) const;
        void* GetOrCreateThumbnail(
            ContentBrowserCache& cache,
            IRenderBackend* renderer,
            const std::filesystem::path& entryPath,
            bool isDirectory) const;
    };
}
