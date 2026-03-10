#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Luma/Core/App/Project.h"
#include "Luma/Editor/Panels/Console/CommandPalettePanel.h"
#include "Luma/Editor/Panels/Console/ConsoleTasksPanel.h"
#include "Luma/Scene/Scene.h"

namespace Luma::Assets
{
    class ResourceStreamingService;
}

namespace Luma::Editor
{
    struct ConsoleCommandInitializationContext
    {
        std::vector<ConsoleCommandDesc>* commands = nullptr;
        std::vector<std::string>* favoriteCommands = nullptr;
        std::unordered_map<std::string, std::string>* cvars = nullptr;
        std::vector<ConsoleTaskState>* tasks = nullptr;
        bool vsyncEnabled = false;
        bool viewportGridEnabled = false;
        bool cameraDebugOverlayEnabled = false;
        std::uint32_t primitiveMeshLod = 0;
    };

    struct ConsoleCommandExecutionContext
    {
        std::vector<ConsoleCommandDesc>* commands = nullptr;
        std::vector<std::string>* favoriteCommands = nullptr;
        std::unordered_map<std::string, std::string>* cvars = nullptr;
        std::vector<ConsoleTaskState>* tasks = nullptr;
        std::vector<std::string>* commandHistory = nullptr;
        std::vector<std::string>* recentCommands = nullptr;
        int* historyCursor = nullptr;

        bool* showHierarchyPanel = nullptr;
        bool* showViewportPanel = nullptr;
        bool* showInspectorPanel = nullptr;
        bool* showContentBrowserPanel = nullptr;
        bool* showConsolePanel = nullptr;
        bool* showPackageManagerPanel = nullptr;
        bool* showFooter = nullptr;
        bool* showGpuResourcesPanel = nullptr;
        bool* showProjectSettingsPanel = nullptr;
        bool* showPreferencesPanel = nullptr;
        bool* viewportGridEnabled = nullptr;
        bool* cameraDebugOverlayEnabled = nullptr;

        RenderPipelineProfile* activeProfile = nullptr;
        float lastDeltaTimeSeconds = 0.0f;
        std::uint32_t* primitiveMeshLod = nullptr;

        Assets::ResourceStreamingService* resourceStreamingService = nullptr;
        Scene* scene = nullptr;

        std::function<void(EntityID)> selectSingleEntity;
        std::function<void()> refreshContentBrowser;
        std::function<void()> openProjectSettings;
        std::function<void()> openPreferences;
        std::function<void()> openPackageManager;
        std::function<void()> requestPackagesRefresh;
        std::function<void(std::string_view, std::string_view)> requestPackagesInstall;
        std::function<void(std::string_view, std::string_view)> requestPackagesUpdate;
        std::function<void(std::string_view)> requestPackagesRemove;
        std::function<void(std::string_view)> requestPackagesVerify;
        std::function<void()> markSceneRenderCacheDirty;
        std::function<void()> clearConsoleOutput;
    };

    class ConsoleCommandService
    {
    public:
        void Initialize(const ConsoleCommandInitializationContext& context) const;
        void Execute(const ConsoleCommandExecutionContext& context, std::string_view commandLine, bool addToHistory = true) const;
    };
}
