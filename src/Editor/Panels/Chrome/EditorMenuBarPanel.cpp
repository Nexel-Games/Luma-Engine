#include "Luma/Editor/Panels/Chrome/EditorMenuBarPanel.h"

#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"

namespace Luma::Editor
{
    namespace
    {
        void ShowItemTooltip(const std::string_view tooltip)
        {
            UI::Tooltip::Show(tooltip);
        }

        void ShowItemTooltipFromLabel(const char* label, const char* prefix = nullptr)
        {
            UI::Tooltip::ShowForItemLabel(label, prefix == nullptr ? std::string_view {} : std::string_view(prefix));
        }

        template <typename... Args>
        bool MenuItemWithTooltip(const char* label, Args&&... args)
        {
            const bool activated = ImGui::MenuItem(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return activated;
        }
    }

    void EditorMenuBarPanel::Draw(const EditorMenuBarPanelContext& context)
    {
        if (!ImGui::BeginMainMenuBar())
        {
            return;
        }

        if (ImGui::BeginMenu("File"))
        {
            ShowItemTooltip("Scene file actions for the current editor scene.");
            if (MenuItemWithTooltip("New Scene") && context.requestNewScene)
            {
                context.requestNewScene();
            }

            if (MenuItemWithTooltip("Save Scene", nullptr, false, context.canSaveScene) && context.saveScene)
            {
                context.saveScene();
            }
            ShowItemTooltip("Save the current scene to its active path.");

            if (MenuItemWithTooltip("Save Scene As...", nullptr, false, context.canSaveSceneAs) && context.saveSceneAs)
            {
                context.saveSceneAs();
            }
            ShowItemTooltip("Save the current scene under a chosen scene name.");

            if (MenuItemWithTooltip("Reload Scene", nullptr, false, context.canReloadScene) && context.reloadScene)
            {
                context.reloadScene();
            }
            ShowItemTooltip("Reload the current scene from disk.");

            if (MenuItemWithTooltip(
                    "Set Current Scene As Project Start Scene",
                    nullptr,
                    false,
                    context.canSetProjectStartScene) &&
                context.setProjectStartScene)
            {
                context.setProjectStartScene();
            }
            ShowItemTooltip("Update project settings so this scene loads on startup.");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit"))
        {
            ShowItemTooltip("Edit menu: project settings and editor preferences.");
            MenuItemWithTooltip("Project Settings", nullptr, context.showProjectSettingsPanel);
            ShowItemTooltip("Open project config: rendering profile, backend, build profiles, and gameplay input.");
            MenuItemWithTooltip("Preferences", nullptr, context.showPreferencesPanel);
            ShowItemTooltip("Open editor preferences for viewport camera controls and debug options.");
            MenuItemWithTooltip("Plugins", nullptr, context.showPluginsPanel);
            ShowItemTooltip("Open the built-in plugin manager for this project.");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ShowItemTooltip("View menu: viewport overlays and debug toggles.");
            MenuItemWithTooltip("Camera Debug Overlay", nullptr, context.showCameraDebugOverlay);
            ShowItemTooltip("Toggle debug text overlay in the viewport.");
            MenuItemWithTooltip("Viewport Grid", nullptr, context.showViewportGrid);
            ShowItemTooltip("Show or hide the viewport grid.");
            ImGui::Separator();
            MenuItemWithTooltip("Preview Scene Camera Lens", nullptr, context.previewSceneCameraLens);
            ShowItemTooltip("Use selected/available scene camera lens values for viewport FOV preview.");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Window"))
        {
            ShowItemTooltip("Window menu: show or hide editor panels.");
            MenuItemWithTooltip("Hierarchy", nullptr, context.showHierarchyPanel);
            ShowItemTooltip("Show or hide the Hierarchy panel.");
            MenuItemWithTooltip("Viewport", nullptr, context.showViewportPanel);
            ShowItemTooltip("Show or hide the Viewport panel.");
            MenuItemWithTooltip("Inspector", nullptr, context.showInspectorPanel);
            ShowItemTooltip("Show or hide the Inspector panel.");
            MenuItemWithTooltip("Content Browser", nullptr, context.showContentBrowserPanel);
            ShowItemTooltip("Show or hide the Content Browser panel.");
            MenuItemWithTooltip("Console", nullptr, context.showConsolePanel);
            ShowItemTooltip("Show or hide the editor Console.");
            MenuItemWithTooltip("Package Manager", nullptr, context.showPackageManagerPanel);
            ShowItemTooltip("Show or hide the Package Manager panel.");
            MenuItemWithTooltip("Footer", nullptr, context.showFooter);
            ShowItemTooltip("Show or hide the status/footer bar.");
            MenuItemWithTooltip("GPU Resources", nullptr, context.showGpuResourcesPanel);
            ShowItemTooltip("Show or hide live GPU resource diagnostics.");
            ImGui::EndMenu();
        }

        const float sceneLabelWidth = ImGui::CalcTextSize(context.sceneLabel.data()).x;
        const float separatorWidth = ImGui::CalcTextSize("  |  ").x;
        const float projectLabelWidth = ImGui::CalcTextSize(context.projectLabel.data()).x;
        const float labelGroupWidth = sceneLabelWidth + separatorWidth + projectLabelWidth;
        const float rightAlignedPosition = ImGui::GetWindowContentRegionMax().x - labelGroupWidth;
        if (rightAlignedPosition > ImGui::GetCursorPosX())
        {
            ImGui::SetCursorPosX(rightAlignedPosition);
        }

        ImGui::TextDisabled("%s", context.sceneLabel.data());
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextDisabled("  |  ");
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::TextUnformatted(context.projectLabel.data());
        ImGui::EndMainMenuBar();
    }
}
