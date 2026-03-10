#pragma once

#include <vector>

#include "Luma/Editor/Content/ContentBrowserCache.h"
#include "Luma/Asset/Streaming/ResourceStreamingService.h"
#include "Luma/RHI/GPUResourceManager.h"

namespace Luma::Editor
{
    struct GPUResourcesPanelContext
    {
        GPUResourceManager::Stats stats {};
        std::vector<GPUResourceManager::DebugEntry> entries;
        Assets::StreamingStats streamingStats {};
        std::vector<Assets::StreamRecord> streamingRecords;
        ContentBrowserThumbnailStats thumbnailStats {};
        float sceneRebuildMs = 0.0f;
        float sceneViewBuildMs = 0.0f;
        float renderFrameMs = 0.0f;
    };

    class GPUResourcesPanel
    {
    public:
        void Draw(bool* open, const GPUResourcesPanelContext& context);
    };
}
