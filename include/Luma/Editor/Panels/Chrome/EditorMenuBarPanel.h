#pragma once

#include <functional>
#include <string_view>

namespace Luma::Editor
{
    struct EditorMenuBarPanelContext
    {
        bool canSaveScene = false;
        bool canSaveSceneAs = false;
        bool canReloadScene = false;
        bool canSetProjectStartScene = false;

        bool* showProjectSettingsPanel = nullptr;
        bool* showPreferencesPanel = nullptr;
        bool* showPluginsPanel = nullptr;

        bool* showCameraDebugOverlay = nullptr;
        bool* showViewportGrid = nullptr;
        bool* previewSceneCameraLens = nullptr;

        bool* showHierarchyPanel = nullptr;
        bool* showViewportPanel = nullptr;
        bool* showInspectorPanel = nullptr;
        bool* showContentBrowserPanel = nullptr;
        bool* showConsolePanel = nullptr;
        bool* showPackageManagerPanel = nullptr;
        bool* showFooter = nullptr;
        bool* showGpuResourcesPanel = nullptr;

        std::string_view sceneLabel;
        std::string_view projectLabel;

        std::function<void()> requestNewScene;
        std::function<void()> saveScene;
        std::function<void()> saveSceneAs;
        std::function<void()> reloadScene;
        std::function<void()> setProjectStartScene;
    };

    class EditorMenuBarPanel
    {
    public:
        void Draw(const EditorMenuBarPanelContext& context);
    };
}
