#include "Luma/Editor/Panels/Project/PreferencesPanel.h"

#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Editor/Viewport/EditorViewportController.h"
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
        bool CheckboxWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::Checkbox(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Toggle ");
            return changed;
        }

        template <typename... Args>
        bool SliderFloatWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::SliderFloat(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }
    }

    void PreferencesPanel::Draw(bool* open, EditorViewportController& viewportController)
    {
        ImGui::SetNextWindowSize(ImVec2(920.0f, 620.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Preferences", open))
        {
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted("Editor Preferences");
        ImGui::Separator();
        ImGui::TextWrapped(
            "Editor input mapping is no longer configured in Preferences. "
            "Viewport camera controls use the built-in raw viewport input path.");
        ImGui::Spacing();
        ImGui::TextUnformatted("Viewport Camera");
        auto& viewportCamera = viewportController.Camera();
        SliderFloatWithTooltip("Look Sensitivity", &viewportCamera.lookSensitivity, 0.02f, 1.0f, "%.3f");
        ShowItemTooltip("Mouse look sensitivity for viewport camera.");
        SliderFloatWithTooltip("Move Speed", &viewportCamera.moveSpeed, 0.5f, 48.0f, "%.2f");
        ShowItemTooltip("Keyboard move speed for viewport camera.");
        SliderFloatWithTooltip("Pan Speed", &viewportCamera.panSpeed, 0.001f, 0.10f, "%.4f");
        ShowItemTooltip("Middle-mouse pan speed.");
        SliderFloatWithTooltip("Zoom Speed", &viewportCamera.zoomSpeed, 0.1f, 8.0f, "%.2f");
        ShowItemTooltip("Mouse wheel zoom speed.");
        CheckboxWithTooltip("Camera Debug Overlay", &viewportController.ShowCameraDebugOverlay());
        ShowItemTooltip("Show camera yaw/pitch/debug text in viewport.");
        CheckboxWithTooltip("Preview Scene Camera Lens", &viewportController.PreviewSceneCameraLens());
        ShowItemTooltip("Use scene camera lens FOV for viewport preview.");

        ImGui::End();
    }
}
