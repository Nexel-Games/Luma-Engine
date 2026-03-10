#include "Luma/Layers/ProjectManagerLayer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "Luma/Core/App/Project.h"
#include "Luma/Core/App/RenderSelection.h"
#include "Luma/RHI/IRenderBackend.h"

namespace Luma
{
    namespace
    {
        const char* PipelineLabel(const RenderPipelineProfile profile)
        {
            switch (profile)
            {
            case RenderPipelineProfile::CoreX:
                return "CoreX";
            case RenderPipelineProfile::CoreLite:
            default:
                return "CoreLite";
            }
        }

        const char* APICliArgument(const RendererAPI api)
        {
            (void)api;
            return "opengl";
        }

        const char* PipelineCliArgument(const RenderPipelineProfile profile)
        {
            return profile == RenderPipelineProfile::CoreX ? "corex" : "corelite";
        }

        const char* BackendCliArgument(const BackendPreference backend)
        {
            (void)backend;
            return "opengl";
        }

        std::string QuoteArgument(const std::string& value)
        {
            return "\"" + value + "\"";
        }

        std::filesystem::path GetExecutablePath()
        {
#ifdef _WIN32
            std::array<char, MAX_PATH> pathBuffer {};
            const DWORD length = GetModuleFileNameA(nullptr, pathBuffer.data(), static_cast<DWORD>(pathBuffer.size()));
            if (length > 0 && length < pathBuffer.size())
            {
                return std::filesystem::path(std::string(pathBuffer.data(), length));
            }
#endif
            return {};
        }
    }

    ProjectManagerLayer::ProjectManagerLayer()
        : Layer("ProjectManagerLayer")
    {
    }

    void ProjectManagerLayer::OnAttach()
    {
        std::filesystem::path projectsRoot;

#ifdef _WIN32
        if (const char* userProfile = std::getenv("USERPROFILE"); userProfile != nullptr && *userProfile != '\0')
        {
            projectsRoot = std::filesystem::path(userProfile).root_path() / "MyProjects";
        }

        if (projectsRoot.empty())
        {
            if (const char* systemDrive = std::getenv("SystemDrive"); systemDrive != nullptr && *systemDrive != '\0')
            {
                projectsRoot = std::filesystem::path(systemDrive) / "MyProjects";
            }
        }

        if (projectsRoot.empty())
        {
            projectsRoot = std::filesystem::path("C:/MyProjects");
        }
#else
        projectsRoot = std::filesystem::path("/MyProjects");
#endif

        m_ProjectsRoot = std::move(projectsRoot);
        m_RecentProjectsFile = m_ProjectsRoot / "recent_projects.txt";
        std::filesystem::create_directories(m_ProjectsRoot);
        m_NewProjectPath = m_ProjectsRoot.string();
        m_SelectedPipelineProfile = RenderPipelineProfile::CoreLite;
        m_SelectedBackendPreference = BackendPreference::OpenGL;
        m_SaveAsProjectDefault = true;
        m_VSyncEnabled = true;
        m_LastConfiguredRecentIndex = -1;
        m_TemplateTexturesLoaded = false;
        m_RenderBackend = nullptr;
        m_ShouldClose = false;

        LoadRecentProjects();
        if (!m_RecentProjects.empty())
        {
            m_SelectedRecentIndex = 0;
        }

    }

    void ProjectManagerLayer::OnDetach()
    {
        ReleaseTemplateTextures();
        SaveRecentProjects();
        m_RenderBackend = nullptr;
    }

    void ProjectManagerLayer::OnRender(IRenderBackend& renderer)
    {
        m_RenderBackend = &renderer;
        m_RuntimeAPI = renderer.GetAPI();
    }

    bool ProjectManagerLayer::ShouldClose() const
    {
        return m_ShouldClose;
    }

    void ProjectManagerLayer::Reset()
    {
        m_ShouldClose = false;
        m_StatusMessage.clear();
    }

    void ProjectManagerLayer::OnImGuiRender()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return;
        }

        if (!m_TemplateTexturesLoaded && m_RenderBackend != nullptr && ImGui::GetCurrentContext() != nullptr)
        {
            m_TemplateTexturesLoaded = LoadTemplateTextures();
        }

        const ImVec4 accentColor(0.26f, 0.58f, 0.98f, 1.0f);
        const ImU32 accentBorder = IM_COL32(66, 148, 250, 255);
        const ImU32 cardBg = IM_COL32(28, 30, 36, 255);
        const ImU32 cardBgHovered = IM_COL32(33, 36, 43, 255);
        const ImU32 cardBgSelected = IM_COL32(37, 43, 56, 255);
        const ImU32 cardBorder = IM_COL32(72, 78, 90, 255);

        auto containsInsensitive = [](const std::string& value, const std::string& query)
        {
            if (query.empty())
            {
                return true;
            }

            std::string loweredValue = value;
            std::string loweredQuery = query;

            std::transform(
                loweredValue.begin(),
                loweredValue.end(),
                loweredValue.begin(),
                [](const unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });
            std::transform(
                loweredQuery.begin(),
                loweredQuery.end(),
                loweredQuery.begin(),
                [](const unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });

            return loweredValue.find(loweredQuery) != std::string::npos;
        };

        ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.08f, 0.09f, 0.10f, 1.0f));
        ImGui::Begin(
            "##ProjectBrowserPanel",
            nullptr,
            ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoDocking |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoTitleBar);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);

        if (Project::IsLoaded())
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.12f, 0.14f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
            ImGui::BeginChild("HeaderBar", ImVec2(0.0f, 34.0f), true, ImGuiWindowFlags_NoScrollbar);
            const std::string loadedText = "Loaded: " + Project::GetConfig().name;
            const float loadedTextWidth = ImGui::CalcTextSize(loadedText.c_str()).x;
            const float rightAlignedX = ImGui::GetWindowContentRegionMax().x - loadedTextWidth;
            if (rightAlignedX > ImGui::GetCursorPosX())
            {
                ImGui::SetCursorPosX(rightAlignedX);
            }
            ImGui::TextColored(ImVec4(0.58f, 0.82f, 0.67f, 1.0f), "%s", loadedText.c_str());
            ImGui::EndChild();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        const ImVec4 inactiveTabColor(0.17f, 0.18f, 0.20f, 1.0f);
        const ImVec4 inactiveTabHovered(0.23f, 0.24f, 0.27f, 1.0f);
        auto drawTabButton = [&](const char* label, const bool active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, active ? accentColor : inactiveTabColor);
            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                active ? ImVec4(0.32f, 0.64f, 1.0f, 1.0f) : inactiveTabHovered);
            ImGui::PushStyleColor(
                ImGuiCol_ButtonActive,
                active ? ImVec4(0.21f, 0.49f, 0.88f, 1.0f) : ImVec4(0.27f, 0.28f, 0.32f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            const bool pressed = ImGui::Button(label, ImVec2(136.0f, 32.0f));
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            return pressed;
        };

        if (drawTabButton("Projects", m_ActiveTab == ProjectBrowserTab::Recent))
        {
            m_ActiveTab = ProjectBrowserTab::Recent;
        }
        ImGui::SameLine();
        if (drawTabButton("New Project", m_ActiveTab == ProjectBrowserTab::New))
        {
            m_ActiveTab = ProjectBrowserTab::New;
        }
        ImGui::SameLine();
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 18.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 8.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.03f, 0.03f, 0.04f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.06f, 0.06f, 0.07f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.08f, 0.08f, 0.10f, 1.0f));
        ImGui::SetNextItemWidth(-1.0f);
        std::array<char, 256> searchBuffer {};
        std::snprintf(searchBuffer.data(), searchBuffer.size(), "%s", m_SearchQuery.c_str());
        if (ImGui::InputTextWithHint("##ProjectSearch", "Search projects or templates...", searchBuffer.data(), searchBuffer.size()))
        {
            m_SearchQuery = searchBuffer.data();
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        std::vector<int> visibleIndices;
        if (m_ActiveTab == ProjectBrowserTab::Recent)
        {
            visibleIndices.reserve(m_RecentProjects.size());
            for (int i = 0; i < static_cast<int>(m_RecentProjects.size()); ++i)
            {
                const std::string projectName = m_RecentProjects[static_cast<std::size_t>(i)].filename().string();
                if (containsInsensitive(projectName, m_SearchQuery))
                {
                    visibleIndices.push_back(i);
                }
            }
        }
        else
        {
            visibleIndices.reserve(m_Templates.size());
            for (int i = 0; i < static_cast<int>(m_Templates.size()); ++i)
            {
                if (containsInsensitive(m_Templates[static_cast<std::size_t>(i)], m_SearchQuery))
                {
                    visibleIndices.push_back(i);
                }
            }
        }

        ImGui::Spacing();
        const float leftPaneWidth = 430.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::BeginChild("LeftPane", ImVec2(leftPaneWidth, -72.0f), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::TextUnformatted(m_ActiveTab == ProjectBrowserTab::Recent ? "Recent Projects" : "Templates");
        ImGui::Separator();

        ImGui::BeginChild("ProjectList", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
        const float cardSpacing = 12.0f;
        const float cardWidth = 192.0f;
        const float cardHeight = 152.0f;
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        int columns = static_cast<int>((availableWidth + cardSpacing) / (cardWidth + cardSpacing));
        if (columns < 1)
        {
            columns = 1;
        }

        int visibleCardCounter = 0;
        for (const int index : visibleIndices)
        {
            const std::string itemName =
                m_ActiveTab == ProjectBrowserTab::Recent
                    ? m_RecentProjects[static_cast<std::size_t>(index)].filename().string()
                    : m_Templates[static_cast<std::size_t>(index)];

            if (visibleCardCounter > 0 && (visibleCardCounter % columns) != 0)
            {
                ImGui::SameLine(0.0f, cardSpacing);
            }

            ImGui::PushID(index);
            const ImVec2 cardMin = ImGui::GetCursorScreenPos();
            const ImVec2 cardSize(cardWidth, cardHeight);
            const ImVec2 cardMax(cardMin.x + cardSize.x, cardMin.y + cardSize.y);
            ImGui::InvisibleButton("##ProjectCard", cardSize);

            const bool hovered = ImGui::IsItemHovered();
            const bool clicked = ImGui::IsItemClicked();
            if (clicked)
            {
                if (m_ActiveTab == ProjectBrowserTab::Recent)
                {
                    m_SelectedRecentIndex = index;
                }
                else
                {
                    m_SelectedTemplateIndex = index;
                    m_SelectedTemplate = itemName;
                }
            }

            const bool selected =
                (m_ActiveTab == ProjectBrowserTab::Recent && m_SelectedRecentIndex == index) ||
                (m_ActiveTab == ProjectBrowserTab::New && m_SelectedTemplateIndex == index);

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(
                cardMin,
                cardMax,
                selected ? cardBgSelected : (hovered ? cardBgHovered : cardBg),
                8.0f);
            drawList->AddRect(
                cardMin,
                cardMax,
                selected ? accentBorder : cardBorder,
                8.0f,
                0,
                selected ? 2.0f : 1.0f);

            const ImVec2 thumbMin(cardMin.x + 8.0f, cardMin.y + 8.0f);
            const ImVec2 thumbMax(cardMax.x - 8.0f, cardMin.y + 96.0f);
            void* const templateTexture =
                m_ActiveTab == ProjectBrowserTab::New ? GetTemplateTexture(itemName) : nullptr;
            if (templateTexture != nullptr)
            {
                drawList->AddImageRounded(
                    reinterpret_cast<ImTextureID>(templateTexture),
                    thumbMin,
                    thumbMax,
                    ImVec2(0.0f, 0.0f),
                    ImVec2(1.0f, 1.0f),
                    IM_COL32(255, 255, 255, 255),
                    5.0f);
                drawList->AddRect(thumbMin, thumbMax, IM_COL32(96, 101, 114, 255), 5.0f);
            }
            else
            {
                drawList->AddRectFilledMultiColor(
                    thumbMin,
                    thumbMax,
                    IM_COL32(53, 58, 70, 255),
                    IM_COL32(42, 46, 57, 255),
                    IM_COL32(35, 38, 48, 255),
                    IM_COL32(42, 46, 57, 255));
                drawList->AddRect(thumbMin, thumbMax, IM_COL32(96, 101, 114, 255), 5.0f);
                drawList->AddText(
                    ImVec2(thumbMin.x + 10.0f, thumbMin.y + 10.0f),
                    IM_COL32(230, 233, 238, 255),
                    m_ActiveTab == ProjectBrowserTab::Recent ? "PROJECT" : "TEMPLATE");
            }

            drawList->AddText(
                ImVec2(cardMin.x + 10.0f, cardMin.y + 108.0f),
                IM_COL32(227, 230, 234, 255),
                itemName.c_str());
            drawList->AddText(
                ImVec2(cardMin.x + 10.0f, cardMin.y + 129.0f),
                IM_COL32(146, 153, 166, 255),
                m_ActiveTab == ProjectBrowserTab::Recent ? "Open existing project" : "Create from template");
            ImGui::PopID();
            ++visibleCardCounter;
        }

        if (visibleCardCounter == 0)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("No items match your search.");
        }

        ImGui::EndChild();
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("RightPane", ImVec2(0.0f, -72.0f), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::TextUnformatted("Preview");
        ImGui::Separator();

        const float previewHeight = 220.0f;
        ImGui::BeginChild("PreviewArea", ImVec2(0.0f, previewHeight), true, ImGuiWindowFlags_NoScrollbar);
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const ImVec2 size = ImGui::GetContentRegionAvail();
            const ImVec2 p1(p0.x + size.x, p0.y + size.y);

            std::string previewText = "Select a project or template";
            void* previewTexture = nullptr;

            if (m_ActiveTab == ProjectBrowserTab::Recent &&
                m_SelectedRecentIndex >= 0 &&
                m_SelectedRecentIndex < static_cast<int>(m_RecentProjects.size()))
            {
                previewText = m_RecentProjects[static_cast<std::size_t>(m_SelectedRecentIndex)].filename().string();
            }
            else if (
                m_ActiveTab == ProjectBrowserTab::New &&
                m_SelectedTemplateIndex >= 0 &&
                m_SelectedTemplateIndex < static_cast<int>(m_Templates.size()))
            {
                previewText = m_Templates[static_cast<std::size_t>(m_SelectedTemplateIndex)] + " Template";
                previewTexture = GetTemplateTexture(m_Templates[static_cast<std::size_t>(m_SelectedTemplateIndex)]);
            }

            if (previewTexture != nullptr)
            {
                drawList->AddImageRounded(
                    reinterpret_cast<ImTextureID>(previewTexture),
                    p0,
                    p1,
                    ImVec2(0.0f, 0.0f),
                    ImVec2(1.0f, 1.0f),
                    IM_COL32(255, 255, 255, 255),
                    6.0f);
                drawList->AddRect(p0, p1, IM_COL32(96, 101, 114, 255), 6.0f);
            }
            else
            {
                drawList->AddRectFilledMultiColor(
                    p0,
                    p1,
                    IM_COL32(76, 85, 109, 255),
                    IM_COL32(57, 65, 90, 255),
                    IM_COL32(33, 39, 57, 255),
                    IM_COL32(47, 54, 78, 255));
                drawList->AddRect(p0, p1, IM_COL32(86, 96, 122, 255), 6.0f);
            }

            const ImVec2 textSize = ImGui::CalcTextSize(previewText.c_str());
            const ImVec2 textPos(
                p0.x + (size.x - textSize.x) * 0.5f,
                p0.y + (size.y - textSize.y) * 0.5f);
            drawList->AddText(textPos, IM_COL32(245, 247, 250, 255), previewText.c_str());
            ImGui::Dummy(size);
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::TextUnformatted("Details");
        ImGui::Separator();

        if (m_ActiveTab == ProjectBrowserTab::Recent)
        {
            if (m_SelectedRecentIndex < 0 || m_SelectedRecentIndex >= static_cast<int>(m_RecentProjects.size()))
            {
                m_LastConfiguredRecentIndex = -1;
                ImGui::TextDisabled("No project selected.");
            }
            else
            {
                const auto& selectedRoot = m_RecentProjects[static_cast<std::size_t>(m_SelectedRecentIndex)];
                const auto projectFile = ProjectFileFromRoot(selectedRoot);
                const bool exists = std::filesystem::exists(projectFile);
                Project::ProjectConfig selectedConfig {};
                const bool parsedConfig = exists && Project::PeekConfig(projectFile, selectedConfig);
                if (parsedConfig && m_LastConfiguredRecentIndex != m_SelectedRecentIndex)
                {
                    m_SelectedPipelineProfile = selectedConfig.pipeline;
                    m_SelectedBackendPreference = BackendPreference::OpenGL;
                    m_VSyncEnabled = selectedConfig.vsync;
                    m_LastConfiguredRecentIndex = m_SelectedRecentIndex;
                }

                ImGui::Text("Name: %s", selectedRoot.filename().string().c_str());
                ImGui::TextUnformatted("Path:");
                ImGui::TextWrapped("%s", selectedRoot.string().c_str());
                ImGui::Text("Project File: %s", projectFile.filename().string().c_str());
                ImGui::TextColored(
                    exists ? ImVec4(0.58f, 0.82f, 0.67f, 1.0f) : ImVec4(0.90f, 0.44f, 0.44f, 1.0f),
                    "Status: %s",
                    exists ? "Available" : "Missing");

                if (exists)
                {
                    constexpr const char* pipelineOptions[] = { "CoreLite", "CoreX" };
                    int pipelineIndex = m_SelectedPipelineProfile == RenderPipelineProfile::CoreX ? 1 : 0;
                    if (ImGui::Combo("Render Pipeline", &pipelineIndex, pipelineOptions, IM_ARRAYSIZE(pipelineOptions)))
                    {
                        m_SelectedPipelineProfile =
                            pipelineIndex == 1 ? RenderPipelineProfile::CoreX : RenderPipelineProfile::CoreLite;
                    }

                    m_SelectedBackendPreference = BackendPreference::OpenGL;
                    ImGui::Text("Graphics Backend: %s", ToString(m_SelectedBackendPreference).data());
                    ImGui::TextDisabled("Alpha builds ship OpenGL only. Renderer abstractions stay in place for future backends.");

                    ImGui::Checkbox("VSync", &m_VSyncEnabled);
                    ImGui::Checkbox("Save as project default", &m_SaveAsProjectDefault);

                    const RenderSelectionResult resolvedSelection =
                        ResolveRenderSelection(m_SelectedPipelineProfile, m_SelectedBackendPreference);
                    ImGui::Text("Resolved Backend: %s", ToString(resolvedSelection.rendererAPI).data());
                    if (!resolvedSelection.message.empty())
                    {
                        ImGui::TextDisabled("%s", resolvedSelection.message.c_str());
                    }
                    if (resolvedSelection.hardFailure)
                    {
                        ImGui::TextColored(ImVec4(0.90f, 0.44f, 0.44f, 1.0f), "OpenGL is unavailable on this system.");
                    }
                }
            }
        }
        else
        {
            if (m_SelectedTemplateIndex >= 0 && m_SelectedTemplateIndex < static_cast<int>(m_Templates.size()))
            {
                m_SelectedTemplate = m_Templates[static_cast<std::size_t>(m_SelectedTemplateIndex)];
            }

            ImGui::Text("Template: %s", m_SelectedTemplate.c_str());

            std::array<char, 128> nameBuffer {};
            std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", m_NewProjectName.c_str());
            ImGui::TextUnformatted("Project Name");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##ProjectName", nameBuffer.data(), nameBuffer.size()))
            {
                m_NewProjectName = nameBuffer.data();
            }

            std::array<char, 512> pathBuffer {};
            std::snprintf(pathBuffer.data(), pathBuffer.size(), "%s", m_NewProjectPath.c_str());
            ImGui::TextUnformatted("Project Path");
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputText("##ProjectPath", pathBuffer.data(), pathBuffer.size()))
            {
                m_NewProjectPath = pathBuffer.data();
            }

            std::filesystem::path resolvedBase =
                std::filesystem::path(m_NewProjectPath.empty() ? m_ProjectsRoot.string() : m_NewProjectPath);
            std::filesystem::path fullPath = resolvedBase;
            if (fullPath.filename().string() != m_NewProjectName)
            {
                fullPath /= m_NewProjectName;
            }

            ImGui::TextUnformatted("Resolved Project Folder");
            ImGui::TextWrapped("%s", fullPath.lexically_normal().string().c_str());

            constexpr const char* pipelineOptions[] = { "CoreLite", "CoreX" };
            int pipelineIndex = m_SelectedPipelineProfile == RenderPipelineProfile::CoreX ? 1 : 0;
            if (ImGui::Combo("Render Pipeline", &pipelineIndex, pipelineOptions, IM_ARRAYSIZE(pipelineOptions)))
            {
                m_SelectedPipelineProfile =
                    pipelineIndex == 1 ? RenderPipelineProfile::CoreX : RenderPipelineProfile::CoreLite;
            }

            m_SelectedBackendPreference = BackendPreference::OpenGL;
            ImGui::Text("Graphics Backend: %s", ToString(m_SelectedBackendPreference).data());
            ImGui::TextDisabled("Alpha builds ship OpenGL only. Renderer abstractions stay in place for future backends.");

            ImGui::Checkbox("VSync", &m_VSyncEnabled);

            const RenderSelectionResult resolvedSelection =
                ResolveRenderSelection(m_SelectedPipelineProfile, m_SelectedBackendPreference);
            ImGui::Text("Resolved Backend: %s", ToString(resolvedSelection.rendererAPI).data());
            if (!resolvedSelection.message.empty())
            {
                ImGui::TextDisabled("%s", resolvedSelection.message.c_str());
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();

        ImGui::Separator();

        const float buttonWidth = 148.0f;
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float rightStart = ImGui::GetWindowContentRegionMax().x - (buttonWidth * 2.0f + spacing);
        if (rightStart > ImGui::GetCursorPosX())
        {
            ImGui::SetCursorPosX(rightStart);
        }

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.17f, 0.18f, 0.20f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.23f, 0.24f, 0.27f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.27f, 0.28f, 0.32f, 1.0f));

        if (m_ActiveTab == ProjectBrowserTab::Recent)
        {
            if (ImGui::Button("Refresh", ImVec2(buttonWidth, 0.0f)))
            {
                LoadRecentProjects();
                m_StatusMessage = "Refreshed project list.";
            }

            ImGui::SameLine();
            ImGui::PopStyleColor(3);
            ImGui::PushStyleColor(ImGuiCol_Button, accentColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.64f, 1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.21f, 0.49f, 0.88f, 1.0f));
            if (ImGui::Button("Open Project", ImVec2(buttonWidth, 0.0f)))
            {
                if (m_SelectedRecentIndex >= 0)
                {
                    if (!OpenProjectByIndex(static_cast<std::size_t>(m_SelectedRecentIndex)) && m_StatusMessage.empty())
                    {
                        m_StatusMessage = "Failed to open selected project.";
                    }
                }
                else
                {
                    m_StatusMessage = "Select a project first.";
                }
            }
            ImGui::PopStyleColor(3);
        }
        else
        {
            ImGui::Dummy(ImVec2(buttonWidth, 0.0f));
            ImGui::SameLine();
            ImGui::PopStyleColor(3);
            ImGui::PushStyleColor(ImGuiCol_Button, accentColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.32f, 0.64f, 1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.21f, 0.49f, 0.88f, 1.0f));
            if (ImGui::Button("Create Project", ImVec2(buttonWidth, 0.0f)))
            {
                if (!CreateProjectFromInput() && m_StatusMessage.empty())
                {
                    m_StatusMessage = "Failed to create project.";
                }
            }
            ImGui::PopStyleColor(3);
        }

        if (!m_StatusMessage.empty())
        {
            const bool isError =
                m_StatusMessage.find("Failed") != std::string::npos ||
                m_StatusMessage.find("Cannot") != std::string::npos ||
                m_StatusMessage.find("missing") != std::string::npos ||
                m_StatusMessage.find("required") != std::string::npos;
            ImGui::TextColored(
                isError ? ImVec4(0.92f, 0.46f, 0.46f, 1.0f) : ImVec4(0.63f, 0.82f, 0.71f, 1.0f),
                "%s",
                m_StatusMessage.c_str());
        }

        ImGui::End();
    }

    void ProjectManagerLayer::LoadRecentProjects()
    {
        m_RecentProjects.clear();

        std::ifstream input(m_RecentProjectsFile);
        if (input.is_open())
        {
            std::string line;
            while (std::getline(input, line))
            {
                if (!line.empty())
                {
                    m_RecentProjects.emplace_back(line);
                }
            }
        }

        if (m_RecentProjects.empty())
        {
            for (const auto& entry : std::filesystem::directory_iterator(m_ProjectsRoot))
            {
                if (!entry.is_directory())
                {
                    continue;
                }

                const auto root = entry.path();
                if (std::filesystem::exists(ProjectFileFromRoot(root)))
                {
                    m_RecentProjects.push_back(root);
                }
            }

            std::sort(m_RecentProjects.begin(), m_RecentProjects.end());
        }

        if (!m_RecentProjects.empty() && (m_SelectedRecentIndex < 0 || m_SelectedRecentIndex >= static_cast<int>(m_RecentProjects.size())))
        {
            m_SelectedRecentIndex = 0;
        }
    }

    void ProjectManagerLayer::SaveRecentProjects() const
    {
        std::ofstream output(m_RecentProjectsFile, std::ios::trunc);
        if (!output.is_open())
        {
            return;
        }

        for (const auto& projectRoot : m_RecentProjects)
        {
            output << projectRoot.string() << '\n';
        }
    }

    bool ProjectManagerLayer::OpenProjectByIndex(const std::size_t index)
    {
        if (index >= m_RecentProjects.size())
        {
            return false;
        }

        const auto projectRoot = m_RecentProjects[index];
        const auto projectFile = ProjectFileFromRoot(projectRoot);
        if (!std::filesystem::exists(projectFile))
        {
            m_StatusMessage = "Project file missing: " + projectFile.string();
            return false;
        }

        AddRecentProject(projectRoot);
        SaveRecentProjects();
        m_SelectedRecentIndex = 0;
        const bool opened = LaunchSelectedProject(projectFile);
        return opened;
    }

    bool ProjectManagerLayer::CreateProjectFromInput()
    {
        if (m_NewProjectName.empty())
        {
            m_StatusMessage = "Project name is required.";
            return false;
        }

        if (m_NewProjectPath.empty())
        {
            m_StatusMessage = "Project path is required.";
            return false;
        }

        std::filesystem::path projectRoot = std::filesystem::path(m_NewProjectPath);
        if (projectRoot.filename().string() != m_NewProjectName)
        {
            projectRoot /= m_NewProjectName;
        }

        projectRoot = projectRoot.lexically_normal();
        const std::filesystem::path projectFile = ProjectFileFromRoot(projectRoot);

        bool success = false;
        if (std::filesystem::exists(projectFile))
        {
            success = Project::Load(projectFile);
        }
        else
        {
            Project::CreateProjectDesc createDesc;
            createDesc.rootPath = projectRoot;
            createDesc.name = m_NewProjectName;
            createDesc.templateName = m_SelectedTemplate;
            createDesc.config = Project::DefaultConfig(m_NewProjectName, m_SelectedTemplate);
            createDesc.config.pipeline = m_SelectedPipelineProfile;
            createDesc.config.backend = BackendPreference::OpenGL;
            createDesc.config.vsync = m_VSyncEnabled;
            success = Project::Create(createDesc, nullptr);
        }

        if (!success)
        {
            return false;
        }

        Project::SetRenderingConfig(m_SelectedPipelineProfile, m_SelectedBackendPreference, m_VSyncEnabled);
        if (!Project::SaveLoadedConfig())
        {
            m_StatusMessage = "Project created but failed to save rendering defaults.";
            return false;
        }

        AddRecentProject(projectRoot);
        SaveRecentProjects();
        m_SelectedRecentIndex = 0;
        m_NewProjectPath = projectRoot.parent_path().string();
        const bool launched = LaunchSelectedProject(projectFile);
        return launched;
    }

    bool ProjectManagerLayer::LaunchSelectedProject(const std::filesystem::path& projectFile)
    {
        const RenderSelectionResult selection =
            ResolveRenderSelection(m_SelectedPipelineProfile, m_SelectedBackendPreference);

        if (selection.hardFailure || !selection.available)
        {
            m_StatusMessage = "Cannot launch project: " + selection.message;
            return false;
        }

        const bool backendSwitched = selection.rendererAPI != m_RuntimeAPI;
        const std::filesystem::path projectRoot = projectFile.parent_path().lexically_normal();
        const bool loadedProjectMatches =
            Project::IsLoaded() &&
            Project::GetProjectRoot().lexically_normal() == projectRoot;

        if (!backendSwitched)
        {
            if (!loadedProjectMatches && !Project::Load(projectFile))
            {
                m_StatusMessage = "Failed to load project: " + projectFile.string();
                return false;
            }

            Project::SetRenderingConfig(m_SelectedPipelineProfile, m_SelectedBackendPreference, m_VSyncEnabled);
            if (m_SaveAsProjectDefault && !Project::SaveLoadedConfig())
            {
                m_StatusMessage = "Failed to save rendering defaults for project.";
                return false;
            }

            m_ShouldClose = true;
            m_StatusMessage =
                "Loaded project with " +
                std::string(PipelineLabel(m_SelectedPipelineProfile)) + " / " +
                std::string(ToString(selection.rendererAPI)) + ".";
            return true;
        }

        bool loadedForDefaults = false;
        if (m_SaveAsProjectDefault)
        {
            if (!loadedProjectMatches && !Project::Load(projectFile))
            {
                m_StatusMessage = "Failed to load project: " + projectFile.string();
                return false;
            }
            loadedForDefaults = !loadedProjectMatches;

            if (!SaveLoadedProjectRenderingDefaults())
            {
                m_StatusMessage = "Failed to save rendering defaults for project.";
                if (loadedForDefaults)
                {
                    Project::Unload();
                }
                return false;
            }
        }

        const std::filesystem::path executablePath = GetExecutablePath();
        if (executablePath.empty())
        {
            if (loadedForDefaults)
            {
                Project::Unload();
            }
            m_StatusMessage = "Unable to determine executable path for relaunch.";
            return false;
        }

        std::ostringstream command;
#ifdef _WIN32
        command << "start \"\" " << QuoteArgument(executablePath.string());
#else
        command << QuoteArgument(executablePath.string());
#endif
        command << " --project=" << QuoteArgument(projectFile.string());
        command << " --pipeline=" << PipelineCliArgument(m_SelectedPipelineProfile);
        command << " --backend=" << BackendCliArgument(m_SelectedBackendPreference);
        command << " --api=" << APICliArgument(selection.rendererAPI);

        if (std::system(command.str().c_str()) != 0)
        {
            if (loadedForDefaults)
            {
                Project::Unload();
            }
            m_StatusMessage = "Failed to launch editor process.";
            return false;
        }

        if (loadedForDefaults)
        {
            Project::Unload();
        }

        GLFWwindow* window = nullptr;
        if (const ImGuiViewport* viewport = ImGui::GetMainViewport();
            viewport != nullptr && viewport->PlatformHandle != nullptr)
        {
            window = static_cast<GLFWwindow*>(viewport->PlatformHandle);
        }
        if (window == nullptr)
        {
            window = glfwGetCurrentContext();
        }

        if (window != nullptr)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }

        m_StatusMessage =
            std::string(backendSwitched ? "Restarting renderer. " : "Launching project with ") +
            std::string(PipelineLabel(m_SelectedPipelineProfile)) + " / " +
            std::string(ToString(selection.rendererAPI)) + ".";
        return true;
    }

    bool ProjectManagerLayer::SaveLoadedProjectRenderingDefaults()
    {
        if (!Project::IsLoaded())
        {
            return false;
        }

        Project::SetRenderingConfig(m_SelectedPipelineProfile, m_SelectedBackendPreference, m_VSyncEnabled);
        return Project::SaveLoadedConfig();
    }

    bool ProjectManagerLayer::LoadTemplateTextures()
    {
        ReleaseTemplateTextures();

        if (m_RenderBackend == nullptr)
        {
            return false;
        }

        const std::vector<std::filesystem::path> firstPersonCandidates = {
            "C:/Luma/assets/Images/First_Person_Thumbnail.png",
            std::filesystem::current_path() / "assets" / "Images" / "First_Person_Thumbnail.png",
            std::filesystem::current_path().parent_path() / "assets" / "Images" / "First_Person_Thumbnail.png",
            std::filesystem::current_path().parent_path().parent_path() / "assets" / "Images" / "First_Person_Thumbnail.png"
        };

        const std::vector<std::filesystem::path> thirdPersonCandidates = {
            "C:/Luma/assets/Images/Third_Person_Thumbnail.png",
            std::filesystem::current_path() / "assets" / "Images" / "Third_Person_Thumbnail.png",
            std::filesystem::current_path().parent_path() / "assets" / "Images" / "Third_Person_Thumbnail.png",
            std::filesystem::current_path().parent_path().parent_path() / "assets" / "Images" / "Third_Person_Thumbnail.png"
        };

        const std::vector<std::filesystem::path> blankCandidates = {
            "C:/Luma/thirdparty/editor-icons/imgs/GameProjectDialog/blank_project_preview.png",
            std::filesystem::current_path() / "thirdparty" / "editor-icons" / "imgs" / "GameProjectDialog" / "blank_project_preview.png",
            std::filesystem::current_path().parent_path() / "thirdparty" / "editor-icons" / "imgs" / "GameProjectDialog" / "blank_project_preview.png",
            std::filesystem::current_path().parent_path().parent_path() / "thirdparty" / "editor-icons" / "imgs" / "GameProjectDialog" / "blank_project_preview.png",
            std::filesystem::current_path() / "LumaEngine" / "thirdparty" / "editor-icons" / "imgs" / "GameProjectDialog" / "blank_project_preview.png",
            std::filesystem::current_path().parent_path() / "LumaEngine" / "thirdparty" / "editor-icons" / "imgs" / "GameProjectDialog" / "blank_project_preview.png"
        };

        const bool loadedFirst =
            LoadTextureFromCandidates(
                firstPersonCandidates,
                m_FirstPersonTexture,
                m_FirstPersonTextureWidth,
                m_FirstPersonTextureHeight,
                m_FirstPersonTexturePath);

        const bool loadedThird =
            LoadTextureFromCandidates(
                thirdPersonCandidates,
                m_ThirdPersonTexture,
                m_ThirdPersonTextureWidth,
                m_ThirdPersonTextureHeight,
                m_ThirdPersonTexturePath);

        const bool loadedBlank =
            LoadTextureFromCandidates(
                blankCandidates,
                m_BlankProjectTexture,
                m_BlankProjectTextureWidth,
                m_BlankProjectTextureHeight,
                m_BlankProjectTexturePath);

        if (!loadedFirst || !loadedThird || !loadedBlank)
        {
            m_StatusMessage = "Some template images could not be loaded.";
        }

        m_TemplateTexturesLoaded = loadedFirst || loadedThird || loadedBlank;
        return m_TemplateTexturesLoaded;
    }

    void ProjectManagerLayer::ReleaseTemplateTextures()
    {
        if (m_FirstPersonTexture != nullptr)
        {
            if (m_RenderBackend != nullptr)
            {
                m_RenderBackend->DestroyImGuiTexture(m_FirstPersonTexture);
            }
            m_FirstPersonTexture = nullptr;
            m_FirstPersonTextureWidth = 0;
            m_FirstPersonTextureHeight = 0;
            m_FirstPersonTexturePath.clear();
        }

        if (m_ThirdPersonTexture != nullptr)
        {
            if (m_RenderBackend != nullptr)
            {
                m_RenderBackend->DestroyImGuiTexture(m_ThirdPersonTexture);
            }
            m_ThirdPersonTexture = nullptr;
            m_ThirdPersonTextureWidth = 0;
            m_ThirdPersonTextureHeight = 0;
            m_ThirdPersonTexturePath.clear();
        }

        if (m_BlankProjectTexture != nullptr)
        {
            if (m_RenderBackend != nullptr)
            {
                m_RenderBackend->DestroyImGuiTexture(m_BlankProjectTexture);
            }
            m_BlankProjectTexture = nullptr;
            m_BlankProjectTextureWidth = 0;
            m_BlankProjectTextureHeight = 0;
            m_BlankProjectTexturePath.clear();
        }

        m_TemplateTexturesLoaded = false;
    }

    bool ProjectManagerLayer::LoadTextureFromCandidates(
        const std::vector<std::filesystem::path>& candidates,
        void*& outTexture,
        int& outWidth,
        int& outHeight,
        std::filesystem::path& outPath)
    {
        std::filesystem::path imagePath;
        for (const auto& candidate : candidates)
        {
            if (std::filesystem::exists(candidate))
            {
                imagePath = candidate;
                break;
            }
        }

        if (imagePath.empty())
        {
            return false;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* pixels = stbi_load(imagePath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr)
        {
            return false;
        }

        if (m_RenderBackend == nullptr)
        {
            stbi_image_free(pixels);
            return false;
        }

        void* texture =
            m_RenderBackend->CreateImGuiTextureRGBA8(
                static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height),
                pixels);

        stbi_image_free(pixels);

        if (texture == nullptr)
        {
            return false;
        }

        outTexture = texture;
        outWidth = width;
        outHeight = height;
        outPath = imagePath;
        return true;
    }

    void* ProjectManagerLayer::GetTemplateTexture(const std::string& templateName) const
    {
        if (templateName == "First Person")
        {
            return m_FirstPersonTexture;
        }

        if (templateName == "Third Person")
        {
            return m_ThirdPersonTexture;
        }

        if (IsBlankProjectTemplate(templateName))
        {
            return m_BlankProjectTexture;
        }

        return nullptr;
    }

    bool ProjectManagerLayer::IsBlankProjectTemplate(const std::string& templateName) const
    {
        return templateName == "Empty Project" || templateName == "Blank Project";
    }

    void ProjectManagerLayer::AddRecentProject(const std::filesystem::path& projectRoot)
    {
        std::error_code ec;
        auto normalized = std::filesystem::weakly_canonical(projectRoot, ec);
        if (ec)
        {
            normalized = projectRoot.lexically_normal();
        }

        m_RecentProjects.erase(
            std::remove_if(
                m_RecentProjects.begin(),
                m_RecentProjects.end(),
                [&normalized](const std::filesystem::path& candidatePath)
                {
                    std::error_code candidateEc;
                    auto candidate = std::filesystem::weakly_canonical(candidatePath, candidateEc);
                    if (candidateEc)
                    {
                        candidate = candidatePath.lexically_normal();
                    }
                    return candidate == normalized;
                }),
            m_RecentProjects.end());

        m_RecentProjects.insert(m_RecentProjects.begin(), normalized);
        if (m_RecentProjects.size() > 16)
        {
            m_RecentProjects.resize(16);
        }
    }

    std::filesystem::path ProjectManagerLayer::ProjectFileFromRoot(const std::filesystem::path& projectRoot) const
    {
        const std::string projectName = projectRoot.filename().string();
        return projectRoot / (projectName + ".ep");
    }
}

