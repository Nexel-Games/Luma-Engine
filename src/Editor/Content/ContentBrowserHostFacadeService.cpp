#include "Luma/Editor/Content/ContentBrowserHostFacadeService.h"

namespace Luma::Editor
{
    std::filesystem::path ContentBrowserHostFacadeService::ResolveInitialContentRoot(
        const ContentBrowserHostFacadeContext& context) const
    {
        return m_RootService.ResolveInitialContentRoot(context.projectLoaded, context.projectAssetsPath);
    }

    void ContentBrowserHostFacadeService::RefreshRoots(ContentBrowserHostFacadeContext& context) const
    {
        ContentBrowserRootRefreshContext rootRefreshContext {};
        rootRefreshContext.contentRoot = context.contentRoot;
        rootRefreshContext.currentDirectory = context.currentDirectory;
        rootRefreshContext.selectedEntry = context.selectedEntry;
        rootRefreshContext.roots = context.roots;
        rootRefreshContext.activeRootIndex = context.activeRootIndex;
        const std::vector<Assets::PackageMountRoot> mountRoots =
            context.getMountRoots ? context.getMountRoots() : std::vector<Assets::PackageMountRoot> {};
        rootRefreshContext.mountRoots = &mountRoots;
        rootRefreshContext.invalidateFolderTreeCache = [this, &context]()
        {
            InvalidateFolderTreeCache(context);
        };
        rootRefreshContext.clearEntries = [&context]()
        {
            if (context.cache != nullptr)
            {
                context.cache->ClearEntries();
            }
        };
        m_RootService.RefreshRoots(rootRefreshContext);
    }

    const ContentBrowserRootState* ContentBrowserHostFacadeService::GetActiveRoot(
        const ContentBrowserHostFacadeContext& context) const
    {
        if (context.roots == nullptr || context.activeRootIndex == nullptr)
        {
            return nullptr;
        }

        return m_RootService.GetActiveRoot(*context.roots, *context.activeRootIndex);
    }

    void ContentBrowserHostFacadeService::InvalidateFilterCache(ContentBrowserHostFacadeContext& context) const
    {
        if (context.cache != nullptr)
        {
            m_RefreshService.InvalidateFilterCache(*context.cache);
        }
    }

    void ContentBrowserHostFacadeService::InvalidateFolderTreeCache(ContentBrowserHostFacadeContext& context) const
    {
        if (context.cache != nullptr)
        {
            m_RefreshService.InvalidateFolderTreeCache(*context.cache);
        }
    }

    void ContentBrowserHostFacadeService::RebuildFilteredContentEntries(ContentBrowserHostFacadeContext& context) const
    {
        if (context.cache != nullptr)
        {
            m_RefreshService.RebuildFilteredContentEntries(*context.cache);
        }
    }

    void ContentBrowserHostFacadeService::RebuildContentFolderTreeCache(
        ContentBrowserHostFacadeContext& context,
        const std::filesystem::path& rootPath) const
    {
        if (context.cache != nullptr)
        {
            m_RefreshService.RebuildContentFolderTreeCache(*context.cache, rootPath);
        }
    }

    void ContentBrowserHostFacadeService::PumpContentFolderTreeRebuild(ContentBrowserHostFacadeContext& context) const
    {
        if (context.cache != nullptr && context.roots != nullptr && context.activeRootIndex != nullptr)
        {
            m_RefreshService.PumpContentFolderTreeRebuild(*context.cache, *context.roots, *context.activeRootIndex);
        }
    }

    void ContentBrowserHostFacadeService::RefreshContentEntries(ContentBrowserHostFacadeContext& context) const
    {
        if (context.cache != nullptr && context.currentDirectory != nullptr && context.status != nullptr)
        {
            m_RefreshService.RefreshContentEntries(*context.cache, *context.currentDirectory, *context.status);
        }
    }

    void ContentBrowserHostFacadeService::PumpContentEntriesRefresh(ContentBrowserHostFacadeContext& context) const
    {
        if (context.cache != nullptr && context.currentDirectory != nullptr && context.selectedEntry != nullptr &&
            context.status != nullptr)
        {
            m_RefreshService.PumpContentEntriesRefresh(
                *context.cache,
                *context.currentDirectory,
                *context.selectedEntry,
                *context.status);
        }
    }

    void ContentBrowserHostFacadeService::Draw(ContentBrowserHostFacadeContext& context)
    {
        ContentBrowserHostContext hostContext = BuildHostContext(context);
        m_HostService.Draw(hostContext);
    }

    ContentBrowserHostContext ContentBrowserHostFacadeService::BuildHostContext(ContentBrowserHostFacadeContext& context)
    {
        ContentBrowserHostContext hostContext {};
        hostContext.open = context.open;
        hostContext.contentRoot = context.contentRoot;
        hostContext.currentDirectory = context.currentDirectory;
        hostContext.selectedEntry = context.selectedEntry;
        hostContext.roots = context.roots;
        hostContext.activeRootIndex = context.activeRootIndex;
        hostContext.status = context.status;
        hostContext.cache = context.cache;
        hostContext.thumbnailRenderer = context.thumbnailRenderer;
        hostContext.pendingImports = context.pendingImports;
        hostContext.contentImportCursor = context.contentImportCursor;
        hostContext.importedCount = context.importedCount;
        hostContext.skippedCount = context.skippedCount;
        hostContext.failedCount = context.failedCount;
        hostContext.firstError = context.firstError;
        hostContext.contentImportTask = context.contentImportTask;
        hostContext.contentImportActive = context.contentImportActive;
        hostContext.assetPipelineInitialized = context.assetPipelineInitialized;
        hostContext.hasImportPipeline = context.hasImportPipeline;
        hostContext.resolveInitialContentRoot = [this, &context]() -> std::filesystem::path
        {
            return ResolveInitialContentRoot(context);
        };
        hostContext.refreshRoots = [this, &context]()
        {
            RefreshRoots(context);
        };
        hostContext.refreshEntries = [this, &context]()
        {
            RefreshContentEntries(context);
        };
        hostContext.invalidateFolderTreeCache = [this, &context]()
        {
            InvalidateFolderTreeCache(context);
        };
        hostContext.rebuildFolderTreeCache = [this, &context](const std::filesystem::path& rootPath)
        {
            RebuildContentFolderTreeCache(context, rootPath);
        };
        hostContext.requestLoadScene = context.requestLoadScene;
        hostContext.openAsset = context.openAsset;
        return hostContext;
    }
}
