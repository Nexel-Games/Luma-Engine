#include "Luma/Layers/TriangleLayer.h"

namespace Luma
{
    Editor::SceneDocumentHostContext TriangleLayer::BuildSceneDocumentHostContext()
    {
        Editor::SceneDocumentHostContext context {};
        context.scene = &m_Scene;
        context.sceneDocument = &m_SceneDocument;
        context.contentStatus = &m_EditorStatus.Content();
        context.selectedContentEntry = &m_SelectedContentEntry;
        return context;
    }

    Editor::SceneDocumentHostContext TriangleLayer::BuildSceneDocumentHostContext() const
    {
        Editor::SceneDocumentHostContext context {};
        context.scene = const_cast<Scene*>(&m_Scene);
        context.sceneDocument = const_cast<Editor::SceneDocument*>(&m_SceneDocument);
        return context;
    }

    Editor::SceneStartupHostContext TriangleLayer::BuildSceneStartupHostContext()
    {
        Editor::SceneStartupHostContext context {};
        context.scene = &m_Scene;
        context.sceneDocument = &m_SceneDocument;
        context.contentStatus = &m_EditorStatus.Content();
        context.selectedContentEntry = &m_SelectedContentEntry;
        context.projectLoaded = Project::IsLoaded();
        context.loadSceneFromPath = [this](const std::filesystem::path& scenePath) -> bool
        {
            return LoadSceneFromPath(scenePath);
        };
        context.seedDefaultSceneEntities = [this]()
        {
            SeedDefaultSceneEntities();
        };
        context.refreshWindowTitle = [this]()
        {
            RefreshWindowTitle();
        };
        context.updateWorldTransforms = [this]()
        {
            m_Scene.UpdateWorldTransforms();
        };
        return context;
    }

    Editor::SceneActionHostContext TriangleLayer::BuildSceneActionHostContext()
    {
        Editor::SceneActionHostContext context {};
        context.actionService = &m_SceneActionService;
        context.sceneFileService = &m_SceneFileService;
        context.sceneDocument = &m_SceneDocument;
        context.contentStatus = &m_EditorStatus.Content();
        context.isSceneDirty = [this]() -> bool
        {
            return IsSceneDirty();
        };
        context.createNewScene = [this]()
        {
            CreateNewScene();
        };
        context.loadScene = [this](const std::filesystem::path& scenePath)
        {
            LoadSceneFromPath(scenePath);
        };
        context.saveSceneToPath = [this](const std::filesystem::path& scenePath) -> bool
        {
            return SaveSceneToPath(scenePath);
        };
        return context;
    }

    Editor::SceneBootstrapContext TriangleLayer::BuildSceneBootstrapContext()
    {
        Editor::SceneBootstrapContext context {};
        context.scene = &m_Scene;
        context.selectedEntity = m_SelectedEntity;
        context.isEntitySelected = [this](const EntityID entity) -> bool
        {
            return m_SceneEntityUtilityService.IsEntitySelected(m_SelectionState, entity);
        };
        context.clearEntitySelection = [this]()
        {
            m_SceneEntityUtilityService.ClearEntitySelection(m_SelectionState);
        };
        context.selectSingleEntity = [this](const EntityID entity)
        {
            Editor::SceneEntitySelectionContext selectionContext { &m_Scene, &m_SelectionState, &m_SelectedContentEntry };
            m_SceneEntityUtilityService.SelectSingleEntity(selectionContext, entity);
        };
        context.initializeSkyLightDefaults = [this](SkyLightComponent& skyLight)
        {
            m_SceneEntityUtilityService.InitializeSkyLightDefaults(skyLight);
        };
        context.resetViewportCamera = [this]()
        {
            m_ViewportController.ResetCameraToDefault();
        };
        return context;
    }

    Editor::SceneBootstrapContext TriangleLayer::BuildSceneBootstrapContext() const
    {
        Editor::SceneBootstrapContext context {};
        context.scene = const_cast<Scene*>(&m_Scene);
        context.selectedEntity = m_SelectedEntity;
        context.isEntitySelected = [this](const EntityID entity) -> bool
        {
            return m_SceneEntityUtilityService.IsEntitySelected(m_SelectionState, entity);
        };
        return context;
    }

    bool TriangleLayer::LoadSceneFromPath(const std::filesystem::path& scenePath)
    {
        Editor::SceneDocumentHostContext context = BuildSceneDocumentHostContext();
        context.afterLoad = [this]()
        {
            m_SceneEntityUtilityService.ClearEntitySelection(m_SelectionState);
            m_ViewportController.ResetCameraToDefault();

            const auto roots = m_Scene.GetRootEntities();
            if (!roots.empty())
            {
                Editor::SceneEntitySelectionContext selectionContext { &m_Scene, &m_SelectionState, &m_SelectedContentEntry };
                m_SceneEntityUtilityService.SelectSingleEntity(selectionContext, roots.front());
            }
        };

        const bool loaded = m_SceneDocumentHostService.LoadSceneFromPath(context, scenePath);
        if (loaded)
        {
            MarkSceneRenderCacheDirty(Editor::SceneRenderCacheDirtyFlags::All);
        }
        return loaded;
    }

    bool TriangleLayer::SaveSceneToPath(const std::filesystem::path& scenePath)
    {
        Editor::SceneDocumentHostContext context = BuildSceneDocumentHostContext();
        context.beforeSave = [this]()
        {
            m_Scene.UpdateWorldTransforms();
        };
        context.afterSave = [this]()
        {
            RefreshContentBrowserTreeAndEntries();
        };

        return m_SceneDocumentHostService.SaveSceneToPath(context, scenePath);
    }

    bool TriangleLayer::SetProjectStartScene(const std::filesystem::path& scenePath)
    {
        return m_SceneFileService.SetProjectStartScene(
            scenePath,
            m_EditorStatus.ProjectConfig(),
            [this]()
            {
                m_ProjectSettingsPanel.InvalidateDraft();
            });
    }

    bool TriangleLayer::CaptureSceneSnapshot(std::string& outSnapshot, std::string& outError) const
    {
        Editor::SceneDocumentHostContext context = BuildSceneDocumentHostContext();
        return m_SceneDocumentHostService.CaptureSceneSnapshot(context, outSnapshot, outError);
    }

    bool TriangleLayer::IsSceneDirty()
    {
        Editor::SceneDocumentHostContext context = BuildSceneDocumentHostContext();
        return m_SceneDocumentHostService.IsSceneDirty(context);
    }

    void TriangleLayer::UpdateSceneDirtyState()
    {
        Editor::SceneDocumentHostContext context = BuildSceneDocumentHostContext();
        m_SceneDocumentHostService.UpdateSceneDirtyState(context);
    }

    void TriangleLayer::CreateNewScene()
    {
        Editor::SceneDocumentHostContext context = BuildSceneDocumentHostContext();
        context.afterNewScene = [this]()
        {
            m_SelectedContentEntry.clear();
            SeedDefaultSceneEntities();
        };

        m_SceneDocumentHostService.CreateNewScene(context);
    }

    void TriangleLayer::RequestPendingSceneActionClose()
    {
        Editor::SceneActionHostContext context = BuildSceneActionHostContext();
        m_SceneActionHostService.RequestPendingClose(context);
    }

    void TriangleLayer::RequestNewScene()
    {
        if (IsPlayModeActive() && !StopPlayMode())
        {
            return;
        }

        Editor::SceneActionHostContext context = BuildSceneActionHostContext();
        m_SceneActionHostService.RequestNewScene(context);
    }

    void TriangleLayer::RequestLoadScene(const std::filesystem::path& scenePath)
    {
        if (IsPlayModeActive() && !StopPlayMode())
        {
            return;
        }

        Editor::SceneActionHostContext context = BuildSceneActionHostContext();
        m_SceneActionHostService.RequestLoadScene(context, scenePath);
    }

    void TriangleLayer::RequestReloadScene()
    {
        if (IsPlayModeActive() && !StopPlayMode())
        {
            return;
        }

        Editor::SceneActionHostContext context = BuildSceneActionHostContext();
        m_SceneActionHostService.RequestReloadScene(context);
    }

    bool TriangleLayer::SaveActiveScene()
    {
        if (IsPlayModeActive() && !StopPlayMode())
        {
            return false;
        }

        Editor::SceneActionHostContext context = BuildSceneActionHostContext();
        return m_SceneActionHostService.SaveActiveScene(context);
    }

    bool TriangleLayer::OpenSaveSceneAsPrompt(std::string_view suggestedName)
    {
        if (IsPlayModeActive() && !StopPlayMode())
        {
            return false;
        }

        Editor::SceneActionHostContext context = BuildSceneActionHostContext();
        return m_SceneActionHostService.OpenSaveSceneAsPrompt(context, suggestedName);
    }

    void TriangleLayer::DrawUnsavedScenePrompt()
    {
        Editor::SceneActionHostContext context = BuildSceneActionHostContext();
        m_SceneActionHostService.DrawUnsavedScenePrompt(context);
    }

    std::filesystem::path TriangleLayer::ResolveScenePath(const std::string& scenePath) const
    {
        return m_SceneFileService.ResolveScenePath(m_SceneDocument, scenePath);
    }

    std::filesystem::path TriangleLayer::GetDefaultScenePath() const
    {
        return m_SceneFileService.GetDefaultScenePath(m_SceneDocument);
    }

    std::filesystem::path TriangleLayer::BuildScenePathFromName(const std::string_view sceneName) const
    {
        return m_SceneFileService.BuildScenePathFromName(m_SceneDocument, sceneName);
    }

    std::string TriangleLayer::GetActiveSceneDisplayName() const
    {
        return m_SceneFileService.GetActiveSceneDisplayName(m_SceneDocument);
    }

    void TriangleLayer::RefreshWindowTitle()
    {
        Editor::SceneDocumentHostContext context = BuildSceneDocumentHostContext();
        m_SceneDocumentHostService.RefreshWindowTitle(context);
    }

    void TriangleLayer::SeedDefaultSceneEntities()
    {
        Editor::SceneBootstrapContext context = BuildSceneBootstrapContext();
        m_SceneBootstrapService.SeedDefaultSceneEntities(context);
    }

    bool TriangleLayer::IsSelectionValid() const
    {
        Editor::SceneBootstrapContext context = BuildSceneBootstrapContext();
        return m_SceneBootstrapService.IsSelectionValid(context);
    }
}
