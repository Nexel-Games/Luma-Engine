#include "Luma/Editor/Console/ConsoleCommandHostService.h"

namespace Luma::Editor
{
    void ConsoleCommandHostService::Initialize(const ConsoleCommandHostContext& context) const
    {
        if (context.commandService == nullptr)
        {
            return;
        }

        context.commandService->Initialize(BuildInitializationContext(context));
    }

    void ConsoleCommandHostService::Execute(
        const ConsoleCommandHostContext& context,
        const std::string_view commandLine,
        const bool addToHistory) const
    {
        if (context.commandService == nullptr)
        {
            return;
        }

        context.commandService->Execute(BuildExecutionContext(context), commandLine, addToHistory);
    }

    ConsoleCommandInitializationContext ConsoleCommandHostService::BuildInitializationContext(
        const ConsoleCommandHostContext& context) const
    {
        ConsoleCommandInitializationContext initializationContext {};
        initializationContext.commands = context.commands;
        initializationContext.favoriteCommands = context.favoriteCommands;
        initializationContext.cvars = context.cvars;
        initializationContext.tasks = context.tasks;
        initializationContext.vsyncEnabled = context.vsyncEnabled;
        initializationContext.viewportGridEnabled = context.viewportGridEnabled;
        initializationContext.cameraDebugOverlayEnabled = context.cameraDebugOverlayEnabled;
        initializationContext.primitiveMeshLod = context.primitiveMeshLod != nullptr ? *context.primitiveMeshLod : 0u;
        return initializationContext;
    }

    ConsoleCommandExecutionContext ConsoleCommandHostService::BuildExecutionContext(
        const ConsoleCommandHostContext& context) const
    {
        ConsoleCommandExecutionContext executionContext {};
        executionContext.commands = context.commands;
        executionContext.favoriteCommands = context.favoriteCommands;
        executionContext.cvars = context.cvars;
        executionContext.tasks = context.tasks;
        executionContext.commandHistory = context.commandHistory;
        executionContext.recentCommands = context.recentCommands;
        executionContext.historyCursor = context.historyCursor;
        executionContext.showHierarchyPanel = context.showHierarchyPanel;
        executionContext.showViewportPanel = context.showViewportPanel;
        executionContext.showInspectorPanel = context.showInspectorPanel;
        executionContext.showContentBrowserPanel = context.showContentBrowserPanel;
        executionContext.showConsolePanel = context.showConsolePanel;
        executionContext.showPackageManagerPanel = context.showPackageManagerPanel;
        executionContext.showFooter = context.showFooter;
        executionContext.showGpuResourcesPanel = context.showGpuResourcesPanel;
        executionContext.showProjectSettingsPanel = context.showProjectSettingsPanel;
        executionContext.showPreferencesPanel = context.showPreferencesPanel;
        executionContext.viewportGridEnabled = context.viewportGridEnabledState;
        executionContext.cameraDebugOverlayEnabled = context.cameraDebugOverlayEnabledState;
        executionContext.activeProfile = context.activeProfile;
        executionContext.lastDeltaTimeSeconds = context.lastDeltaTimeSeconds;
        executionContext.primitiveMeshLod = context.primitiveMeshLod;
        executionContext.resourceStreamingService = context.resourceStreamingService;
        executionContext.scene = context.scene;
        executionContext.selectSingleEntity = context.selectSingleEntity;
        executionContext.refreshContentBrowser = context.refreshContentBrowser;
        executionContext.openProjectSettings = context.openProjectSettings;
        executionContext.openPreferences = context.openPreferences;
        executionContext.markSceneRenderCacheDirty = context.markSceneRenderCacheDirty;
        executionContext.clearConsoleOutput = context.clearConsoleOutput;
        executionContext.openPackageManager = [&context]()
        {
            if (context.packageManagerHostFacadeService == nullptr)
            {
                return;
            }

            context.packageManagerHostFacadeService->Open(context.packageManagerHostContext);
        };
        executionContext.requestPackagesRefresh = [&context]()
        {
            if (context.packageManagerHostFacadeService == nullptr)
            {
                return;
            }

            context.packageManagerHostFacadeService->RequestRefresh(context.packageManagerHostContext);
        };
        executionContext.requestPackagesInstall = [&context](const std::string_view packageId, const std::string_view version)
        {
            if (context.packageManagerHostFacadeService == nullptr)
            {
                return;
            }

            context.packageManagerHostFacadeService->RequestInstall(context.packageManagerHostContext, packageId, version);
        };
        executionContext.requestPackagesUpdate = [&context](const std::string_view packageId, const std::string_view version)
        {
            if (context.packageManagerHostFacadeService == nullptr)
            {
                return;
            }

            context.packageManagerHostFacadeService->RequestUpdate(context.packageManagerHostContext, packageId, version);
        };
        executionContext.requestPackagesRemove = [&context](const std::string_view packageId)
        {
            if (context.packageManagerHostFacadeService == nullptr)
            {
                return;
            }

            context.packageManagerHostFacadeService->RequestRemove(context.packageManagerHostContext, packageId);
        };
        executionContext.requestPackagesVerify = [&context](const std::string_view packageIdOrEmpty)
        {
            if (context.packageManagerHostFacadeService == nullptr)
            {
                return;
            }

            context.packageManagerHostFacadeService->RequestVerify(context.packageManagerHostContext, packageIdOrEmpty);
        };
        return executionContext;
    }
}
