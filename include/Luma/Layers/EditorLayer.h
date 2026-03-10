#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "Luma/Core/App/Layer.h"

namespace Luma
{
    class ProjectManagerLayer;
    class TriangleLayer;

    enum class EditorState
    {
        Startup,
        ProjectBrowser,
        MainEditor
    };

    enum class EditorTransition
    {
        None = 0,
        EnterMainEditor
    };

    enum class EngineBootState
    {
        Bootstrap = 0,
        CoreInit,
        RendererInit,
        ModuleLoad,
        AssetScan,
        ScriptCompile,
        ShaderCompile,
        ProjectLoad,
        EditorInit,
        Ready
    };

    class EditorLayer final : public Layer
    {
    public:
        EditorLayer();
        ~EditorLayer() override;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(float deltaTimeSeconds) override;
        void OnRender(IRenderBackend& renderer) override;
        void OnImGuiRender() override;

    private:
        void EnterMainEditorImmediate();
        void BeginMainEditorTransition(const std::string& taskTitle);
        void TickTransition(float deltaTimeSeconds);
        void ShowProjectBrowser();
        void LoadMainEditor();
        void AdvanceMainEditorBoot(float deltaTimeSeconds);
        void DrawStartupOverlay();
        void UpdateStartupStatus(std::string title, std::string subtask, float progress, std::string detail = {});
        bool EnsureStartupSplashTextureLoaded();
        void ReleaseStartupSplashTexture();
        void SetMainWindowTitle(const std::string& title) const;
        void SetMainWindowFullscreen(bool fullscreen);

        EditorState m_State = EditorState::Startup;
        std::unique_ptr<ProjectManagerLayer> m_ProjectBrowserLayer;
        std::unique_ptr<TriangleLayer> m_MainEditorLayer;
        bool m_ProjectBrowserAttached = false;
        bool m_MainEditorAttached = false;
        EditorTransition m_Transition = EditorTransition::None;
        EngineBootState m_BootState = EngineBootState::Bootstrap;
        float m_BootStateElapsedSeconds = 0.0f;
        IRenderBackend* m_StartupRenderBackend = nullptr;
        IRenderBackend* m_StartupSplashTextureOwner = nullptr;
        void* m_StartupSplashTexture = nullptr;
        std::uint32_t m_StartupSplashWidth = 0;
        std::uint32_t m_StartupSplashHeight = 0;
        bool m_StartupSplashLoadAttempted = false;
        std::string m_StartupTitle = "Opening Project...";
        std::string m_StartupSubtask = "Bootstrapping runtime...";
        std::string m_StartupDetail;
        float m_StartupProgress = 0.0f;
        bool m_MainWindowFullscreen = false;
        std::uint32_t m_StartupReadyFrameCount = 0;
    };
}
