#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "Luma/Core/App/Application.h"
#include "Luma/Core/Foundation/Assert.h"
#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Core/Foundation/Platform.h"
#include "Luma/Core/App/Project.h"
#include "Luma/Asset/CLI/AssetCLI.h"
#include "Luma/Core/App/RenderSelection.h"
#include "Luma/RHI/RendererAPI.h"
#include "Luma/Layers/EditorLayer.h"

namespace
{
    struct LaunchArgs
    {
        std::optional<std::filesystem::path> projectFile;
        std::optional<Luma::RenderPipelineProfile> pipelineOverride;
        std::optional<Luma::BackendPreference> backendOverride;
        std::optional<Luma::RendererAPI> apiOverride;
        std::optional<std::uint32_t> smokeTestFrames;
    };

    std::optional<std::uint32_t> ParseUnsignedInteger(const std::string_view value)
    {
        if (value.empty())
        {
            return std::nullopt;
        }

        std::uint32_t parsedValue = 0;
        for (const char character : value)
        {
            if (!std::isdigit(static_cast<unsigned char>(character)))
            {
                return std::nullopt;
            }

            parsedValue = (parsedValue * 10u) + static_cast<std::uint32_t>(character - '0');
        }

        return parsedValue;
    }

    std::string ToLower(std::string value)
    {
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](const unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
        return value;
    }

    std::optional<Luma::RendererAPI> ParseRendererAPI(const std::string_view value)
    {
        if (value == "opengl")
        {
            return Luma::RendererAPI::OpenGL;
        }
        return std::nullopt;
    }

    std::optional<Luma::BackendPreference> ParseBackendPreference(const std::string_view value)
    {
        if (value == "auto")
        {
            return Luma::BackendPreference::Auto;
        }
        if (value == "opengl")
        {
            return Luma::BackendPreference::OpenGL;
        }
        return std::nullopt;
    }

    std::optional<Luma::RenderPipelineProfile> ParseRenderPipeline(const std::string_view value)
    {
        if (value == "corelite")
        {
            return Luma::RenderPipelineProfile::CoreLite;
        }
        if (value == "corex")
        {
            return Luma::RenderPipelineProfile::CoreX;
        }
        return std::nullopt;
    }

    LaunchArgs ParseLaunchArgs(const int argc, char** argv)
    {
        LaunchArgs args;

        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            const std::string loweredArg = ToLower(arg);

            if (loweredArg.rfind("--api=", 0) == 0)
            {
                const std::string value = loweredArg.substr(6);
                if (const auto parsed = ParseRendererAPI(value))
                {
                    args.apiOverride = parsed;
                }
                continue;
            }

            if (loweredArg.rfind("--backend=", 0) == 0)
            {
                const std::string value = loweredArg.substr(10);
                if (const auto parsed = ParseBackendPreference(value))
                {
                    args.backendOverride = parsed;
                }
                continue;
            }

            if (loweredArg.rfind("--pipeline=", 0) == 0)
            {
                const std::string value = loweredArg.substr(11);
                if (const auto parsed = ParseRenderPipeline(value))
                {
                    args.pipelineOverride = parsed;
                }
                continue;
            }

            if (loweredArg.rfind("--project=", 0) == 0)
            {
                const std::string value = arg.substr(10);
                args.projectFile = std::filesystem::path(value);
                continue;
            }

            if (loweredArg == "--project" && (i + 1) < argc)
            {
                args.projectFile = std::filesystem::path(argv[++i]);
                continue;
            }

            if (loweredArg == "--smoke-test")
            {
                args.smokeTestFrames = 120;
                continue;
            }

            if (loweredArg.rfind("--smoke-test=", 0) == 0)
            {
                const std::string value = loweredArg.substr(13);
                if (const auto parsed = ParseUnsignedInteger(value))
                {
                    args.smokeTestFrames = std::max(*parsed, 1u);
                }
                continue;
            }
        }

        return args;
    }

    Luma::BackendPreference RendererToBackendPreference(const Luma::RendererAPI api)
    {
        (void)api;
        return Luma::BackendPreference::OpenGL;
    }
}

int main(int argc, char** argv)
{
    Luma::Logger::Initialize();
    Luma::Logger::SetLevel(Luma::LogLevel::Trace);
    const Luma::PlatformInfo& platformInfo = Luma::Platform::GetInfo();
    LUMA_LOG_INFO(
        "Core",
        "Booting Luma on " + platformInfo.name +
            " (PID " + std::to_string(Luma::Platform::GetProcessId()) + ")");

    try
    {
        if (const std::optional<int> cliExitCode = Luma::Assets::TryRunAssetCli(argc, argv);
            cliExitCode.has_value())
        {
            Luma::Logger::Shutdown();
            return *cliExitCode;
        }

        const LaunchArgs launchArgs = ParseLaunchArgs(argc, argv);

        bool hasProject = false;
        std::filesystem::path projectFile;
        Luma::Project::ProjectConfig projectConfig {};

        if (launchArgs.projectFile.has_value())
        {
            projectFile = launchArgs.projectFile->lexically_normal();
            if (!std::filesystem::exists(projectFile))
            {
                std::cerr << "Project file does not exist: " << projectFile.string() << '\n';
                return 1;
            }

            if (!Luma::Project::PeekConfig(projectFile, projectConfig))
            {
                std::cerr << "Failed to parse project config: " << projectFile.string() << '\n';
                return 1;
            }

            hasProject = true;
        }

        Luma::RenderPipelineProfile selectedPipeline =
            launchArgs.pipelineOverride.value_or(
                hasProject ? projectConfig.pipeline : Luma::RenderPipelineProfile::CoreLite);

        Luma::BackendPreference selectedBackendPreference = Luma::BackendPreference::Auto;
        if (launchArgs.backendOverride.has_value())
        {
            selectedBackendPreference = launchArgs.backendOverride.value();
        }
        else if (launchArgs.apiOverride.has_value())
        {
            selectedBackendPreference = RendererToBackendPreference(launchArgs.apiOverride.value());
        }
        else if (hasProject)
        {
            selectedBackendPreference = projectConfig.backend;
        }

        const Luma::RenderSelectionResult selection =
            Luma::ResolveRenderSelection(selectedPipeline, selectedBackendPreference);
        if (selection.hardFailure || !selection.available)
        {
            LUMA_LOG_ERROR("Core", "Renderer selection failed: " + selection.message);
            return 1;
        }
        if (!selection.message.empty())
        {
            LUMA_LOG_INFO("Core", selection.message);
        }

        if (hasProject)
        {
            if (!Luma::Project::Load(projectFile))
            {
                std::cerr << "Failed to load project: " << projectFile.string() << '\n';
                return 1;
            }
        }

        Luma::ApplicationConfig config;
        config.title = hasProject
                           ? ("Luma - " + Luma::Project::GetConfig().name)
                           : "Project Browser";
        config.width = 1000;
        config.height = 650;
        config.rendererAPI = selection.rendererAPI;
        config.startMaximized = hasProject;
        config.maxFrames = launchArgs.smokeTestFrames.value_or(0);

        Luma::Application app(config);
        LUMA_CORE_ASSERT(config.width > 0 && config.height > 0, "Invalid application window dimensions.");
        app.PushLayer(std::make_unique<Luma::EditorLayer>());
        const int runResult = app.Run();
        Luma::Logger::Shutdown();
        return runResult;
    }
    catch (const std::exception& e)
    {
        LUMA_LOG_FATAL("Core", std::string("Fatal error: ") + e.what());
        Luma::Logger::Shutdown();
        return 1;
    }
}
