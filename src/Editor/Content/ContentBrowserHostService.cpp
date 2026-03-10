#include "Luma/Editor/Content/ContentBrowserHostService.h"

namespace Luma::Editor
{
    void ContentBrowserHostService::Draw(ContentBrowserHostContext& context)
    {
        ContentBrowserControllerContext controllerContext;
        controllerContext.contentRoot = context.contentRoot;
        controllerContext.currentDirectory = context.currentDirectory;
        controllerContext.selectedEntry = context.selectedEntry;
        controllerContext.roots = context.roots;
        controllerContext.activeRootIndex = context.activeRootIndex;
        controllerContext.status = context.status;
        controllerContext.cache = context.cache;
        controllerContext.thumbnailRenderer = context.thumbnailRenderer;
        controllerContext.pendingImports = context.pendingImports;
        controllerContext.contentImportCursor = context.contentImportCursor;
        controllerContext.importedCount = context.importedCount;
        controllerContext.skippedCount = context.skippedCount;
        controllerContext.failedCount = context.failedCount;
        controllerContext.firstError = context.firstError;
        controllerContext.contentImportTask = context.contentImportTask;
        controllerContext.contentImportActive = context.contentImportActive;
        controllerContext.assetPipelineInitialized = context.assetPipelineInitialized;
        controllerContext.hasImportPipeline = context.hasImportPipeline;
        controllerContext.resolveInitialContentRoot = std::move(context.resolveInitialContentRoot);
        controllerContext.refreshRoots = std::move(context.refreshRoots);
        controllerContext.refreshEntries = std::move(context.refreshEntries);
        controllerContext.invalidateFolderTreeCache = std::move(context.invalidateFolderTreeCache);
        controllerContext.rebuildFolderTreeCache = std::move(context.rebuildFolderTreeCache);
        controllerContext.requestLoadScene = std::move(context.requestLoadScene);
        controllerContext.openAsset = std::move(context.openAsset);
        m_Controller.Draw(context.open, controllerContext);
    }
}
