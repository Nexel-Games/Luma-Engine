#include "Luma/Editor/Scene/SceneActionPrompt.h"

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"

namespace Luma::Editor
{
    namespace
    {
        bool ButtonWithTooltip(const char* label, const std::string_view tooltip)
        {
            const bool pressed = ImGui::Button(label);
            UI::Tooltip::Show(tooltip);
            return pressed;
        }
    }

    void SceneActionPrompt::RequestExit()
    {
        Request(Action::ExitApplication, {});
    }

    void SceneActionPrompt::RequestNewScene()
    {
        Request(Action::NewScene, {});
    }

    void SceneActionPrompt::RequestLoadScene(const std::filesystem::path& scenePath)
    {
        Request(Action::LoadScene, scenePath.lexically_normal());
    }

    void SceneActionPrompt::RequestReloadScene(const std::filesystem::path& scenePath)
    {
        Request(Action::ReloadScene, scenePath.lexically_normal());
    }

    bool SceneActionPrompt::HasPendingAction() const
    {
        return m_PendingAction != Action::None;
    }

    SceneActionPrompt::Action SceneActionPrompt::GetPendingAction() const
    {
        return m_PendingAction;
    }

    const std::filesystem::path& SceneActionPrompt::GetPendingScenePath() const
    {
        return m_PendingScenePath;
    }

    void SceneActionPrompt::Clear()
    {
        m_PendingAction = Action::None;
        m_PendingScenePath.clear();
        m_OpenPrompt = false;
    }

    void SceneActionPrompt::DrawModal(const Callbacks& callbacks)
    {
        if (m_OpenPrompt)
        {
            ImGui::OpenPopup("Unsaved Scene Changes");
            m_OpenPrompt = false;
        }

        if (!ImGui::BeginPopupModal("Unsaved Scene Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            return;
        }

        ImGui::TextWrapped("The current scene has unsaved changes.");
        ImGui::Spacing();
        ImGui::TextWrapped("Save before continuing?");
        ImGui::Spacing();

        if (ButtonWithTooltip("Save", "Save the current scene before continuing."))
        {
            const bool handled = !callbacks.onSave || callbacks.onSave(m_PendingAction, m_PendingScenePath);
            if (handled)
            {
                Clear();
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();
        if (ButtonWithTooltip("Discard", "Discard unsaved scene changes and continue."))
        {
            if (callbacks.onDiscard)
            {
                callbacks.onDiscard(m_PendingAction, m_PendingScenePath);
            }
            Clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ButtonWithTooltip("Cancel", "Cancel the pending scene action."))
        {
            if (callbacks.onCancel)
            {
                callbacks.onCancel();
            }
            Clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    void SceneActionPrompt::Request(Action action, std::filesystem::path scenePath)
    {
        m_PendingAction = action;
        m_PendingScenePath = std::move(scenePath);
        m_OpenPrompt = true;
    }
}
