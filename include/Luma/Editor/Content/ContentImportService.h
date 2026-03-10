#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Luma/Asset/Import/ImportPipeline.h"
#include "Luma/Editor/Content/ContentBrowserController.h"
#include "Luma/Editor/Core/EditorTaskManager.h"

namespace Luma::Editor
{
    struct ContentImportState
    {
        std::vector<PendingContentImport> pendingImports;
        std::size_t cursor = 0;
        int importedCount = 0;
        int skippedCount = 0;
        int failedCount = 0;
        std::string firstError;
        EditorTaskHandle task = 0;
        bool active = false;
    };

    struct ContentImportTickContext
    {
        bool assetPipelineInitialized = false;
        Assets::ImportPipeline* importPipeline = nullptr;
        std::function<void()> invalidateFolderTreeCache;
        std::function<void()> refreshEntries;
        std::function<void(std::string)> setStatus;
    };

    class ContentImportService
    {
    public:
        ContentImportState& State() { return m_State; }
        const ContentImportState& State() const { return m_State; }

        void Tick(const ContentImportTickContext& context);
        void Reset();
        std::string BuildFooterStatus() const;

    private:
        ContentImportState m_State;
    };
}
