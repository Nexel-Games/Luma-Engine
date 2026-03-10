#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Luma/Asset/Package/PackageTypes.h"
#include "Luma/Editor/Content/ContentBrowserHostService.h"
#include "Luma/Editor/Content/ContentBrowserRefreshService.h"
#include "Luma/Editor/Content/ContentBrowserRootService.h"

namespace Luma::Editor
{
    struct ContentBrowserHostFacadeContext
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
        bool projectLoaded = false;
        std::filesystem::path projectAssetsPath;

        std::function<std::vector<Assets::PackageMountRoot>()> getMountRoots;
        std::function<void(const std::filesystem::path&)> requestLoadScene;
        std::function<void(const std::filesystem::path&, const std::string&)> openAsset;
    };

    class ContentBrowserHostFacadeService
    {
    public:
        std::filesystem::path ResolveInitialContentRoot(const ContentBrowserHostFacadeContext& context) const;
        void RefreshRoots(ContentBrowserHostFacadeContext& context) const;
        const ContentBrowserRootState* GetActiveRoot(const ContentBrowserHostFacadeContext& context) const;
        void InvalidateFilterCache(ContentBrowserHostFacadeContext& context) const;
        void InvalidateFolderTreeCache(ContentBrowserHostFacadeContext& context) const;
        void RebuildFilteredContentEntries(ContentBrowserHostFacadeContext& context) const;
        void RebuildContentFolderTreeCache(ContentBrowserHostFacadeContext& context, const std::filesystem::path& rootPath) const;
        void PumpContentFolderTreeRebuild(ContentBrowserHostFacadeContext& context) const;
        void RefreshContentEntries(ContentBrowserHostFacadeContext& context) const;
        void PumpContentEntriesRefresh(ContentBrowserHostFacadeContext& context) const;
        void Draw(ContentBrowserHostFacadeContext& context);

    private:
        ContentBrowserHostContext BuildHostContext(ContentBrowserHostFacadeContext& context);

        ContentBrowserRootService m_RootService;
        ContentBrowserRefreshService m_RefreshService;
        ContentBrowserHostService m_HostService;
    };
}
