#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Luma/Editor/Content/ContentBrowserController.h"

namespace Luma::Editor
{
    struct ContentBrowserHostContext
    {
        bool* open = nullptr;
        std::filesystem::path* contentRoot = nullptr;
        std::filesystem::path* currentDirectory = nullptr;
        std::filesystem::path* selectedEntry = nullptr;
        std::vector<ContentBrowserRootState>* roots = nullptr;
        int* activeRootIndex = nullptr;
        std::string* status = nullptr;
        ContentBrowserCache* cache = nullptr;
        IRenderBackend* thumbnailRenderer = nullptr;

        std::vector<PendingContentImport>* pendingImports = nullptr;
        std::size_t* contentImportCursor = nullptr;
        int* importedCount = nullptr;
        int* skippedCount = nullptr;
        int* failedCount = nullptr;
        std::string* firstError = nullptr;
        EditorTaskHandle* contentImportTask = nullptr;
        bool* contentImportActive = nullptr;

        bool assetPipelineInitialized = false;
        bool hasImportPipeline = false;

        std::function<std::filesystem::path()> resolveInitialContentRoot;
        std::function<void()> refreshRoots;
        std::function<void()> refreshEntries;
        std::function<void()> invalidateFolderTreeCache;
        std::function<void(const std::filesystem::path&)> rebuildFolderTreeCache;
        std::function<void(const std::filesystem::path&)> requestLoadScene;
        std::function<void(const std::filesystem::path&, const std::string&)> openAsset;
    };

    class ContentBrowserHostService
    {
    public:
        void Draw(ContentBrowserHostContext& context);

    private:
        ContentBrowserController m_Controller;
    };
}
