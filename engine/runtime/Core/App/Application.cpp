#include "Luma/Core/App/Application.h"

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "Luma/Core/Foundation/Assert.h"
#include "Luma/Core/Foundation/Jobs.h"
#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Core/Foundation/Memory.h"
#include "Luma/Core/Foundation/Platform.h"
#include "Luma/Core/Foundation/Time.h"
#include "Luma/Input/Input.h"
#include "Luma/RHI/RHIFactory.h"
#include "Luma/Scripting/ScriptEngine.h"

namespace Luma
{
    Application* Application::s_Instance = nullptr;

    namespace
    {
        constexpr std::string_view kApplicationInputContext = "Application";
        constexpr std::string_view kActionExit = "Application.Exit";

        void GlfwErrorCallback(int error, const char* description)
        {
            std::cerr << "[GLFW] (" << error << ") " << description << '\n';
        }

        void ApplyLumaStyle()
        {
            ImGuiStyle& style = ImGui::GetStyle();
            ImVec4* colors = style.Colors;

            colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.10f, 0.11f, 1.00f);
            colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
            colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.07f, 0.08f, 0.09f, 1.00f);
            colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.13f, 0.99f);
            colors[ImGuiCol_Border] = ImVec4(0.22f, 0.23f, 0.26f, 0.55f);
            colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
            colors[ImGuiCol_Text] = ImVec4(0.86f, 0.87f, 0.89f, 1.00f);
            colors[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.54f, 0.57f, 1.00f);

            colors[ImGuiCol_Header] = ImVec4(0.18f, 0.19f, 0.21f, 1.00f);
            colors[ImGuiCol_HeaderHovered] = ImVec4(0.23f, 0.24f, 0.27f, 1.00f);
            colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.28f, 0.31f, 1.00f);
            colors[ImGuiCol_Button] = ImVec4(0.17f, 0.18f, 0.20f, 1.00f);
            colors[ImGuiCol_ButtonHovered] = ImVec4(0.23f, 0.24f, 0.27f, 1.00f);
            colors[ImGuiCol_ButtonActive] = ImVec4(0.27f, 0.28f, 0.32f, 1.00f);
            colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.19f, 1.00f);
            colors[ImGuiCol_FrameBgHovered] = ImVec4(0.21f, 0.22f, 0.25f, 1.00f);
            colors[ImGuiCol_FrameBgActive] = ImVec4(0.24f, 0.25f, 0.28f, 1.00f);

            colors[ImGuiCol_Tab] = ImVec4(0.13f, 0.14f, 0.16f, 1.00f);
            colors[ImGuiCol_TabHovered] = ImVec4(0.20f, 0.21f, 0.24f, 1.00f);
            colors[ImGuiCol_TabActive] = ImVec4(0.17f, 0.18f, 0.20f, 1.00f);
            colors[ImGuiCol_TabUnfocused] = ImVec4(0.10f, 0.11f, 0.12f, 1.00f);
            colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.15f, 0.17f, 1.00f);

            colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.09f, 0.10f, 1.00f);
            colors[ImGuiCol_TitleBgActive] = ImVec4(0.11f, 0.12f, 0.14f, 1.00f);
            colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);
            colors[ImGuiCol_MenuBarBg] = ImVec4(0.08f, 0.09f, 0.10f, 1.00f);
            colors[ImGuiCol_ScrollbarBg] = ImVec4(0.09f, 0.10f, 0.11f, 1.00f);
            colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.22f, 0.23f, 0.26f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.28f, 0.29f, 0.32f, 1.00f);
            colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.34f, 0.35f, 0.39f, 1.00f);
            colors[ImGuiCol_CheckMark] = ImVec4(0.83f, 0.84f, 0.86f, 1.00f);
            colors[ImGuiCol_SliderGrab] = ImVec4(0.58f, 0.60f, 0.65f, 1.00f);
            colors[ImGuiCol_SliderGrabActive] = ImVec4(0.72f, 0.74f, 0.79f, 1.00f);
            colors[ImGuiCol_TextSelectedBg] = ImVec4(0.35f, 0.38f, 0.45f, 0.35f);

            style.WindowRounding = 2.0f;
            style.ChildRounding = 2.0f;
            style.FrameRounding = 3.0f;
            style.PopupRounding = 2.0f;
            style.ScrollbarRounding = 2.0f;
            style.GrabRounding = 2.0f;
            style.TabRounding = 3.0f;
            style.WindowBorderSize = 1.0f;
            style.ChildBorderSize = 1.0f;
            style.FrameBorderSize = 0.0f;
            style.PopupBorderSize = 1.0f;
            style.WindowPadding = ImVec2(8.0f, 8.0f);
            style.FramePadding = ImVec2(8.0f, 5.0f);
            style.ItemSpacing = ImVec2(6.0f, 4.0f);
            style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
            style.IndentSpacing = 16.0f;
            style.ScrollbarSize = 12.0f;
            style.GrabMinSize = 10.0f;
            style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
            style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
            style.SelectableTextAlign = ImVec2(0.0f, 0.5f);
        }

        void SetupEditorFonts()
        {
            ImGuiIO& io = ImGui::GetIO();
            static const ImWchar ranges[] = {
                0x0020, 0x00FF,
                0
            };

            const std::array<const char*, 8> fontCandidates = {
                "assets/Fonts/Inter/Inter-VariableFont_opsz,wght.ttf",
                "../assets/Fonts/Inter/Inter-VariableFont_opsz,wght.ttf",
                "../../assets/Fonts/Inter/Inter-VariableFont_opsz,wght.ttf",
                "../../../assets/Fonts/Inter/Inter-VariableFont_opsz,wght.ttf",
                "thirdparty/imgui/misc/fonts/Karla-Regular.ttf",
                "../thirdparty/imgui/misc/fonts/Karla-Regular.ttf",
                "C:/Windows/Fonts/segoeui.ttf",
                "C:/Windows/Fonts/arial.ttf"
            };

            for (const char* fontPath : fontCandidates)
            {
                if (!std::filesystem::exists(fontPath))
                {
                    continue;
                }

                ImFont* loadedFont = io.Fonts->AddFontFromFileTTF(fontPath, 15.0f, nullptr, ranges);
                if (loadedFont != nullptr)
                {
                    io.FontDefault = loadedFont;
                    return;
                }
            }
        }
    }

    Application::Application(ApplicationConfig config)
        : m_Config(std::move(config))
    {
        LUMA_CORE_ASSERT(s_Instance == nullptr, "Only one Application instance may exist at a time.");
        s_Instance = this;
        LUMA_CORE_ASSERT(m_Config.width > 0 && m_Config.height > 0, "Window dimensions must be greater than zero.");
        LUMA_LOG_INFO("Core", "Initializing application.");

        if (!ScriptEngine::Initialize())
        {
            throw std::runtime_error("Failed to initialize ScriptEngine.");
        }

        glfwSetErrorCallback(GlfwErrorCallback);

        if (!InitializeWindow())
        {
            throw std::runtime_error("Failed to initialize GLFW window.");
        }

        Time::Initialize();
        JobSystem::Start();
        const PlatformInfo& platform = Platform::GetInfo();
        LUMA_LOG_INFO(
            "Core",
            "Platform: " + platform.name +
                " | CPU Threads: " + std::to_string(platform.logicalCoreCount));

        Input::Initialize(m_Window);
        Input::RegisterContext(kApplicationInputContext, 1000, true);
        Input::RegisterAction(kActionExit, ActionValueType::Bool);
        Input::ClearActionBindings(kApplicationInputContext, kActionExit);
        Input::BindAction(
            kApplicationInputContext,
            kActionExit,
            InputBinding::Key(KeyCode::Escape, InputTrigger::Pressed));

        m_RenderBackend = CreateRenderBackend(m_Config.rendererAPI);
        if (!m_RenderBackend)
        {
            throw std::runtime_error("Failed to create render backend.");
        }

        if (!m_RenderBackend->Initialize(m_Window))
        {
            throw std::runtime_error("Render backend initialization failed.");
        }

        m_RenderBackend->SetVSyncEnabled(m_Config.vsyncEnabled);

        if (!InitializeImGui())
        {
            std::cerr << "ImGui initialization skipped or failed." << '\n';
        }

        std::cout << "Renderer backend: " << ToString(m_RenderBackend->GetAPI()) << '\n';
    }

    Application::~Application()
    {
        m_LayerStack.Clear();
        ShutdownImGui();

        if (m_RenderBackend)
        {
            m_RenderBackend->WaitIdle();
            m_RenderBackend->Shutdown();
            m_RenderBackend.reset();
        }

        ScriptEngine::Shutdown();
        Input::Shutdown();
        JobSystem::Stop();
        ShutdownWindow();
        const MemoryStats memoryStats = Memory::GetStats();
        LUMA_LOG_INFO(
            "Core",
            "Memory stats | Allocations: " + std::to_string(memoryStats.allocationCount) +
                ", Frees: " + std::to_string(memoryStats.freeCount) +
                ", LiveBytes: " + std::to_string(memoryStats.liveBytes));
        LUMA_LOG_INFO("Core", "Application shutdown complete.");
        s_Instance = nullptr;
    }

    void Application::PushLayer(std::unique_ptr<Layer> layer)
    {
        m_LayerStack.PushLayer(std::move(layer));
    }

    void Application::PushOverlay(std::unique_ptr<Layer> overlay)
    {
        m_LayerStack.PushOverlay(std::move(overlay));
    }

    int Application::Run()
    {
        std::uint32_t renderedFrames = 0;

        while (m_Running && !glfwWindowShouldClose(m_Window))
        {
            glfwPollEvents();
            Time::BeginFrame();
            Input::BeginFrame();

            if (Input::WasActionStarted(kActionExit))
            {
                if (RequestClose())
                {
                    m_Running = false;
                }
            }

            const float deltaTime = static_cast<float>(Time::GetDeltaSeconds());

            for (const auto& layer : m_LayerStack)
            {
                layer->OnUpdate(deltaTime);
            }

            m_RenderBackend->BeginFrame();
            for (const auto& layer : m_LayerStack)
            {
                layer->OnRender(*m_RenderBackend);
            }

            if (m_ImGuiEnabled)
            {
                ImGui_ImplOpenGL3_NewFrame();
                ImGui_ImplGlfw_NewFrame();
                ImGui::NewFrame();

                for (const auto& layer : m_LayerStack)
                {
                    layer->OnImGuiRender();
                }

                ImGui::Render();
                ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            }

            m_RenderBackend->EndFrame();

            if (m_Config.maxFrames > 0)
            {
                ++renderedFrames;
                if (renderedFrames >= m_Config.maxFrames)
                {
                    m_Running = false;
                }
            }
        }

        return 0;
    }

    void Application::RequestExit()
    {
        m_Running = false;
    }

    bool Application::RequestClose()
    {
        for (auto it = m_LayerStack.rbegin(); it != m_LayerStack.rend(); ++it)
        {
            if (!(*it)->OnCloseRequested())
            {
                return false;
            }
        }

        return true;
    }

    Application* Application::Get()
    {
        return s_Instance;
    }

    bool Application::InitializeWindow()
    {
        if (glfwInit() != GLFW_TRUE)
        {
            return false;
        }

        constexpr std::pair<int, int> kOpenGLVersions[] {
            { 4, 6 },
            { 4, 5 },
            { 4, 4 },
            { 4, 3 }
        };

        for (const auto& [majorVersion, minorVersion] : kOpenGLVersions)
        {
            glfwDefaultWindowHints();
            if (m_Config.startMaximized)
            {
                glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
            }

            glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, majorVersion);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minorVersion);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

            m_Window = glfwCreateWindow(
                static_cast<int>(m_Config.width),
                static_cast<int>(m_Config.height),
                m_Config.title.c_str(),
                nullptr,
                nullptr);
            if (m_Window != nullptr)
            {
                break;
            }
        }

        if (!m_Window)
        {
            return false;
        }

        glfwSetWindowUserPointer(m_Window, this);
        glfwSetFramebufferSizeCallback(m_Window, FramebufferResizeCallback);
        glfwSetWindowCloseCallback(m_Window, WindowCloseCallback);
        return true;
    }

    bool Application::InitializeImGui()
    {
        if (!m_Window || !m_RenderBackend)
        {
            return false;
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        SetupEditorFonts();
        ApplyLumaStyle();

        if (!ImGui_ImplGlfw_InitForOpenGL(m_Window, true))
        {
            ImGui::DestroyContext();
            return false;
        }

        if (!ImGui_ImplOpenGL3_Init("#version 430"))
        {
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            return false;
        }

        m_ImGuiEnabled = true;
        return true;
    }

    void Application::ShutdownWindow()
    {
        if (m_Window)
        {
            glfwDestroyWindow(m_Window);
            m_Window = nullptr;
        }

        glfwTerminate();
    }

    void Application::ShutdownImGui()
    {
        if (!m_ImGuiEnabled)
        {
            return;
        }

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        m_ImGuiEnabled = false;
    }

    void Application::HandleResize(const int width, const int height)
    {
        if (!m_RenderBackend || width <= 0 || height <= 0)
        {
            return;
        }

        m_RenderBackend->OnResize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
    }

    void Application::FramebufferResizeCallback(GLFWwindow* window, const int width, const int height)
    {
        auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
        if (app)
        {
            app->HandleResize(width, height);
        }
    }

    void Application::WindowCloseCallback(GLFWwindow* window)
    {
        auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
        if (app == nullptr)
        {
            return;
        }

        if (app->RequestClose())
        {
            app->m_Running = false;
            return;
        }

        glfwSetWindowShouldClose(window, GLFW_FALSE);
    }
}
