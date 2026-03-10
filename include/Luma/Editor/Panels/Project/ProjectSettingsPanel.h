#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <string_view>

#include "Luma/Core/App/Project.h"

namespace Luma::Editor
{
    struct ProjectSettingsPanelContext
    {
        std::function<void()> resetGameplayInputBindings;
        std::function<void()> onProjectConfigSaved;
        std::function<void(std::string)> setProjectInputStatus;
        std::function<std::string_view()> getProjectInputStatus;
        std::function<void(std::string)> setProjectConfigStatus;
        std::function<std::string_view()> getProjectConfigStatus;
    };

    class ProjectSettingsPanel
    {
    public:
        void Draw(bool* open, const ProjectSettingsPanelContext& context);

        void Reset();
        void InvalidateDraft();
        void CancelInputCapture();

    private:
        struct InputCaptureState
        {
            std::string context;
            std::string action;
            std::size_t bindingIndex = 0;
            bool isAppend = false;
            bool allowAxes = false;
        };

        void DrawProjectSection(const ProjectSettingsPanelContext& context);
        void DrawInputSystemSection(const ProjectSettingsPanelContext& context);
        static void DrawBuildProfileEditor(
            const char* label,
            Project::BuildProfileConfig& config,
            const char* idSuffix);
        void SyncDraftFromLoaded();
        void ClearInputCapture();

        bool m_DraftInitialized = false;
        int m_SectionIndex = 0;
        Project::ProjectConfig m_Draft {};
        InputCaptureState m_InputCapture {};
    };
}
