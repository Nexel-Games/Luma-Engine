#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "Luma/Core/App/LayerStack.h"
#include "Luma/RHI/IRenderBackend.h"

struct GLFWwindow;

namespace Luma
{
    struct ApplicationConfig
    {
        std::string title = "Luma";
        std::uint32_t width = 1600;
        std::uint32_t height = 900;
        RendererAPI rendererAPI = RendererAPI::OpenGL;
        bool vsyncEnabled = true;
        bool startMaximized = false;
        std::uint32_t maxFrames = 0;
    };

    class Application
    {
    public:
        explicit Application(ApplicationConfig config);
        ~Application();

        Application(const Application&) = delete;
        Application& operator=(const Application&) = delete;
        Application(Application&&) = delete;
        Application& operator=(Application&&) = delete;

        void PushLayer(std::unique_ptr<Layer> layer);
        void PushOverlay(std::unique_ptr<Layer> overlay);

        int Run();
        void RequestExit();
        static Application* Get();

        GLFWwindow* GetWindowHandle() const
        {
            return m_Window;
        }

        IRenderBackend& GetRenderer() const
        {
            return *m_RenderBackend;
        }

        const ApplicationConfig& GetConfig() const
        {
            return m_Config;
        }

    private:
        bool RequestClose();
        bool InitializeWindow();
        bool InitializeImGui();
        void ShutdownWindow();
        void ShutdownImGui();
        void HandleResize(int width, int height);
        static void FramebufferResizeCallback(GLFWwindow* window, int width, int height);
        static void WindowCloseCallback(GLFWwindow* window);
        static Application* s_Instance;

        ApplicationConfig m_Config;
        GLFWwindow* m_Window = nullptr;
        bool m_Running = true;
        LayerStack m_LayerStack;
        std::unique_ptr<IRenderBackend> m_RenderBackend;
        bool m_ImGuiEnabled = false;
    };
}
