#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Luma/Core/App/Layer.h"
#include "Luma/Core/App/Project.h"
#include "Luma/RHI/RendererAPI.h"

namespace Luma
{
    enum class ProjectBrowserTab
    {
        Recent,
        New
    };

    class ProjectManagerLayer final : public Layer
    {
    public:
        ProjectManagerLayer();
        ~ProjectManagerLayer() override = default;

        void OnAttach() override;
        void OnDetach() override;
        void OnImGuiRender() override;
        void OnRender(IRenderBackend& renderer) override;
        bool ShouldClose() const;
        void Reset();

    private:
        void LoadRecentProjects();
        void SaveRecentProjects() const;
        bool OpenProjectByIndex(std::size_t index);
        bool CreateProjectFromInput();
        bool LaunchSelectedProject(const std::filesystem::path& projectFile);
        bool SaveLoadedProjectRenderingDefaults();
        bool LoadTemplateTextures();
        void ReleaseTemplateTextures();
        bool LoadTextureFromCandidates(
            const std::vector<std::filesystem::path>& candidates,
            void*& outTexture,
            int& outWidth,
            int& outHeight,
            std::filesystem::path& outPath);
        void* GetTemplateTexture(const std::string& templateName) const;
        bool IsBlankProjectTemplate(const std::string& templateName) const;
        void AddRecentProject(const std::filesystem::path& projectRoot);
        std::filesystem::path ProjectFileFromRoot(const std::filesystem::path& projectRoot) const;

        std::filesystem::path m_ProjectsRoot;
        std::filesystem::path m_RecentProjectsFile;
        std::vector<std::filesystem::path> m_RecentProjects;

        ProjectBrowserTab m_ActiveTab = ProjectBrowserTab::Recent;
        std::string m_SearchQuery;
        int m_SelectedRecentIndex = -1;

        std::vector<std::string> m_Templates = { "Blank Project", "First Person", "Third Person" };
        int m_SelectedTemplateIndex = 0;
        std::string m_SelectedTemplate = "Blank Project";
        std::string m_NewProjectName = "MyNewProject";
        std::string m_NewProjectPath;
        RenderPipelineProfile m_SelectedPipelineProfile = RenderPipelineProfile::CoreLite;
        BackendPreference m_SelectedBackendPreference = BackendPreference::Auto;
        bool m_SaveAsProjectDefault = true;
        bool m_VSyncEnabled = true;
        RendererAPI m_RuntimeAPI = RendererAPI::OpenGL;
        int m_LastConfiguredRecentIndex = -1;
        bool m_TemplateTexturesLoaded = false;
        IRenderBackend* m_RenderBackend = nullptr;
        bool m_ShouldClose = false;

        std::string m_StatusMessage;

        void* m_FirstPersonTexture = nullptr;
        int m_FirstPersonTextureWidth = 0;
        int m_FirstPersonTextureHeight = 0;
        std::filesystem::path m_FirstPersonTexturePath;

        void* m_ThirdPersonTexture = nullptr;
        int m_ThirdPersonTextureWidth = 0;
        int m_ThirdPersonTextureHeight = 0;
        std::filesystem::path m_ThirdPersonTexturePath;

        void* m_BlankProjectTexture = nullptr;
        int m_BlankProjectTextureWidth = 0;
        int m_BlankProjectTextureHeight = 0;
        std::filesystem::path m_BlankProjectTexturePath;
    };
}
