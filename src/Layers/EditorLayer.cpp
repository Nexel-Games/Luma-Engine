#include "Luma/Layers/EditorLayer.h"

#include <algorithm>
#include <filesystem>
#include <vector>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <stb_image.h>

#include "Luma/Core/App/Application.h"
#include "Luma/Core/App/Project.h"
#include "Luma/Core/App/RenderSelection.h"
#include "Luma/Editor/Core/EditorTaskManager.h"
#include "Luma/Layers/ProjectManagerLayer.h"
#include "Luma/Layers/TriangleLayer.h"

namespace Luma
{
    EditorLayer::EditorLayer()
        : Layer("EditorLayer")
    {
    }

    EditorLayer::~EditorLayer() = default;

    void EditorLayer::OnAttach()
    {
        if (Project::IsLoaded())
        {
            m_State = EditorState::Startup;
            m_MainWindowFullscreen = false;
            BeginMainEditorTransition("Opening Project...");
        }
        else
        {
            m_State = EditorState::ProjectBrowser;
            if (!m_ProjectBrowserLayer)
            {
                m_ProjectBrowserLayer = std::make_unique<ProjectManagerLayer>();
            }
            if (!m_ProjectBrowserAttached)
            {
                m_ProjectBrowserLayer->OnAttach();
                m_ProjectBrowserAttached = true;
            }
            SetMainWindowFullscreen(false);
            SetMainWindowTitle("Project Browser");
        }
    }

    void EditorLayer::OnDetach()
    {
        if (m_MainEditorAttached && m_MainEditorLayer)
        {
            m_MainEditorLayer->OnDetach();
            m_MainEditorAttached = false;
        }

        if (m_ProjectBrowserAttached && m_ProjectBrowserLayer)
        {
            m_ProjectBrowserLayer->OnDetach();
            m_ProjectBrowserAttached = false;
        }

        ReleaseStartupSplashTexture();
        EditorTaskManager::Reset();
    }

    void EditorLayer::OnUpdate(const float deltaTimeSeconds)
    {
        TickTransition(deltaTimeSeconds);

        if (m_State == EditorState::Startup)
        {
            if (m_MainEditorLayer && m_MainEditorAttached)
            {
                m_MainEditorLayer->OnUpdate(deltaTimeSeconds);
                return;
            }

            if (m_ProjectBrowserLayer && m_ProjectBrowserAttached)
            {
                m_ProjectBrowserLayer->OnUpdate(deltaTimeSeconds);
            }
            return;
        }

        if (m_State == EditorState::MainEditor && m_MainEditorLayer)
        {
            m_MainEditorLayer->OnUpdate(deltaTimeSeconds);
            return;
        }

        if (m_State == EditorState::ProjectBrowser && m_ProjectBrowserLayer)
        {
            m_ProjectBrowserLayer->OnUpdate(deltaTimeSeconds);
        }
    }

    void EditorLayer::OnRender(IRenderBackend& renderer)
    {
        EditorTaskManager::SetRenderBackend(&renderer);
        if (m_StartupRenderBackend != &renderer)
        {
            m_StartupRenderBackend = &renderer;
            if (m_StartupSplashTexture != nullptr && m_StartupSplashTextureOwner != &renderer)
            {
                ReleaseStartupSplashTexture();
            }
        }

        if (m_State == EditorState::Startup)
        {
            if (m_MainEditorLayer && m_MainEditorAttached)
            {
                m_MainEditorLayer->OnRender(renderer);
            }

            if (m_ProjectBrowserLayer && m_ProjectBrowserAttached)
            {
                m_ProjectBrowserLayer->OnRender(renderer);
            }
            return;
        }

        if (m_State == EditorState::MainEditor)
        {
            LoadMainEditor();
            if (m_MainEditorLayer)
            {
                m_MainEditorLayer->OnRender(renderer);
            }
            return;
        }

        if (m_State == EditorState::ProjectBrowser && m_ProjectBrowserLayer)
        {
            m_ProjectBrowserLayer->OnRender(renderer);
        }
    }

    void EditorLayer::OnImGuiRender()
    {
        if (m_State == EditorState::Startup && m_Transition == EditorTransition::None)
        {
            m_State = Project::IsLoaded() ? EditorState::MainEditor : EditorState::ProjectBrowser;
        }

        switch (m_State)
        {
        case EditorState::Startup:
            if (m_ProjectBrowserLayer && m_ProjectBrowserAttached)
            {
                m_ProjectBrowserLayer->OnImGuiRender();
            }
            DrawStartupOverlay();
            break;
        case EditorState::ProjectBrowser:
            ShowProjectBrowser();
            break;
        case EditorState::MainEditor:
            LoadMainEditor();
            if (m_MainEditorLayer)
            {
                m_MainEditorLayer->OnImGuiRender();
            }
            break;
        default:
            break;
        }

        EditorTaskManager::DrawOverlay();
    }

    void EditorLayer::EnterMainEditorImmediate()
    {
        m_State = EditorState::MainEditor;
        ReleaseStartupSplashTexture();
        SetMainWindowFullscreen(true);
        if (Project::IsLoaded())
        {
            SetMainWindowTitle("Luma - " + Project::GetConfig().name);
        }
        LoadMainEditor();
    }

    void EditorLayer::BeginMainEditorTransition(const std::string& taskTitle)
    {
        m_State = EditorState::Startup;
        m_Transition = EditorTransition::EnterMainEditor;
        m_BootState = EngineBootState::Bootstrap;
        m_BootStateElapsedSeconds = 0.0f;
        m_StartupReadyFrameCount = 0;
        SetMainWindowFullscreen(true);
        UpdateStartupStatus(
            taskTitle,
            "Bootstrapping process, config, and crash handling...",
            0.02f,
            Project::IsLoaded() ? ("Project: " + Project::GetConfig().name) : std::string {});

        if (Project::IsLoaded())
        {
            SetMainWindowTitle("Loading - " + Project::GetConfig().name);
        }
        else
        {
            SetMainWindowTitle("Loading Project");
        }
    }

    void EditorLayer::TickTransition(const float deltaTimeSeconds)
    {
        if (m_Transition == EditorTransition::None)
        {
            return;
        }

        if (m_Transition == EditorTransition::EnterMainEditor)
        {
            AdvanceMainEditorBoot(deltaTimeSeconds);
        }
    }

    void EditorLayer::AdvanceMainEditorBoot(const float deltaTimeSeconds)
    {
        constexpr float kMinimumPhaseDurationSeconds = 0.05f;
        constexpr float kGuardPhaseTimeoutSeconds = 1.50f;
        m_BootStateElapsedSeconds += deltaTimeSeconds;

        auto AdvanceState = [this](const EngineBootState nextState)
        {
            m_BootState = nextState;
            m_BootStateElapsedSeconds = 0.0f;
        };

        const auto MinimumPhaseElapsed = [&]()
        {
            return m_BootStateElapsedSeconds >= kMinimumPhaseDurationSeconds;
        };

        const auto GuardTimedOut = [&]()
        {
            return m_BootStateElapsedSeconds >= kGuardPhaseTimeoutSeconds;
        };

        if (!Project::IsLoaded())
        {
            m_Transition = EditorTransition::None;
            m_State = EditorState::ProjectBrowser;
            m_BootState = EngineBootState::Bootstrap;
            m_BootStateElapsedSeconds = 0.0f;
            m_StartupReadyFrameCount = 0;
            ReleaseStartupSplashTexture();

            if (m_MainEditorAttached && m_MainEditorLayer)
            {
                m_MainEditorLayer->OnDetach();
                m_MainEditorAttached = false;
            }

            if (!m_ProjectBrowserLayer)
            {
                m_ProjectBrowserLayer = std::make_unique<ProjectManagerLayer>();
            }
            if (!m_ProjectBrowserAttached)
            {
                m_ProjectBrowserLayer->OnAttach();
                m_ProjectBrowserAttached = true;
            }
            SetMainWindowFullscreen(false);
            return;
        }

        switch (m_BootState)
        {
        case EngineBootState::Bootstrap:
            UpdateStartupStatus(
                "Opening Project...",
                "Bootstrapping process, config, and crash handling...",
                0.06f,
                Project::IsLoaded() ? ("Project: " + Project::GetConfig().name) : std::string {});
            if (MinimumPhaseElapsed())
            {
                AdvanceState(EngineBootState::CoreInit);
            }
            break;
        case EngineBootState::CoreInit:
            UpdateStartupStatus(
                "Opening Project...",
                "Initializing core systems: memory, jobs, events, and filesystem...",
                0.18f,
                "Preparing runtime services for the editor.");
            if (!m_MainEditorLayer)
            {
                m_MainEditorLayer = std::make_unique<TriangleLayer>();
            }
            if (m_MainEditorLayer && MinimumPhaseElapsed())
            {
                AdvanceState(EngineBootState::ModuleLoad);
            }
            break;
        case EngineBootState::ModuleLoad:
            UpdateStartupStatus(
                "Opening Project...",
                "Loading editor modules, systems, and plugin surfaces...",
                0.34f,
                m_MainEditorAttached ? "Main editor layer attached." : "Attaching main editor layer...");
            if (m_MainEditorLayer && !m_MainEditorAttached)
            {
                m_MainEditorLayer->OnAttach();
                m_MainEditorAttached = true;
            }
            if (m_MainEditorAttached && MinimumPhaseElapsed())
            {
                AdvanceState(EngineBootState::AssetScan);
            }
            break;
        case EngineBootState::AssetScan:
        {
            std::string detail = "Scanning content roots and package mounts...";
            if (m_MainEditorLayer)
            {
                detail =
                    std::to_string(m_MainEditorLayer->GetAssetRegistryCount()) + " indexed assets • " +
                    std::to_string(m_MainEditorLayer->GetContentRootCount()) + " roots • " +
                    std::to_string(m_MainEditorLayer->GetContentEntryCount()) + " visible entries";
                detail += m_MainEditorLayer->IsPackageRegistryLoaded() ? " • registry ready" : " • registry pending";
            }
            UpdateStartupStatus(
                "Opening Project...",
                "Scanning assets, content roots, and package registry...",
                0.52f,
                std::move(detail));
            if (m_MainEditorLayer &&
                m_MainEditorLayer->HasContentRoots() &&
                MinimumPhaseElapsed())
            {
                AdvanceState(EngineBootState::ScriptCompile);
            }
            else if (m_MainEditorLayer && GuardTimedOut())
            {
                AdvanceState(EngineBootState::ScriptCompile);
            }
            break;
        }
        case EngineBootState::ScriptCompile:
        {
            std::string scriptSubtask = "Compiling scripts and rebuilding reflection data...";
            std::string detail = "Script runtime bootstrap pending.";
            if (m_MainEditorLayer && !m_MainEditorLayer->IsAssetPipelineInitialized())
            {
                scriptSubtask = "Asset pipeline unavailable, continuing with editor runtime bootstrap...";
                detail = "Import pipeline initialization failed, continuing without script bootstrap.";
            }
            else if (m_MainEditorLayer)
            {
                detail =
                    std::to_string(m_MainEditorLayer->GetAssetRegistryCount()) +
                    " assets ready for runtime/editor script surfaces.";
            }
            UpdateStartupStatus("Opening Project...", std::move(scriptSubtask), 0.64f, std::move(detail));
            if (MinimumPhaseElapsed())
            {
                AdvanceState(EngineBootState::RendererInit);
            }
            break;
        }
        case EngineBootState::RendererInit:
        {
            const RenderSelectionResult selection =
                ResolveRenderSelection(Project::GetConfig().pipeline, Project::GetConfig().backend);
            std::string rendererSubtask = "Creating GPU device, swapchain, and renderer backend: ";
            if (selection.available && !selection.hardFailure)
            {
                rendererSubtask += std::string(ToString(selection.rendererAPI));
            }
            else
            {
                rendererSubtask += "Unavailable";
            }
            rendererSubtask += "...";
            std::string detail = selection.available && !selection.hardFailure
                ? "Resolved backend: " + std::string(ToString(selection.rendererAPI))
                : "Renderer selection fell back to an unavailable backend state.";
            if (m_MainEditorLayer && m_MainEditorLayer->HasRendererBinding())
            {
                detail += " Renderer binding is active.";
            }
            UpdateStartupStatus("Opening Project...", std::move(rendererSubtask), 0.76f, std::move(detail));
            if (m_MainEditorLayer && m_MainEditorLayer->HasRendererBinding() && MinimumPhaseElapsed())
            {
                AdvanceState(EngineBootState::ShaderCompile);
            }
            else if (GuardTimedOut())
            {
                AdvanceState(EngineBootState::ShaderCompile);
            }
            break;
        }
        case EngineBootState::ShaderCompile:
            UpdateStartupStatus(
                "Opening Project...",
                "Compiling shaders, materials, and pipeline variants...",
                0.86f,
                m_MainEditorLayer && m_MainEditorLayer->HasRenderPipeline()
                    ? "Render pipeline is live."
                    : "Waiting for the render pipeline to finish bootstrapping.");
            if (m_MainEditorLayer && m_MainEditorLayer->HasRenderPipeline() && MinimumPhaseElapsed())
            {
                AdvanceState(EngineBootState::ProjectLoad);
            }
            else if (GuardTimedOut())
            {
                AdvanceState(EngineBootState::ProjectLoad);
            }
            break;
        case EngineBootState::ProjectLoad:
            UpdateStartupStatus(
                "Opening Project...",
                "Loading project scene, render graph, and editor context...",
                0.92f,
                m_MainEditorLayer && m_MainEditorLayer->HasSceneEntities()
                    ? "Default scene entities are ready."
                    : "Seeding the initial scene and editor view state.");
            if (m_MainEditorLayer && m_MainEditorLayer->HasSceneEntities() && MinimumPhaseElapsed())
            {
                AdvanceState(EngineBootState::EditorInit);
            }
            else if (GuardTimedOut())
            {
                AdvanceState(EngineBootState::EditorInit);
            }
            break;
        case EngineBootState::EditorInit:
            UpdateStartupStatus(
                "Opening Project...",
                "Initializing editor UI, panels, and dock layout...",
                0.97f,
                "Preparing the first editor frame and workspace layout.");
            SetMainWindowTitle("Luma - " + Project::GetConfig().name);
            if (MinimumPhaseElapsed())
            {
                AdvanceState(EngineBootState::Ready);
            }
            else if (GuardTimedOut())
            {
                AdvanceState(EngineBootState::Ready);
            }
            break;
        case EngineBootState::Ready:
            UpdateStartupStatus("Opening Project...", "Editor ready.", 1.0f, "Startup completed.");
            if (m_StartupReadyFrameCount < 2)
            {
                ++m_StartupReadyFrameCount;
                break;
            }

            ReleaseStartupSplashTexture();
            m_Transition = EditorTransition::None;
            m_BootState = EngineBootState::Bootstrap;
            m_BootStateElapsedSeconds = 0.0f;
            m_StartupReadyFrameCount = 0;
            m_State = EditorState::MainEditor;
            break;
        default:
            AdvanceState(EngineBootState::Ready);
            break;
        }
    }

    void EditorLayer::DrawStartupOverlay()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        if (viewport == nullptr)
        {
            return;
        }

        const bool hasSplash = EnsureStartupSplashTextureLoaded();
        ImGui::SetNextWindowPos(viewport->Pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(viewport->Size, ImGuiCond_Always);
        constexpr ImGuiWindowFlags blockerFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoBackground;
        ImGui::Begin("##StartupTransitionBlocker", nullptr, blockerFlags);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImVec2 viewportMax(
            viewport->Pos.x + viewport->Size.x,
            viewport->Pos.y + viewport->Size.y);
        if (hasSplash && m_StartupSplashTexture != nullptr)
        {
            drawList->AddImage(
                reinterpret_cast<ImTextureID>(m_StartupSplashTexture),
                viewport->Pos,
                viewportMax,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                IM_COL32(255, 255, 255, 255));
            drawList->AddRectFilledMultiColor(
                viewport->Pos,
                viewportMax,
                IM_COL32(6, 8, 12, 175),
                IM_COL32(6, 8, 12, 155),
                IM_COL32(6, 8, 12, 230),
                IM_COL32(6, 8, 12, 245));
        }
        else
        {
            drawList->AddRectFilledMultiColor(
                viewport->Pos,
                viewportMax,
                IM_COL32(16, 22, 34, 255),
                IM_COL32(14, 20, 30, 255),
                IM_COL32(6, 9, 14, 255),
                IM_COL32(7, 10, 16, 255));
        }
        ImGui::End();

        const float maxCardWidth = std::clamp(viewport->Size.x * 0.72f, 640.0f, 1040.0f);
        const float maxCardHeight = std::clamp(viewport->Size.y * 0.78f, 360.0f, 760.0f);
        ImVec2 cardSize(maxCardWidth, hasSplash ? maxCardHeight : 260.0f);
        if (hasSplash && m_StartupSplashWidth > 0 && m_StartupSplashHeight > 0)
        {
            const float aspect = static_cast<float>(m_StartupSplashWidth) / static_cast<float>(m_StartupSplashHeight);
            float width = maxCardWidth;
            float height = width / aspect;
            if (height > maxCardHeight)
            {
                height = maxCardHeight;
                width = height * aspect;
            }
            cardSize = ImVec2(std::max(width, 640.0f), std::max(height, 360.0f));
        }

        const ImVec2 cardPos(
            viewport->Pos.x + (viewport->Size.x - cardSize.x) * 0.5f,
            viewport->Pos.y + (viewport->Size.y - cardSize.y) * 0.5f);

        ImGui::SetNextWindowPos(cardPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(cardSize, ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 18.0f));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.11f, 0.13f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.29f, 0.40f, 0.56f, 0.70f));
        constexpr ImGuiWindowFlags cardFlags =
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoDocking;
        ImGui::Begin("##StartupTransitionCard", nullptr, cardFlags);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        if (hasSplash && m_StartupSplashTexture != nullptr)
        {
            ImDrawList* cardDrawList = ImGui::GetWindowDrawList();
            const ImVec2 windowPos = ImGui::GetWindowPos();
            const ImVec2 windowSize = ImGui::GetWindowSize();
            const ImVec2 windowMax(windowPos.x + windowSize.x, windowPos.y + windowSize.y);
            cardDrawList->AddImageRounded(
                reinterpret_cast<ImTextureID>(m_StartupSplashTexture),
                windowPos,
                windowMax,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                IM_COL32(255, 255, 255, 255),
                12.0f);
            const float panelTop = windowPos.y + windowSize.y * 0.56f;
            cardDrawList->AddRectFilledMultiColor(
                ImVec2(windowPos.x, panelTop),
                windowMax,
                IM_COL32(10, 13, 18, 55),
                IM_COL32(10, 13, 18, 55),
                IM_COL32(10, 13, 18, 230),
                IM_COL32(10, 13, 18, 230));
            cardDrawList->AddRect(windowPos, windowMax, IM_COL32(74, 103, 150, 190), 12.0f, 0, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.93f, 0.95f, 0.98f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.82f, 0.85f, 0.90f, 1.0f));
            ImGui::SetCursorPos(ImVec2(24.0f, windowSize.y - 118.0f));
        }

        ImGui::TextUnformatted(m_StartupTitle.c_str());
        ImGui::Spacing();
        ImGui::TextDisabled("%s", m_StartupSubtask.empty() ? "Preparing editor context..." : m_StartupSubtask.c_str());
        if (!m_StartupDetail.empty())
        {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", m_StartupDetail.c_str());
        }

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.22f, 0.56f, 0.95f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.15f, 0.16f, 0.19f, 1.0f));
        ImGui::ProgressBar(m_StartupProgress, ImVec2(-1.0f, 11.0f));
        ImGui::PopStyleColor(2);
        ImGui::TextDisabled("%.0f%%", m_StartupProgress * 100.0f);

        if (hasSplash && m_StartupSplashTexture != nullptr)
        {
            ImGui::PopStyleColor(2);
        }

        ImGui::End();
    }

    void EditorLayer::UpdateStartupStatus(
        std::string title,
        std::string subtask,
        const float progress,
        std::string detail)
    {
        m_StartupTitle = std::move(title);
        m_StartupSubtask = std::move(subtask);
        m_StartupDetail = std::move(detail);
        m_StartupProgress = std::clamp(progress, 0.0f, 1.0f);
    }

    bool EditorLayer::EnsureStartupSplashTextureLoaded()
    {
        if (m_StartupSplashTexture != nullptr)
        {
            return true;
        }

        if (m_StartupRenderBackend == nullptr)
        {
            return false;
        }

        if (m_StartupSplashLoadAttempted)
        {
            return false;
        }

        const std::vector<std::filesystem::path> candidates = {
            "C:/Luma/assets/Images/lsplash_screen.png",
            std::filesystem::current_path() / "assets" / "Images" / "lsplash_screen.png",
            std::filesystem::current_path().parent_path() / "assets" / "Images" / "lsplash_screen.png",
            std::filesystem::current_path().parent_path().parent_path() / "assets" / "Images" / "lsplash_screen.png"
        };

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
            m_StartupSplashLoadAttempted = true;
            return false;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* pixels = stbi_load(imagePath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr)
        {
            m_StartupSplashLoadAttempted = true;
            return false;
        }

        void* texture = m_StartupRenderBackend->CreateImGuiTextureRGBA8(
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height),
            pixels);
        stbi_image_free(pixels);
        m_StartupSplashLoadAttempted = true;
        if (texture == nullptr)
        {
            return false;
        }

        m_StartupSplashTexture = texture;
        m_StartupSplashTextureOwner = m_StartupRenderBackend;
        m_StartupSplashWidth = static_cast<std::uint32_t>(width);
        m_StartupSplashHeight = static_cast<std::uint32_t>(height);
        return true;
    }

    void EditorLayer::ReleaseStartupSplashTexture()
    {
        if (m_StartupSplashTexture != nullptr && m_StartupSplashTextureOwner != nullptr)
        {
            m_StartupSplashTextureOwner->DestroyImGuiTexture(m_StartupSplashTexture);
        }

        m_StartupSplashTexture = nullptr;
        m_StartupSplashTextureOwner = nullptr;
        m_StartupSplashWidth = 0;
        m_StartupSplashHeight = 0;
        m_StartupSplashLoadAttempted = false;
    }

    void EditorLayer::ShowProjectBrowser()
    {
        if (!m_ProjectBrowserLayer)
        {
            return;
        }

        if (!m_ProjectBrowserAttached)
        {
            m_ProjectBrowserLayer->OnAttach();
            m_ProjectBrowserAttached = true;
        }

        m_ProjectBrowserLayer->OnImGuiRender();
        if (!m_ProjectBrowserLayer->ShouldClose() || !Project::IsLoaded())
        {
            return;
        }

        m_ProjectBrowserLayer->Reset();
        BeginMainEditorTransition("Opening Project...");
    }

    void EditorLayer::LoadMainEditor()
    {
        if (!m_MainEditorLayer)
        {
            m_MainEditorLayer = std::make_unique<TriangleLayer>();
        }

        if (!m_MainEditorAttached)
        {
            m_MainEditorLayer->OnAttach();
            m_MainEditorAttached = true;
        }

        if (m_ProjectBrowserAttached && m_ProjectBrowserLayer)
        {
            m_ProjectBrowserLayer->OnDetach();
            m_ProjectBrowserAttached = false;
        }
    }

    void EditorLayer::SetMainWindowTitle(const std::string& title) const
    {
        GLFWwindow* window = nullptr;
        if (Application* app = Application::Get(); app != nullptr)
        {
            window = app->GetWindowHandle();
        }

        if (window != nullptr)
        {
            glfwSetWindowTitle(window, title.c_str());
        }
    }

    void EditorLayer::SetMainWindowFullscreen(const bool fullscreen)
    {
        if (m_MainWindowFullscreen == fullscreen)
        {
            return;
        }

        GLFWwindow* window = nullptr;
        if (Application* app = Application::Get(); app != nullptr)
        {
            window = app->GetWindowHandle();
        }

        if (window == nullptr)
        {
            return;
        }

        m_MainWindowFullscreen = fullscreen;

        if (fullscreen)
        {
            glfwRestoreWindow(window);
            glfwMaximizeWindow(window);
        }
        else
        {
            glfwRestoreWindow(window);
            glfwSetWindowSize(window, 1000, 650);
        }

        int framebufferWidth = 0;
        int framebufferHeight = 0;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        if (m_StartupRenderBackend != nullptr && framebufferWidth > 0 && framebufferHeight > 0)
        {
            m_StartupRenderBackend->OnResize(
                static_cast<std::uint32_t>(framebufferWidth),
                static_cast<std::uint32_t>(framebufferHeight));
        }

        glfwPostEmptyEvent();
    }
}
