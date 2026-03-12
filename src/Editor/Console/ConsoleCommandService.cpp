#include "Luma/Editor/Console/ConsoleCommandService.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

#include "Luma/Asset/Streaming/ResourceStreamingService.h"
#include "Luma/Core/App/Application.h"
#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Scene/TagComponent.h"

namespace Luma::Editor
{
    namespace
    {
        constexpr std::size_t kMaxRecentCommands = 12;

        const char* StreamStateLabel(const Assets::StreamState state)
        {
            switch (state)
            {
            case Assets::StreamState::Queued:
                return "Queued";
            case Assets::StreamState::Streaming:
                return "Streaming";
            case Assets::StreamState::Resident:
                return "Resident";
            case Assets::StreamState::Evicted:
                return "Evicted";
            case Assets::StreamState::Failed:
                return "Failed";
            case Assets::StreamState::Cancelled:
                return "Cancelled";
            default:
                return "Unknown";
            }
        }

        std::string FormatStreamingBytes(const std::uint64_t bytes)
        {
            constexpr double kilobyte = 1024.0;
            constexpr double megabyte = kilobyte * 1024.0;
            constexpr double gigabyte = megabyte * 1024.0;

            std::ostringstream stream;
            stream.setf(std::ios::fixed, std::ios::floatfield);
            if (bytes >= static_cast<std::uint64_t>(gigabyte))
            {
                stream.precision(2);
                stream << static_cast<double>(bytes) / gigabyte << " GB";
            }
            else if (bytes >= static_cast<std::uint64_t>(megabyte))
            {
                stream.precision(2);
                stream << static_cast<double>(bytes) / megabyte << " MB";
            }
            else if (bytes >= static_cast<std::uint64_t>(kilobyte))
            {
                stream.precision(2);
                stream << static_cast<double>(bytes) / kilobyte << " KB";
            }
            else
            {
                stream.precision(0);
                stream << bytes << " B";
            }
            return stream.str();
        }

        std::string TrimCopy(std::string value)
        {
            const auto isSpace = [](const unsigned char character)
            {
                return std::isspace(character) != 0;
            };

            value.erase(
                value.begin(),
                std::find_if(
                    value.begin(),
                    value.end(),
                    [&](const unsigned char character)
                    {
                        return !isSpace(character);
                    }));
            value.erase(
                std::find_if(
                    value.rbegin(),
                    value.rend(),
                    [&](const unsigned char character)
                    {
                        return !isSpace(character);
                    })
                    .base(),
                value.end());
            return value;
        }

        std::string ToLowerString(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }

        std::vector<std::string> TokenizeCommandLine(const std::string& input)
        {
            std::vector<std::string> tokens;
            std::string current;
            bool inQuotes = false;

            for (const char character : input)
            {
                if (character == '"')
                {
                    inQuotes = !inQuotes;
                    continue;
                }

                if (!inQuotes && std::isspace(static_cast<unsigned char>(character)))
                {
                    if (!current.empty())
                    {
                        tokens.push_back(current);
                        current.clear();
                    }
                    continue;
                }

                current.push_back(character);
            }

            if (!current.empty())
            {
                tokens.push_back(current);
            }

            return tokens;
        }

        bool ParseToggleArg(const std::string& value, const bool currentValue, bool& outValue)
        {
            const std::string lowered = ToLowerString(value);
            if (lowered == "toggle")
            {
                outValue = !currentValue;
                return true;
            }
            if (lowered == "1" || lowered == "true" || lowered == "on" || lowered == "enable" || lowered == "enabled")
            {
                outValue = true;
                return true;
            }
            if (lowered == "0" || lowered == "false" || lowered == "off" || lowered == "disable" || lowered == "disabled")
            {
                outValue = false;
                return true;
            }
            return false;
        }

        Assets::StreamResourceType ParseStreamResourceType(const std::string& value)
        {
            const std::string lowered = ToLowerString(value);
            if (lowered == "texture")
            {
                return Assets::StreamResourceType::Texture;
            }
            if (lowered == "mesh")
            {
                return Assets::StreamResourceType::Mesh;
            }
            if (lowered == "audio")
            {
                return Assets::StreamResourceType::Audio;
            }
            if (lowered == "buffer")
            {
                return Assets::StreamResourceType::Buffer;
            }
            if (lowered == "package")
            {
                return Assets::StreamResourceType::PackageBlob;
            }
            if (lowered == "procedural")
            {
                return Assets::StreamResourceType::Procedural;
            }
            return Assets::StreamResourceType::Unknown;
        }

        Assets::StreamResourceType InferStreamResourceType(const std::filesystem::path& path)
        {
            const std::string extension = ToLowerString(path.extension().string());
            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".bmp" || extension == ".tga" ||
                extension == ".hdr" || extension == ".exr" || extension == ".dds" || extension == ".lumatex")
            {
                return Assets::StreamResourceType::Texture;
            }
            if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb" || extension == ".lumamesh")
            {
                return Assets::StreamResourceType::Mesh;
            }
            if (extension == ".wav" || extension == ".ogg" || extension == ".mp3" || extension == ".flac")
            {
                return Assets::StreamResourceType::Audio;
            }
            return Assets::StreamResourceType::Unknown;
        }

        Assets::StreamPriority ParseStreamPriority(const std::string& value)
        {
            const std::string lowered = ToLowerString(value);
            if (lowered == "background")
            {
                return Assets::StreamPriority::Background;
            }
            if (lowered == "low")
            {
                return Assets::StreamPriority::Low;
            }
            if (lowered == "high")
            {
                return Assets::StreamPriority::High;
            }
            if (lowered == "critical")
            {
                return Assets::StreamPriority::Critical;
            }
            return Assets::StreamPriority::Normal;
        }

        Assets::StreamRequestHandle FindStreamHandle(
            Assets::ResourceStreamingService& streamingService,
            const std::string& token)
        {
            bool numeric = !token.empty();
            for (const char character : token)
            {
                if (!std::isdigit(static_cast<unsigned char>(character)))
                {
                    numeric = false;
                    break;
                }
            }
            if (numeric)
            {
                try
                {
                    return static_cast<Assets::StreamRequestHandle>(std::stoull(token));
                }
                catch (...)
                {
                }
            }

            for (const Assets::StreamRecord& record : streamingService.GetRecords())
            {
                if (record.key == token || record.sourcePath.generic_string() == token)
                {
                    return record.handle;
                }
            }

            return 0;
        }

        void RecordCommandHistory(
            const std::string& rawLine,
            std::vector<std::string>& commandHistory,
            std::vector<std::string>& recentCommands,
            int& historyCursor)
        {
            if (commandHistory.empty() || commandHistory.back() != rawLine)
            {
                commandHistory.push_back(rawLine);
            }
            historyCursor = -1;

            const auto recentIt = std::find(recentCommands.begin(), recentCommands.end(), rawLine);
            if (recentIt != recentCommands.end())
            {
                recentCommands.erase(recentIt);
            }
            recentCommands.insert(recentCommands.begin(), rawLine);
            if (recentCommands.size() > kMaxRecentCommands)
            {
                recentCommands.resize(kMaxRecentCommands);
            }
        }
    }

    void ConsoleCommandService::Initialize(const ConsoleCommandInitializationContext& context) const
    {
        if (context.commands == nullptr || context.favoriteCommands == nullptr || context.cvars == nullptr || context.tasks == nullptr)
        {
            return;
        }

        *context.commands = {
            { "help", "Core", "List available console commands.", "help [filter]" },
            { "clear", "Core", "Clear the console output view.", "clear" },
            { "history.clear", "Core", "Clear command history.", "history.clear" },
            { "window.toggle", "Window", "Toggle an editor panel window.", "window.toggle <hierarchy|viewport|inspector|content|console|package|footer|gpu>" },
            { "view.grid", "View", "Set/toggle viewport grid.", "view.grid <on|off|toggle>" },
            { "view.debug", "View", "Set/toggle camera debug overlay.", "view.debug <on|off|toggle>" },
            { "project.settings", "Project", "Open project settings panel.", "project.settings" },
            { "preferences", "Project", "Open editor preferences panel.", "preferences" },
            { "package.manager", "Project", "Open package manager panel.", "package.manager" },
            { "packages.refresh", "Assets", "Refresh package registry index.", "packages.refresh" },
            { "packages.install", "Assets", "Install a package from the registry.", "packages.install <id> [version]" },
            { "packages.update", "Assets", "Update an installed package.", "packages.update <id> [version]" },
            { "packages.remove", "Assets", "Remove an installed package.", "packages.remove <id>" },
            { "packages.verify", "Assets", "Verify package install integrity.", "packages.verify [id]" },
            { "content.refresh", "Assets", "Rescan current content directory.", "content.refresh" },
            { "streaming.request", "Streaming", "Queue a background stream request.", "streaming.request <path> [type] [priority] [lod]" },
            { "streaming.cancel", "Streaming", "Cancel a queued or resident stream request.", "streaming.cancel <handle|key>" },
            { "streaming.release", "Streaming", "Release a streaming record by handle or key.", "streaming.release <handle|key>" },
            { "streaming.lod", "Streaming", "Retarget a stream request to another LOD.", "streaming.lod <handle> <lod>" },
            { "streaming.budget", "Streaming", "Inspect or set streaming budgets.", "streaming.budget [cpuMB gpuMB inFlight resident]" },
            { "streaming.list", "Streaming", "Print active streaming records.", "streaming.list" },
            { "streaming.meshlod", "Streaming", "Set primitive scene mesh LOD level.", "streaming.meshlod <0-3>" },
            { "renderer.profile", "Renderer", "Switch active render profile.", "renderer.profile <corelite|corex>" },
            { "log.level", "Debug", "Set global log level.", "log.level <trace|info|warn|error|fatal>" },
            { "stat.frame", "Debug", "Print current frame timing stats.", "stat.frame" },
            { "task.run", "Tasks", "Run quick editor task simulation.", "task.run <import|shaders|cook>" },
            { "select.entity", "Scene", "Select an entity by tag name.", "select.entity \"Entity Name\"" },
            { "cvar.set", "Console", "Set runtime cvar value.", "cvar.set <name> <value>" },
            { "cvar.get", "Console", "Get runtime cvar value.", "cvar.get <name>" },
            { "cvar.list", "Console", "List all runtime cvars.", "cvar.list" }
        };

        *context.favoriteCommands = { "help", "content.refresh", "renderer.profile", "view.grid", "window.toggle" };
        *context.cvars = {
            { "r.vsync", context.vsyncEnabled ? "1" : "0" },
            { "r.grid", context.viewportGridEnabled ? "1" : "0" },
            { "editor.camera.debug", context.cameraDebugOverlayEnabled ? "1" : "0" },
            { "streaming.mesh.lod", std::to_string(context.primitiveMeshLod) }
        };
        *context.tasks = {
            { "Import Megascans", "Runs the import pipeline against dropped content.", false, true, 0.0f, 0.24f },
            { "Rebuild Shaders", "Rebuilds editor shader permutations and cache.", false, true, 0.0f, 0.16f },
            { "Cook Project", "Builds cooked assets for the active project profile.", false, true, 0.0f, 0.12f }
        };
    }

    void ConsoleCommandService::Execute(
        const ConsoleCommandExecutionContext& context,
        const std::string_view commandLine,
        const bool addToHistory) const
    {
        const std::string rawLine = TrimCopy(std::string(commandLine));
        if (rawLine.empty())
        {
            return;
        }

        if (context.commandHistory == nullptr ||
            context.recentCommands == nullptr ||
            context.historyCursor == nullptr ||
            context.commands == nullptr ||
            context.cvars == nullptr ||
            context.tasks == nullptr)
        {
            return;
        }

        if (addToHistory)
        {
            RecordCommandHistory(rawLine, *context.commandHistory, *context.recentCommands, *context.historyCursor);
        }

        const std::vector<std::string> tokens = TokenizeCommandLine(rawLine);
        if (tokens.empty())
        {
            return;
        }

        const std::string command = ToLowerString(tokens.front());
        const std::vector<std::string> args(tokens.begin() + 1, tokens.end());

        if (command == "help")
        {
            const std::string filter = args.empty() ? std::string() : ToLowerString(args[0]);
            std::string result = "Commands:";
            for (const ConsoleCommandDesc& desc : *context.commands)
            {
                if (!filter.empty())
                {
                    const std::string haystack = ToLowerString(desc.name + " " + desc.description + " " + desc.category);
                    if (haystack.find(filter) == std::string::npos)
                    {
                        continue;
                    }
                }
                result += " " + desc.name + ";";
            }
            LUMA_LOG_INFO("Console", result);
            return;
        }

        if (command == "clear")
        {
            if (context.clearConsoleOutput)
            {
                context.clearConsoleOutput();
            }
            LUMA_LOG_INFO("Console", "Console output cleared.");
            return;
        }

        if (command == "history.clear")
        {
            context.commandHistory->clear();
            *context.historyCursor = -1;
            LUMA_LOG_INFO("Console", "Command history cleared.");
            return;
        }

        if (command == "window.toggle")
        {
            if (args.empty())
            {
                LUMA_LOG_WARN("Console", "Usage: window.toggle <hierarchy|viewport|inspector|content|console|package|footer|gpu>");
                return;
            }
            const std::string target = ToLowerString(args[0]);
            if (target == "hierarchy" && context.showHierarchyPanel != nullptr)
            {
                *context.showHierarchyPanel = !*context.showHierarchyPanel;
            }
            else if (target == "viewport" && context.showViewportPanel != nullptr)
            {
                *context.showViewportPanel = !*context.showViewportPanel;
            }
            else if (target == "inspector" && context.showInspectorPanel != nullptr)
            {
                *context.showInspectorPanel = !*context.showInspectorPanel;
            }
            else if (target == "content" && context.showContentBrowserPanel != nullptr)
            {
                *context.showContentBrowserPanel = !*context.showContentBrowserPanel;
            }
            else if (target == "console" && context.showConsolePanel != nullptr)
            {
                *context.showConsolePanel = !*context.showConsolePanel;
            }
            else if (target == "package" && context.showPackageManagerPanel != nullptr)
            {
                *context.showPackageManagerPanel = !*context.showPackageManagerPanel;
            }
            else if (target == "footer" && context.showFooter != nullptr)
            {
                *context.showFooter = !*context.showFooter;
            }
            else if (target == "gpu" && context.showGpuResourcesPanel != nullptr)
            {
                *context.showGpuResourcesPanel = !*context.showGpuResourcesPanel;
            }
            else
            {
                LUMA_LOG_WARN("Console", "Unknown window target: " + args[0]);
                return;
            }
            LUMA_LOG_INFO("Console", "Toggled window: " + args[0]);
            return;
        }

        if (command == "view.grid")
        {
            if (context.viewportGridEnabled == nullptr)
            {
                return;
            }

            bool next = *context.viewportGridEnabled;
            if (!args.empty() && !ParseToggleArg(args[0], *context.viewportGridEnabled, next))
            {
                LUMA_LOG_WARN("Console", "Usage: view.grid <on|off|toggle>");
                return;
            }
            if (args.empty())
            {
                next = !next;
            }
            *context.viewportGridEnabled = next;
            (*context.cvars)["r.grid"] = *context.viewportGridEnabled ? "1" : "0";
            LUMA_LOG_INFO("Console", std::string("Viewport grid ") + (*context.viewportGridEnabled ? "enabled" : "disabled"));
            return;
        }

        if (command == "view.debug")
        {
            if (context.cameraDebugOverlayEnabled == nullptr)
            {
                return;
            }

            bool next = *context.cameraDebugOverlayEnabled;
            if (!args.empty() && !ParseToggleArg(args[0], *context.cameraDebugOverlayEnabled, next))
            {
                LUMA_LOG_WARN("Console", "Usage: view.debug <on|off|toggle>");
                return;
            }
            if (args.empty())
            {
                next = !next;
            }
            *context.cameraDebugOverlayEnabled = next;
            (*context.cvars)["editor.camera.debug"] = *context.cameraDebugOverlayEnabled ? "1" : "0";
            LUMA_LOG_INFO(
                "Console",
                std::string("Camera debug overlay ") + (*context.cameraDebugOverlayEnabled ? "enabled" : "disabled"));
            return;
        }

        if (command == "project.settings")
        {
            if (context.openProjectSettings)
            {
                context.openProjectSettings();
            }
            LUMA_LOG_INFO("Console", "Project Settings opened.");
            return;
        }

        if (command == "preferences")
        {
            if (context.openPreferences)
            {
                context.openPreferences();
            }
            LUMA_LOG_INFO("Console", "Preferences opened.");
            return;
        }

        if (command == "package.manager")
        {
            if (context.openPackageManager)
            {
                context.openPackageManager();
            }
            LUMA_LOG_INFO("Console", "Package Manager opened.");
            return;
        }

        if (command == "packages.refresh")
        {
            if (context.requestPackagesRefresh)
            {
                context.requestPackagesRefresh();
            }
            return;
        }

        if (command == "packages.install")
        {
            if (args.empty())
            {
                LUMA_LOG_WARN("Console", "Usage: packages.install <id> [version]");
                return;
            }
            if (context.requestPackagesInstall)
            {
                context.requestPackagesInstall(args[0], args.size() >= 2 ? args[1] : std::string_view {});
            }
            return;
        }

        if (command == "packages.update")
        {
            if (args.empty())
            {
                LUMA_LOG_WARN("Console", "Usage: packages.update <id> [version]");
                return;
            }
            if (context.requestPackagesUpdate)
            {
                context.requestPackagesUpdate(args[0], args.size() >= 2 ? args[1] : std::string_view {});
            }
            return;
        }

        if (command == "packages.remove")
        {
            if (args.empty())
            {
                LUMA_LOG_WARN("Console", "Usage: packages.remove <id>");
                return;
            }
            if (context.requestPackagesRemove)
            {
                context.requestPackagesRemove(args[0]);
            }
            return;
        }

        if (command == "packages.verify")
        {
            if (context.requestPackagesVerify)
            {
                context.requestPackagesVerify(args.empty() ? std::string_view {} : std::string_view(args[0]));
            }
            return;
        }

        if (command == "content.refresh")
        {
            if (context.refreshContentBrowser)
            {
                context.refreshContentBrowser();
            }
            LUMA_LOG_INFO("Console", "Content browser refreshed.");
            return;
        }

        if (command == "streaming.request")
        {
            if (context.resourceStreamingService == nullptr)
            {
                return;
            }
            if (args.empty())
            {
                LUMA_LOG_WARN("Console", "Usage: streaming.request <path> [type] [priority] [lod]");
                return;
            }

            std::filesystem::path sourcePath = args[0];
            Assets::StreamRequestDesc request {};
            request.key = sourcePath.generic_string();
            request.sourcePath = sourcePath;
            request.resourceType = args.size() >= 2 ? ParseStreamResourceType(args[1]) : InferStreamResourceType(sourcePath);
            request.priority = args.size() >= 3 ? ParseStreamPriority(args[2]) : Assets::StreamPriority::Normal;
            if (args.size() >= 4)
            {
                try
                {
                    request.lod.mode = Assets::LODStreamingMode::Explicit;
                    request.lod.targetLod = static_cast<std::uint32_t>(std::stoul(args[3]));
                }
                catch (...)
                {
                    LUMA_LOG_WARN("Console", "Usage: streaming.request <path> [type] [priority] [lod]");
                    return;
                }
            }

            std::string error;
            const Assets::StreamRequestHandle handle = context.resourceStreamingService->Request(request, error);
            if (handle == 0)
            {
                LUMA_LOG_WARN("Streaming", error.empty() ? "Failed to queue stream request." : error);
                return;
            }

            LUMA_LOG_INFO("Streaming", "Queued stream request " + std::to_string(handle) + " for " + request.key + ".");
            return;
        }

        if (command == "streaming.cancel")
        {
            if (context.resourceStreamingService == nullptr)
            {
                return;
            }
            if (args.empty())
            {
                LUMA_LOG_WARN("Console", "Usage: streaming.cancel <handle|key>");
                return;
            }

            const Assets::StreamRequestHandle handle = FindStreamHandle(*context.resourceStreamingService, args[0]);
            if (handle == 0 || !context.resourceStreamingService->Cancel(handle))
            {
                LUMA_LOG_WARN("Streaming", "Unknown streaming request: " + args[0]);
                return;
            }

            LUMA_LOG_INFO("Streaming", "Cancelled stream request " + args[0] + ".");
            return;
        }

        if (command == "streaming.release")
        {
            if (context.resourceStreamingService == nullptr)
            {
                return;
            }
            if (args.empty())
            {
                LUMA_LOG_WARN("Console", "Usage: streaming.release <handle|key>");
                return;
            }

            const Assets::StreamRequestHandle handle = FindStreamHandle(*context.resourceStreamingService, args[0]);
            if (handle == 0 || !context.resourceStreamingService->Release(handle))
            {
                LUMA_LOG_WARN("Streaming", "Unknown streaming request: " + args[0]);
                return;
            }

            LUMA_LOG_INFO("Streaming", "Released stream request " + args[0] + ".");
            return;
        }

        if (command == "streaming.lod")
        {
            if (context.resourceStreamingService == nullptr)
            {
                return;
            }
            if (args.size() < 2)
            {
                LUMA_LOG_WARN("Console", "Usage: streaming.lod <handle> <lod>");
                return;
            }

            const Assets::StreamRequestHandle handle = FindStreamHandle(*context.resourceStreamingService, args[0]);
            if (handle == 0)
            {
                LUMA_LOG_WARN("Streaming", "Unknown streaming request: " + args[0]);
                return;
            }

            std::uint32_t lod = 0;
            try
            {
                lod = static_cast<std::uint32_t>(std::stoul(args[1]));
            }
            catch (...)
            {
                LUMA_LOG_WARN("Console", "Usage: streaming.lod <handle> <lod>");
                return;
            }

            std::string error;
            if (!context.resourceStreamingService->RetargetLOD(handle, lod, error))
            {
                LUMA_LOG_WARN("Streaming", error.empty() ? "Failed to retarget LOD." : error);
                return;
            }

            LUMA_LOG_INFO("Streaming", "Retargeted stream request " + std::to_string(handle) + " to LOD " + std::to_string(lod) + ".");
            return;
        }

        if (command == "streaming.budget")
        {
            if (context.resourceStreamingService == nullptr)
            {
                return;
            }
            if (args.empty())
            {
                const Assets::StreamingBudget budget = context.resourceStreamingService->GetBudget();
                LUMA_LOG_INFO(
                    "Streaming",
                    "Budget CPU=" + FormatStreamingBytes(budget.maxCpuResidentBytes) +
                        " GPU=" + FormatStreamingBytes(budget.maxGpuResidentBytes) +
                        " InFlight=" + std::to_string(budget.maxInFlightRequests) +
                        " Resident=" + std::to_string(budget.maxResidentRecords) + ".");
                return;
            }

            if (args.size() < 4)
            {
                LUMA_LOG_WARN("Console", "Usage: streaming.budget [cpuMB gpuMB inFlight resident]");
                return;
            }

            Assets::StreamingBudget budget = context.resourceStreamingService->GetBudget();
            try
            {
                budget.maxCpuResidentBytes = static_cast<std::uint64_t>(std::stoull(args[0])) * 1024ull * 1024ull;
                budget.maxGpuResidentBytes = static_cast<std::uint64_t>(std::stoull(args[1])) * 1024ull * 1024ull;
                budget.maxInFlightRequests = static_cast<std::uint32_t>(std::stoul(args[2]));
                budget.maxResidentRecords = static_cast<std::uint32_t>(std::stoul(args[3]));
            }
            catch (...)
            {
                LUMA_LOG_WARN("Console", "Usage: streaming.budget [cpuMB gpuMB inFlight resident]");
                return;
            }

            context.resourceStreamingService->SetBudget(budget);
            LUMA_LOG_INFO("Streaming", "Streaming budget updated.");
            return;
        }

        if (command == "streaming.list")
        {
            if (context.resourceStreamingService == nullptr)
            {
                return;
            }
            const std::vector<Assets::StreamRecord> records = context.resourceStreamingService->GetRecords();
            if (records.empty())
            {
                LUMA_LOG_INFO("Streaming", "No streaming records.");
                return;
            }

            std::string message = "Streaming:";
            for (const Assets::StreamRecord& record : records)
            {
                message += " [" + std::to_string(record.handle) + " " + record.key + " " + StreamStateLabel(record.state) +
                    " lod " + std::to_string(record.resolvedLod) + "/" + std::to_string(record.targetLod) + "]";
            }
            LUMA_LOG_INFO("Streaming", message);
            return;
        }

        if (command == "streaming.meshlod")
        {
            if (context.primitiveMeshLod == nullptr)
            {
                return;
            }
            if (args.empty())
            {
                LUMA_LOG_INFO("Streaming", "Primitive mesh LOD is " + std::to_string(*context.primitiveMeshLod) + ".");
                return;
            }

            try
            {
                *context.primitiveMeshLod = std::min<std::uint32_t>(static_cast<std::uint32_t>(std::stoul(args[0])), 3u);
                (*context.cvars)["streaming.mesh.lod"] = std::to_string(*context.primitiveMeshLod);
                if (context.markSceneRenderCacheDirty)
                {
                    context.markSceneRenderCacheDirty();
                }
            }
            catch (...)
            {
                LUMA_LOG_WARN("Console", "Usage: streaming.meshlod <0-3>");
                return;
            }

            LUMA_LOG_INFO("Streaming", "Primitive mesh LOD set to " + std::to_string(*context.primitiveMeshLod) + ".");
            return;
        }

        if (command == "renderer.profile")
        {
            if (context.activeProfile == nullptr)
            {
                return;
            }
            if (args.empty())
            {
                LUMA_LOG_WARN("Console", "Usage: renderer.profile <corelite|corex>");
                return;
            }
            const std::string profile = ToLowerString(args[0]);
            if (profile == "corelite")
            {
                *context.activeProfile = RenderPipelineProfile::CoreLite;
            }
            else if (profile == "corex")
            {
                *context.activeProfile = RenderPipelineProfile::CoreX;
            }
            else
            {
                LUMA_LOG_WARN("Console", "Unknown profile: " + args[0]);
                return;
            }

            LUMA_LOG_INFO("Console", "Active profile set to " + args[0] + ".");
            return;
        }

        if (command == "log.level")
        {
            if (args.empty())
            {
                LUMA_LOG_WARN("Console", "Usage: log.level <trace|info|warn|error|fatal>");
                return;
            }
            const std::string level = ToLowerString(args[0]);
            if (level == "trace")
            {
                Logger::SetLevel(LogLevel::Trace);
            }
            else if (level == "info")
            {
                Logger::SetLevel(LogLevel::Info);
            }
            else if (level == "warn")
            {
                Logger::SetLevel(LogLevel::Warn);
            }
            else if (level == "error")
            {
                Logger::SetLevel(LogLevel::Error);
            }
            else if (level == "fatal")
            {
                Logger::SetLevel(LogLevel::Fatal);
            }
            else
            {
                LUMA_LOG_WARN("Console", "Unknown log level: " + args[0]);
                return;
            }
            LUMA_LOG_INFO("Console", "Log level set to " + level + ".");
            return;
        }

        if (command == "stat.frame")
        {
            const float delta = std::max(context.lastDeltaTimeSeconds, 1.0e-4f);
            const float fps = 1.0f / delta;
            const float frameMs = delta * 1000.0f;
            std::ostringstream result;
            result << "Frame: " << std::round(fps) << " FPS | " << frameMs << " ms";
            LUMA_LOG_INFO("Console", result.str());
            return;
        }

        if (command == "task.run")
        {
            if (args.empty())
            {
                LUMA_LOG_WARN("Console", "Usage: task.run <import|shaders|cook>");
                return;
            }
            const std::string target = ToLowerString(args[0]);
            auto runTask = [&](const std::string& taskName) -> bool
            {
                for (ConsoleTaskState& task : *context.tasks)
                {
                    if (ToLowerString(task.name).find(ToLowerString(taskName)) != std::string::npos)
                    {
                        task.running = true;
                        task.progress = 0.0f;
                        LUMA_LOG_INFO("Task", "Started task: " + task.name);
                        return true;
                    }
                }
                return false;
            };

            bool started = false;
            if (target == "import")
            {
                started = runTask("megascans");
            }
            else if (target == "shaders")
            {
                started = runTask("shaders");
            }
            else if (target == "cook")
            {
                started = runTask("cook");
            }
            if (!started)
            {
                LUMA_LOG_WARN("Console", "Unknown task target: " + args[0]);
            }
            return;
        }

        if (command == "select.entity")
        {
            if (args.empty())
            {
                LUMA_LOG_WARN("Console", "Usage: select.entity \"Entity Name\"");
                return;
            }
            if (context.scene == nullptr || !context.selectSingleEntity)
            {
                return;
            }

            const std::string targetName = ToLowerString(args[0]);
            auto& registry = context.scene->GetRegistry();
            const auto view = registry.view<TagComponent>();
            for (const EntityID entity : view)
            {
                const auto& tag = view.get<TagComponent>(entity);
                if (ToLowerString(tag.name) == targetName)
                {
                    context.selectSingleEntity(entity);
                    LUMA_LOG_INFO("Console", "Selected entity: " + tag.name);
                    return;
                }
            }
            LUMA_LOG_WARN("Console", "Entity not found: " + args[0]);
            return;
        }

        if (command == "cvar.set")
        {
            if (args.size() < 2)
            {
                LUMA_LOG_WARN("Console", "Usage: cvar.set <name> <value>");
                return;
            }
            if (ToLowerString(args[0]) == "r.vsync")
            {
                bool enabled = false;
                const bool currentValue =
                    Application::Get() != nullptr ? Application::Get()->GetRenderer().IsVSyncEnabled() : false;
                if (!ParseToggleArg(args[1], currentValue, enabled))
                {
                    LUMA_LOG_WARN("Console", "Usage: cvar.set r.vsync <on|off|toggle>");
                    return;
                }

                if (Application* app = Application::Get(); app != nullptr)
                {
                    app->GetRenderer().SetVSyncEnabled(enabled);
                }
                (*context.cvars)["r.vsync"] = enabled ? "1" : "0";
                LUMA_LOG_INFO("Console", std::string("Set cvar r.vsync = ") + (enabled ? "1" : "0"));
                return;
            }
            (*context.cvars)[args[0]] = args[1];
            LUMA_LOG_INFO("Console", "Set cvar " + args[0] + " = " + args[1]);
            return;
        }

        if (command == "cvar.get")
        {
            if (args.empty())
            {
                LUMA_LOG_WARN("Console", "Usage: cvar.get <name>");
                return;
            }
            if (ToLowerString(args[0]) == "r.vsync")
            {
                const bool enabled =
                    Application::Get() != nullptr ? Application::Get()->GetRenderer().IsVSyncEnabled() : false;
                (*context.cvars)["r.vsync"] = enabled ? "1" : "0";
            }
            const auto it = context.cvars->find(args[0]);
            if (it == context.cvars->end())
            {
                LUMA_LOG_WARN("Console", "Unknown cvar: " + args[0]);
                return;
            }
            LUMA_LOG_INFO("Console", it->first + " = " + it->second);
            return;
        }

        if (command == "cvar.list")
        {
            std::string result = "Cvars:";
            for (const auto& [name, value] : *context.cvars)
            {
                result += " " + name + "=" + value + ";";
            }
            LUMA_LOG_INFO("Console", result);
            return;
        }

        LUMA_LOG_WARN("Console", "Unknown command: " + rawLine);
    }
}
