#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <mutex>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Luma/Core/App/Layer.h"
#include "Luma/Core/App/Project.h"
#include "Luma/Core/App/RenderPipeline.h"
#include "Luma/Asset/Core/AssetRegistry.h"
#include "Luma/Asset/Core/MeshAssetIO.h"
#include "Luma/Asset/Import/ImporterRegistry.h"
#include "Luma/Asset/Import/ImportPipeline.h"
#include "Luma/Asset/Streaming/ResourceStreamingService.h"
#include "Luma/Editor/Content/ContentBrowserCache.h"
#include "Luma/Editor/Content/ContentBrowserHostFacadeService.h"
#include "Luma/Editor/Content/ContentRootWatchService.h"
#include "Luma/Editor/Content/ContentThumbnailHostService.h"
#include "Luma/Editor/Console/ConsoleCommandHostService.h"
#include "Luma/Editor/Console/ConsolePanelHostService.h"
#include "Luma/Editor/Console/ConsoleCommandService.h"
#include "Luma/Editor/Content/ContentImportService.h"
#include "Luma/Editor/Core/EditorDockspaceService.h"
#include "Luma/Editor/Core/EditorIconService.h"
#include "Luma/Editor/Core/EditorSelectionState.h"
#include "Luma/Editor/Core/EditorStatusState.h"
#include "Luma/Editor/Core/GameplayInputBindingService.h"
#include "Luma/Editor/Core/EditorTickCoordinatorService.h"
#include "Luma/Editor/Scene/EntityTemplateCreationService.h"
#include "Luma/Editor/Scene/PrefabWorkflowService.h"
#include "Luma/Editor/Scene/SceneEntityUtilityService.h"
#include "Luma/Editor/Viewport/EditorViewportDebugOverlay.h"
#include "Luma/Editor/Viewport/EditorViewportInteraction.h"
#include "Luma/Editor/Viewport/EditorViewportController.h"
#include "Luma/Editor/Viewport/ViewportAssetDropService.h"
#include "Luma/Editor/Core/EditorTaskManager.h"
#include "Luma/Editor/Panels/Console/CommandPalettePanel.h"
#include "Luma/Editor/Panels/Console/ConsolePanel.h"
#include "Luma/Editor/Panels/Console/ConsoleOutputPanel.h"
#include "Luma/Editor/Panels/Console/ConsoleTasksPanel.h"
#include "Luma/Editor/Panels/Chrome/EditorMenuBarPanel.h"
#include "Luma/Editor/Panels/Chrome/EditorToolbarPanel.h"
#include "Luma/Editor/Panels/Scene/EntityCreationMenu.h"
#include "Luma/Editor/Panels/Chrome/FooterBarPanel.h"
#include "Luma/Editor/Panels/Rendering/GPUResourcesPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorAudioPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorCameraLightingPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorDestructionPanel.h"
#include "Luma/Editor/Panels/Content/ContentBrowserPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorAdvancedPhysicsPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorAddComponentPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorEnvironmentEffectsPanel.h"
#include "Luma/Editor/Panels/Scene/HierarchyPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorEntityPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorFieldBuoyancyPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorJointPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorMaterialPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorMeshRendererPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorPhysicsEventsPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorPhysicsPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorScriptPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorVehiclePhysicsPanel.h"
#include "Luma/Editor/Panels/Packages/PackageManagerPanel.h"
#include "Luma/Editor/Packages/PackageManagerHostFacadeService.h"
#include "Luma/Editor/Packages/PackageManagerHostService.h"
#include "Luma/Editor/Rendering/MaterialRenderProxyCacheService.h"
#include "Luma/Editor/Rendering/PostProcessBlendService.h"
#include "Luma/Editor/Rendering/RenderFrameCoordinatorService.h"
#include "Luma/Editor/Rendering/SceneRenderCacheDirtyFlags.h"
#include "Luma/Editor/Rendering/SceneRenderCacheStateService.h"
#include "Luma/Editor/Rendering/SkyPreviewTextureHostService.h"
#include "Luma/Editor/Panels/Assets/MaterialTextureAssetPickerPanel.h"
#include "Luma/Editor/Panels/Project/PluginsPanel.h"
#include "Luma/Editor/Panels/Project/PreferencesPanel.h"
#include "Luma/Editor/Panels/Project/ProjectSettingsPanel.h"
#include "Luma/Editor/Assets/MaterialTextureAssetPickerService.h"
#include "Luma/Editor/Assets/MeshEntityImportService.h"
#include "Luma/Editor/Assets/MeshStreamingGeometryService.h"
#include "Luma/Editor/Scene/SceneFileService.h"
#include "Luma/Editor/Panels/Viewport/ViewportToolbarPanel.h"
#include "Luma/Editor/Panels/Viewport/ViewportPanel.h"
#include "Luma/Editor/Scene/SceneActionService.h"
#include "Luma/Editor/Scene/SceneActionHostService.h"
#include "Luma/Editor/Scene/SceneBootstrapService.h"
#include "Luma/Editor/Scene/InspectorHostService.h"
#include "Luma/Editor/Scene/SceneDocument.h"
#include "Luma/Editor/Scene/SceneDocumentHostService.h"
#include "Luma/Editor/Scene/SceneStartupHostService.h"
#include "Luma/Editor/Rendering/SceneRenderCacheBuilder.h"
#include "Luma/Editor/Rendering/SceneRenderItemAssemblyService.h"
#include "Luma/Editor/Rendering/SkyEnvironmentService.h"
#include "Luma/Physics/PhysicsSystem.h"
#include "Luma/Renderer/PrimitiveMeshFactory.h"
#include "Luma/RHI/GPUResourceManager.h"
#include "Luma/Scene/EditorRuntimeOnlyComponent.h"
#include "Luma/Scene/MaterialComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/PostProcessComponent.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SkyLightComponent.h"
#include "Luma/Scene/TransformComponent.h"
#include "Luma/Scripting/LuaScriptRuntime.h"

namespace Luma
{
    enum class LogLevel : int;
    class IRenderPipeline;

    enum class EditorPlayState : std::uint8_t
    {
        Stopped = 0,
        Playing,
        Paused
    };

    struct CameraActorMeshState
    {
        bool loadAttempted = false;
        bool loadFailed = false;
        std::filesystem::path resolvedPath;
        std::vector<Assets::MeshScenePart> parts;
        std::vector<MeshDesc> meshes;
    };

    class TriangleLayer final : public Layer
    {
    public:
        TriangleLayer();
        ~TriangleLayer() override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(float deltaTimeSeconds) override;
        void OnRender(IRenderBackend& renderer) override;
        void OnImGuiRender() override;
        bool OnCloseRequested() override;
        bool IsAssetPipelineInitialized() const;
        bool IsPackageRegistryLoaded() const;
        bool HasContentRoots() const;
        bool HasRendererBinding() const;
        bool HasRenderPipeline() const;
        bool IsDockLayoutInitialized() const;
        bool HasSceneEntities() const;
        std::size_t GetAssetRegistryCount() const;
        std::size_t GetContentRootCount() const;
        std::size_t GetContentEntryCount() const;

    private:
        using ContentBrowserRoot = Editor::ContentBrowserRootState;
        using PendingContentImport = Editor::PendingContentImport;
        using ContentBrowserEntry = Editor::ContentBrowserEntry;
        using ContentFolderTreeNode = Editor::ContentFolderTreeNode;

        void DrawDockspace();
        void DrawEditorToolbar();
        void DrawConsolePanel();
        void DrawFooter();
        void DrawGPUResourcesPanel();
        void DrawInspectorPanel();
        void StartSceneAudioPlayback();
        void StopSceneAudioPlayback();
        void UpdateSceneAudioRuntime();
        void EnterPlayMode();
        void TogglePausePlayMode();
        bool StopPlayMode();
        EntityID EnsurePlayModeGameCameraEntity();
        EntityID CreateEntityFromTemplate(Editor::EntityTemplateKind templateKind, EntityID parentEntity = entt::null);
        EntityID CreateEntityFromMeshAsset(
            const std::filesystem::path& assetPath,
            EntityID parentEntity = entt::null,
            const std::array<float, 3>* worldPosition = nullptr);
        Editor::EntityTemplateCreationContext BuildEntityTemplateCreationContext();
        Editor::MeshEntityImportContext BuildMeshEntityImportContext();
        Editor::PrefabWorkflowContext BuildPrefabWorkflowContext();
        Editor::InspectorHostContext BuildInspectorHostContext();
        Editor::HierarchyPanelContext BuildHierarchyPanelContext();
        Editor::ProjectSettingsPanelContext BuildProjectSettingsPanelContext();
        Editor::PluginsPanelContext BuildPluginsPanelContext();
        void DrawViewportPanel();
        void DrawContentBrowserPanel();
        bool EnsureGizmoToolbarIconsLoaded();
        void ReleaseGizmoToolbarIcons();
        void TickContentImportQueue();
        void UpdateStreamingTaskState();
        void HandleStreamingEvent(const Assets::StreamEvent& event);
        EntityID FindEditorCameraEntity() const;
        void RebuildScenePrimitiveMesh();
        void MarkSceneRenderCacheDirty(Editor::SceneRenderCacheDirtyFlags flags = Editor::SceneRenderCacheDirtyFlags::All);
        Editor::MeshStreamingGeometryContext BuildMeshStreamingGeometryContext();
        Editor::SceneRenderCacheBuildContext BuildSceneRenderCacheBuildContext(
            bool collectRenderSources,
            bool buildScenePrimitiveMesh,
            bool buildSkyPrimitiveMesh);
        Editor::RenderFrameCoordinatorContext BuildRenderFrameCoordinatorContext(IRenderBackend& renderer);
        const PrimitiveMeshData* ResolveMeshRendererGeometry(
            const TransformComponent& transform,
            const MeshRendererComponent& meshRenderer);
        std::uint32_t ComputeRequestedMeshLod(
            const TransformComponent& transform,
            const MeshRendererComponent& meshRenderer) const;
        void InitializeConsoleCommands();
        void UpdateConsoleTasks(float deltaTimeSeconds);
        void ExecuteConsoleCommand(std::string_view commandLine, bool addToHistory = true);
        void AddConsoleLine(LogLevel level, std::string_view category, std::string_view message, std::string_view line);
        Editor::ConsoleCommandHostContext BuildConsoleCommandHostContext();
        Editor::ContentBrowserHostFacadeContext BuildContentBrowserHostFacadeContext();
        Editor::PackageManagerHostFacadeContext BuildPackageManagerHostFacadeContext();
        Editor::EditorTickCoordinatorContext BuildEditorTickCoordinatorContext(float deltaTimeSeconds);
        Editor::SceneStartupHostContext BuildSceneStartupHostContext();
        Editor::SceneDocumentHostContext BuildSceneDocumentHostContext();
        Editor::SceneDocumentHostContext BuildSceneDocumentHostContext() const;
        Editor::SceneActionHostContext BuildSceneActionHostContext();
        Editor::SceneBootstrapContext BuildSceneBootstrapContext();
        Editor::SceneBootstrapContext BuildSceneBootstrapContext() const;
        Editor::SceneRenderItemAssemblyContext BuildSceneRenderItemAssemblyContext(
            const std::vector<Editor::PendingSceneRenderSource>& pendingRenderSources,
            std::uint64_t renderItemsStateHash);
        bool TryBuildBakedLightmap(
            EntityID entity,
            const TransformComponent& transform,
            const MeshRendererComponent& meshRenderer,
            const PrimitiveMeshData& geometry,
            const MaterialRenderProxy& material,
            BakedLightmapData& outLightmap) const;
        void RefreshContentBrowserRoots();
        void RefreshContentBrowserEntries();
        void RefreshContentBrowserTreeAndEntries();
        void RefreshContentBrowserAll();
        void OpenContentBrowserAsset(const std::filesystem::path& assetPath, const std::string& entryName);
        EntityID FindPrimarySkyEntity() const;
        void BuildBlendedPostProcessView(
            const std::array<float, 3>& cameraWorldPosition,
            ScenePostProcessView& outPostProcess) const;
        std::filesystem::path ResolveSkyAssetPath(const std::string& path) const;
        bool LoadSceneFromPath(const std::filesystem::path& scenePath);
        bool SaveSceneToPath(const std::filesystem::path& scenePath);
        bool SetProjectStartScene(const std::filesystem::path& scenePath);
        bool CaptureSceneSnapshot(std::string& outSnapshot, std::string& outError) const;
        bool IsSceneDirty();
        void UpdateSceneDirtyState();
        void CreateNewScene();
        void RequestPendingSceneActionClose();
        void RequestNewScene();
        void RequestLoadScene(const std::filesystem::path& scenePath);
        void RequestReloadScene();
        bool SaveActiveScene();
        bool OpenSaveSceneAsPrompt(std::string_view suggestedName = {});
        void DrawUnsavedScenePrompt();
        std::filesystem::path ResolveScenePath(const std::string& scenePath) const;
        std::filesystem::path GetDefaultScenePath() const;
        std::filesystem::path BuildScenePathFromName(std::string_view sceneName) const;
        std::string GetActiveSceneDisplayName() const;
        void RefreshWindowTitle();
        void SeedDefaultSceneEntities();
        bool IsSelectionValid() const;
        bool IsPlayModeActive() const;
        bool IsPlayModePaused() const;
        bool IsSceneSimulationEnabled() const;
        std::vector<UUID> CaptureSelectedEntityUuids() const;
        void RestoreSelectedEntityUuids(const std::vector<UUID>& selectionUuids, UUID primarySelectionUuid);
        Editor::SceneRenderCacheStateContext BuildSceneRenderCacheStateContext() const;
        Editor::SkyPreviewTextureHostContext BuildSkyPreviewTextureHostContext();

        float m_Time = 0.0f;
        RenderPipelineProfile m_ActiveProfile = RenderPipelineProfile::CoreLite;
        std::unique_ptr<IRenderPipeline> m_ActivePipeline;
        GPUResourceManager m_GPUResourceManager;
        IRenderBackend* m_LastRenderer = nullptr;

        bool m_DockLayoutInitialized = false;
        bool m_ShowHierarchyPanel = true;
        bool m_ShowViewportPanel = true;
        bool m_ShowInspectorPanel = true;
        bool m_ShowContentBrowserPanel = true;
        bool m_ShowFooter = true;
        bool m_ShowGPUResourcesPanel = false;
        bool m_ShowConsolePanel = true;
        bool m_ShowColliderDebug = true;
        bool m_ShowProjectSettingsPanel = false;
        bool m_ShowPreferencesPanel = false;
        bool m_ShowPluginsPanel = false;
        bool m_ShowPackageManagerPanel = false;
        float m_LastDeltaTimeSeconds = 0.0f;
        float m_LastContentBrowserTickMs = 0.0f;
        float m_LastStreamingTickMs = 0.0f;
        float m_LastSceneRebuildMs = 0.0f;
        float m_LastSceneViewBuildMs = 0.0f;
        float m_LastRenderFrameMs = 0.0f;

        Scene m_Scene;
        Editor::SceneDocument m_SceneDocument;
        Editor::EditorViewportController m_ViewportController;
        Editor::EditorViewportDebugOverlay m_ViewportDebugOverlay;
        Editor::EditorViewportInteraction m_ViewportInteraction;
        PhysicsSystem m_PhysicsSystem;
        PhysicsSettings m_PhysicsSettings {};
        bool m_PhysicsSimulationEnabled = true;
        Editor::EditorSelectionState m_SelectionState;
        EntityID& m_SelectedEntity;
        std::vector<EntityID>& m_SelectedEntities;
        using ConsoleCommandDesc = Editor::ConsoleCommandDesc;
        using ConsoleEntry = Editor::ConsoleEntry;
        using ConsoleTaskState = Editor::ConsoleTaskState;
        std::vector<ConsoleEntry> m_ConsoleEntries;
        std::vector<ConsoleCommandDesc> m_ConsoleCommands;
        std::vector<ConsoleTaskState> m_ConsoleTasks;
        std::vector<std::string> m_ConsoleCommandHistory;
        std::vector<std::string> m_ConsoleRecentCommands;
        std::vector<std::string> m_ConsoleFavoriteCommands;
        std::unordered_map<std::string, std::string> m_ConsoleCvars;
        std::string m_ConsoleSearchQuery;
        std::string m_CommandPaletteQuery;
        std::string m_ConsoleCommandInput;
        bool m_ConsoleFilterTrace = true;
        bool m_ConsoleFilterInfo = true;
        bool m_ConsoleFilterWarn = true;
        bool m_ConsoleFilterError = true;
        bool m_ConsoleFilterFatal = true;
        bool m_ConsoleAutoScroll = true;
        bool m_ConsoleScrollToBottom = false;
        std::size_t m_ConsoleMaxEntries = 4000;
        int m_ConsoleHistoryCursor = -1;
        std::uint64_t m_ConsoleLogSinkHandle = 0;
        std::mutex m_ConsoleMutex;

        std::filesystem::path m_ContentRoot;
        std::filesystem::path m_CurrentContentDirectory;
        std::filesystem::path m_SelectedContentEntry;
        std::vector<ContentBrowserRoot> m_ContentRoots;
        int m_ActiveContentRootIndex = 0;
        Editor::ContentImportService m_ContentImportService;
        Editor::ConsoleCommandHostService m_ConsoleCommandHostService;
        Editor::ConsolePanelHostService m_ConsolePanelHostService;
        Editor::ConsoleCommandService m_ConsoleCommandService;
        Editor::EditorDockspaceService m_EditorDockspaceService;
        Editor::EditorIconService m_EditorIconService;
        Editor::GameplayInputBindingService m_GameplayInputBindingService;
        Editor::EditorTickCoordinatorService m_EditorTickCoordinatorService;
        Editor::EntityTemplateCreationService m_EntityTemplateCreationService;
        Editor::SceneEntityUtilityService m_SceneEntityUtilityService;
        Editor::PrefabWorkflowService m_PrefabWorkflowService;
        Editor::ViewportAssetDropService m_ViewportAssetDropService;
        Editor::ConsolePanel m_ConsolePanel;
        Editor::EditorMenuBarPanel m_EditorMenuBarPanel;
        Editor::EditorToolbarPanel m_EditorToolbarPanel;
        Editor::EntityCreationMenu m_EntityCreationMenu;
        Editor::FooterBarPanel m_FooterBarPanel;
        Editor::GPUResourcesPanel m_GPUResourcesPanel;
        Editor::HierarchyPanel m_HierarchyPanel;
        Editor::InspectorAddComponentPanel m_InspectorAddComponentPanel;
        Editor::InspectorAdvancedPhysicsPanel m_InspectorAdvancedPhysicsPanel;
        Editor::InspectorAudioPanel m_InspectorAudioPanel;
        Editor::InspectorCameraLightingPanel m_InspectorCameraLightingPanel;
        Editor::InspectorDestructionPanel m_InspectorDestructionPanel;
        Editor::InspectorEntityPanel m_InspectorEntityPanel;
        Editor::InspectorEnvironmentEffectsPanel m_InspectorEnvironmentEffectsPanel;
        Editor::InspectorFieldBuoyancyPanel m_InspectorFieldBuoyancyPanel;
        Editor::InspectorJointPanel m_InspectorJointPanel;
        Editor::InspectorMaterialPanel m_InspectorMaterialPanel;
        Editor::InspectorMeshRendererPanel m_InspectorMeshRendererPanel;
        Editor::InspectorPanel m_InspectorPanel;
        Editor::InspectorPhysicsEventsPanel m_InspectorPhysicsEventsPanel;
        Editor::InspectorPhysicsPanel m_InspectorPhysicsPanel;
        Editor::InspectorScriptPanel m_InspectorScriptPanel;
        Editor::InspectorVehiclePhysicsPanel m_InspectorVehiclePhysicsPanel;
        Editor::MaterialTextureAssetPickerPanel m_MaterialTextureAssetPickerPanel;
        Editor::MaterialRenderProxyCacheService m_MaterialRenderProxyCacheService;
        Editor::PackageManagerPanel m_PackageManagerPanel;
        Editor::PackageManagerHostFacadeService m_PackageManagerHostFacadeService;
        Editor::PackageManagerHostService m_PackageManagerHostService;
        Editor::PluginsPanel m_PluginsPanel;
        Editor::PreferencesPanel m_PreferencesPanel;
        Editor::ProjectSettingsPanel m_ProjectSettingsPanel;
        Editor::RenderFrameCoordinatorService m_RenderFrameCoordinatorService;
        Editor::SceneRenderCacheStateService m_SceneRenderCacheStateService;
        Editor::SceneActionHostService m_SceneActionHostService;
        Editor::SceneBootstrapService m_SceneBootstrapService;
        Editor::InspectorHostService m_InspectorHostService;
        Editor::SceneFileService m_SceneFileService;
        Editor::SceneDocumentHostService m_SceneDocumentHostService;
        Editor::SceneStartupHostService m_SceneStartupHostService;
        Editor::SceneRenderCacheBuilder m_SceneRenderCacheBuilder;
        Editor::SceneRenderItemAssemblyService m_SceneRenderItemAssemblyService;
        Editor::SceneRenderItemAssemblyScratch m_SceneRenderItemAssemblyScratch;
        Editor::SkyEnvironmentService m_SkyEnvironmentService;
        Editor::SkyPreviewTextureHostService m_SkyPreviewTextureHostService;
        Editor::PostProcessBlendService m_PostProcessBlendService;
        Editor::ViewportPanel m_ViewportPanel;
        Editor::ViewportToolbarPanel m_ViewportToolbarPanel;
        Editor::EditorStatusState m_EditorStatus;
        Editor::ContentBrowserCache m_ContentBrowserCache;
        Editor::ContentBrowserHostFacadeService m_ContentBrowserHostFacadeService;
        Editor::ContentRootWatchService m_ContentRootWatchService;
        Editor::ContentThumbnailHostService m_ContentThumbnailHostService;
        Editor::MaterialTextureAssetPickerService m_MaterialTextureAssetPickerService;
        Editor::MeshEntityImportService m_MeshEntityImportService;
        Assets::ResourceStreamingService m_ResourceStreamingService;
        using StreamedMeshAssetState = Editor::MeshStreamingAssetState;
        using ImportedScenePartsState = Editor::MeshStreamingImportedScenePartsState;
        std::unordered_map<std::string, ImportedScenePartsState> m_ImportedSceneParts;
        std::unordered_map<std::string, StreamedMeshAssetState> m_StreamedMeshAssets;
        CameraActorMeshState m_CameraActorMeshState;
        Editor::MeshStreamingGeometryService m_MeshStreamingGeometryService;
        EditorTaskHandle m_StreamingTask = 0;
        bool m_StreamingTaskActive = false;
        std::unordered_set<Assets::StreamRequestHandle> m_StreamingActiveHandles;
        std::uint32_t m_StreamingTaskBatchSize = 0;
        std::string m_StreamingTaskLastMessage;
        Assets::AssetRegistry m_AssetRegistry;
        Assets::ImporterRegistry m_ImporterRegistry;
        std::unique_ptr<Assets::ImportPipeline> m_ImportPipeline;
        bool m_AssetPipelineInitialized = false;
        std::uint32_t m_PrimitiveMeshLod = 0;
        MeshDesc m_ScenePrimitiveMeshDesc;
        std::uint64_t m_ScenePrimitiveMeshRevision = 1;
        std::uint64_t m_ScenePrimitiveMeshHash = 0;
        bool m_HasScenePrimitiveMesh = false;
        MeshDesc m_SkyPrimitiveMeshDesc;
        std::uint64_t m_SkyPrimitiveMeshRevision = 1;
        std::uint64_t m_SkyPrimitiveMeshHash = 0;
        bool m_HasSkyPrimitiveMesh = false;
        std::vector<SceneRenderItem> m_SceneRenderItems;
        std::uint64_t m_SceneRenderItemsRevision = 1;
        std::uint64_t m_SceneRenderItemsHash = 0;
        Editor::SceneRenderCacheDirtyFlags m_RenderSceneCacheDirtyFlags = Editor::SceneRenderCacheDirtyFlags::All;
        bool m_LastViewportGridEnabled = true;
        std::string m_LastSkyMeshSignature;
        EditorPlayState m_PlayState = EditorPlayState::Stopped;
        std::string m_PlaySceneSnapshot;
        std::vector<UUID> m_PlaySelectedEntityUuids;
        UUID m_PlayPrimarySelectedEntityUuid = 0;
        std::filesystem::path m_PlaySelectedContentEntry;
        EntityID m_PlayGameCameraEntity = entt::null;
        LuaScriptRuntime m_LuaScriptRuntime;
        void* m_SkyboxPreviewTexture = nullptr;
        int m_SkyboxPreviewWidth = 0;
        int m_SkyboxPreviewHeight = 0;
        std::filesystem::path m_SkyboxSourcePath;
        std::string m_SkyboxSignature;
        std::array<float, 3> m_SkyAverageColor { 0.0f, 0.0f, 0.0f };
        std::vector<float> m_SkyEnvironmentLinearPixels;
        int m_SkyEnvironmentWidth = 0;
        int m_SkyEnvironmentHeight = 0;
        Editor::SceneActionService m_SceneActionService;
    };
}
