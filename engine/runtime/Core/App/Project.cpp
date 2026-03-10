#include "Luma/Core/App/Project.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace Luma
{
    std::filesystem::path Project::s_ProjectRoot {};
    std::filesystem::path Project::s_ProjectFile {};
    Project::ProjectConfig Project::s_Config {};
    bool Project::s_Loaded = false;

    namespace
    {
        std::string Trim(std::string value)
        {
            const auto isSpace = [](const char c)
            {
                return c == ' ' || c == '\t' || c == '\r' || c == '\n';
            };

            while (!value.empty() && isSpace(value.front()))
            {
                value.erase(value.begin());
            }
            while (!value.empty() && isSpace(value.back()))
            {
                value.pop_back();
            }
            return value;
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

        std::string NormalizeLegacyEngineVersion(std::string value)
        {
            value = Trim(std::move(value));
            if (value == "0.1" || value == "0.1.0")
            {
                return "0.0.1";
            }

            return value;
        }

        bool ParseBool(const std::string& value, bool& outBool)
        {
            const std::string lowered = ToLower(Trim(value));
            if (lowered == "true" || lowered == "1" || lowered == "yes" || lowered == "on")
            {
                outBool = true;
                return true;
            }

            if (lowered == "false" || lowered == "0" || lowered == "no" || lowered == "off")
            {
                outBool = false;
                return true;
            }

            return false;
        }

        bool TryParseUnsignedInt(const std::string& value, std::uint32_t& outValue)
        {
            const std::string trimmed = Trim(value);
            const char* begin = trimmed.data();
            const char* end = begin + trimmed.size();
            std::uint32_t parsed = 0;
            const auto [ptr, ec] = std::from_chars(begin, end, parsed);
            if (ec != std::errc() || ptr != end)
            {
                return false;
            }

            outValue = parsed;
            return true;
        }

        bool TryParseBuildProfileInsensitive(const std::string_view value, BuildProfile& outProfile)
        {
            if (TryParseBuildProfile(value, outProfile))
            {
                return true;
            }

            const std::string lowered = ToLower(std::string(value));
            if (lowered == "debug")
            {
                outProfile = BuildProfile::Debug;
                return true;
            }
            if (lowered == "development")
            {
                outProfile = BuildProfile::Development;
                return true;
            }
            if (lowered == "release")
            {
                outProfile = BuildProfile::Release;
                return true;
            }

            return false;
        }

        void ApplyConfigDefaults(Project::ProjectConfig& config, std::string_view fallbackName, std::string_view fallbackTemplate)
        {
            if (config.schemaVersion == 0)
            {
                config.schemaVersion = 3;
            }

            if (config.name.empty())
            {
                config.name = std::string(fallbackName);
            }

            if (config.engineVersion.empty())
            {
                config.engineVersion = "0.0.1";
            }

            if (config.projectVersion.empty())
            {
                config.projectVersion = "0.1.0";
            }

            if (config.templateName.empty())
            {
                config.templateName = std::string(fallbackTemplate);
            }

            if (config.startScene.empty())
            {
                config.startScene = "Assets/Scenes/Main.scene";
            }

            // Alpha builds ship OpenGL only. Keep parsing legacy "Auto" values,
            // but normalize saved configs to the backend that actually exists.
            if (config.backend == BackendPreference::Auto)
            {
                config.backend = BackendPreference::OpenGL;
            }

            const Project::BuildProfileConfig defaultDebug = Project::DefaultBuildProfileConfig(BuildProfile::Debug);
            const Project::BuildProfileConfig defaultDevelopment = Project::DefaultBuildProfileConfig(BuildProfile::Development);
            const Project::BuildProfileConfig defaultRelease = Project::DefaultBuildProfileConfig(BuildProfile::Release);

            if (config.build.debug.outputDirectory.empty())
            {
                config.build.debug.outputDirectory = defaultDebug.outputDirectory;
            }
            if (config.build.debug.defines.empty())
            {
                config.build.debug.defines = defaultDebug.defines;
            }

            if (config.build.development.outputDirectory.empty())
            {
                config.build.development.outputDirectory = defaultDevelopment.outputDirectory;
            }
            if (config.build.development.defines.empty())
            {
                config.build.development.defines = defaultDevelopment.defines;
            }

            if (config.build.release.outputDirectory.empty())
            {
                config.build.release.outputDirectory = defaultRelease.outputDirectory;
            }
            if (config.build.release.defines.empty())
            {
                config.build.release.defines = defaultRelease.defines;
            }
        }

        bool SaveBuildProfileConfig(
            std::ofstream& output,
            std::string_view profileName,
            const Project::BuildProfileConfig& config)
        {
            if (!output.good())
            {
                return false;
            }

            output << "Build." << profileName << ".EnableValidation=" << (config.enableValidation ? "true" : "false") << '\n';
            output << "Build." << profileName << ".EnableOptimizations=" << (config.enableOptimizations ? "true" : "false") << '\n';
            output << "Build." << profileName << ".EnableDebugSymbols=" << (config.enableDebugSymbols ? "true" : "false") << '\n';
            output << "Build." << profileName << ".EnableHotReload=" << (config.enableHotReload ? "true" : "false") << '\n';
            output << "Build." << profileName << ".OutputDirectory=" << config.outputDirectory << '\n';
            output << "Build." << profileName << ".Defines=" << config.defines << '\n';
            return output.good();
        }
    }

    Project::ProjectConfig Project::DefaultConfig(const std::string_view name, const std::string_view templateName)
    {
        ProjectConfig config;
        config.schemaVersion = 3;
        config.name = std::string(name);
        config.engineVersion = "0.0.1";
        config.projectVersion = "0.1.0";
        config.templateName = std::string(templateName);
        config.startScene = "Assets/Scenes/Main.scene";
        config.pipeline = RenderPipelineProfile::CoreLite;
        config.backend = BackendPreference::OpenGL;
        config.vsync = true;

        config.build.activeProfile = BuildProfile::Development;
        config.build.debug = DefaultBuildProfileConfig(BuildProfile::Debug);
        config.build.development = DefaultBuildProfileConfig(BuildProfile::Development);
        config.build.release = DefaultBuildProfileConfig(BuildProfile::Release);
        return config;
    }

    bool Project::Create(const std::filesystem::path& path, const std::string& name)
    {
        CreateProjectDesc desc;
        desc.rootPath = path;
        desc.name = name;
        desc.config = DefaultConfig(name, "Blank Project");
        return Create(desc, nullptr);
    }

    bool Project::Create(const CreateProjectDesc& createDesc, std::filesystem::path* outProjectFile)
    {
        if (createDesc.rootPath.empty() || createDesc.name.empty())
        {
            return false;
        }

        ProjectConfig config = createDesc.config;
        if (config.name.empty())
        {
            config.name = createDesc.name;
        }
        if (config.templateName.empty())
        {
            config.templateName = createDesc.templateName;
        }
        ApplyConfigDefaults(config, createDesc.name, createDesc.templateName);

        const std::filesystem::path projectRoot = createDesc.rootPath.lexically_normal();
        const std::filesystem::path projectFile = projectRoot / (config.name + ".ep");

        std::error_code ec;
        std::filesystem::create_directories(projectRoot, ec);
        if (ec)
        {
            return false;
        }

        const std::array<std::filesystem::path, 6> requiredDirectories = {
            projectRoot / "Assets",
            projectRoot / "Assets" / "Scenes",
            projectRoot / "Scripts",
            projectRoot / "Config",
            projectRoot / "Cache",
            projectRoot / "Build"
        };

        for (const std::filesystem::path& directory : requiredDirectories)
        {
            std::filesystem::create_directories(directory, ec);
            if (ec)
            {
                return false;
            }
        }

        const std::array<BuildProfile, 3> profiles = {
            BuildProfile::Debug,
            BuildProfile::Development,
            BuildProfile::Release
        };
        for (const BuildProfile profile : profiles)
        {
            const BuildProfileConfig& buildConfig = GetBuildProfileConfig(config.build, profile);
            if (buildConfig.outputDirectory.empty())
            {
                continue;
            }

            std::filesystem::path outputPath = std::filesystem::path(buildConfig.outputDirectory);
            if (!outputPath.is_absolute())
            {
                outputPath = projectRoot / outputPath;
            }

            std::filesystem::create_directories(outputPath, ec);
            if (ec)
            {
                return false;
            }
        }

        if (!SaveConfig(projectFile, config))
        {
            return false;
        }

        s_ProjectRoot = projectRoot;
        s_ProjectFile = projectFile;
        s_Config = config;
        s_Loaded = true;

        if (outProjectFile != nullptr)
        {
            *outProjectFile = projectFile;
        }

        return true;
    }

    bool Project::Load(const std::filesystem::path& projectFile)
    {
        if (projectFile.empty() || !std::filesystem::exists(projectFile))
        {
            return false;
        }

        ProjectConfig config;
        if (!LoadConfig(projectFile, config))
        {
            return false;
        }

        s_ProjectRoot = projectFile.parent_path().lexically_normal();
        s_ProjectFile = projectFile.lexically_normal();
        s_Config = config;
        s_Loaded = true;
        return true;
    }

    bool Project::PeekConfig(const std::filesystem::path& projectFile, ProjectConfig& outConfig)
    {
        if (projectFile.empty() || !std::filesystem::exists(projectFile))
        {
            return false;
        }

        return LoadConfig(projectFile, outConfig);
    }

    bool Project::SerializeConfig(const std::filesystem::path& file, const ProjectConfig& config)
    {
        return SaveConfig(file, config);
    }

    bool Project::DeserializeConfig(const std::filesystem::path& file, ProjectConfig& config)
    {
        return LoadConfig(file, config);
    }

    bool Project::UpdateSettings(const ProjectConfig& config, const bool saveImmediately)
    {
        if (!s_Loaded)
        {
            return false;
        }

        ProjectConfig updated = config;
        ApplyConfigDefaults(updated, s_Config.name, s_Config.templateName);
        s_Config = std::move(updated);
        return !saveImmediately || SaveLoadedConfig();
    }

    void Project::SetRenderingConfig(const RenderPipelineProfile pipeline, const BackendPreference backend, const bool vsync)
    {
        s_Config.pipeline = pipeline;
        s_Config.backend = backend;
        s_Config.vsync = vsync;
    }

    bool Project::SetActiveBuildProfile(const BuildProfile profile, const bool saveImmediately)
    {
        if (!s_Loaded)
        {
            return false;
        }

        s_Config.build.activeProfile = profile;
        return !saveImmediately || SaveLoadedConfig();
    }

    bool Project::SaveLoadedConfig()
    {
        if (!s_Loaded || s_ProjectRoot.empty() || s_Config.name.empty())
        {
            return false;
        }

        const std::filesystem::path expectedFile = (s_ProjectRoot / (s_Config.name + ".ep")).lexically_normal();
        if (s_ProjectFile.empty())
        {
            s_ProjectFile = expectedFile;
        }
        else if (s_ProjectFile.lexically_normal() != expectedFile)
        {
            std::error_code ec;
            if (std::filesystem::exists(s_ProjectFile))
            {
                std::filesystem::rename(s_ProjectFile, expectedFile, ec);
                if (ec)
                {
                    std::filesystem::copy_file(
                        s_ProjectFile,
                        expectedFile,
                        std::filesystem::copy_options::overwrite_existing,
                        ec);
                    if (!ec)
                    {
                        std::filesystem::remove(s_ProjectFile, ec);
                    }
                }
            }

            s_ProjectFile = expectedFile;
        }

        return SaveConfig(s_ProjectFile, s_Config);
    }

    void Project::Unload()
    {
        s_ProjectRoot.clear();
        s_ProjectFile.clear();
        s_Config = {};
        s_Loaded = false;
    }

    bool Project::IsLoaded()
    {
        return s_Loaded;
    }

    const std::filesystem::path& Project::GetProjectRoot()
    {
        return s_ProjectRoot;
    }

    const std::filesystem::path& Project::GetProjectFilePath()
    {
        return s_ProjectFile;
    }

    std::filesystem::path Project::GetAssetsPath()
    {
        return s_ProjectRoot / "Assets";
    }

    std::filesystem::path Project::GetScenesPath()
    {
        return GetAssetsPath() / "Scenes";
    }

    std::filesystem::path Project::GetScriptsPath()
    {
        return s_ProjectRoot / "Scripts";
    }

    std::filesystem::path Project::GetConfigPath()
    {
        return s_ProjectRoot / "Config";
    }

    std::filesystem::path Project::GetCachePath()
    {
        return s_ProjectRoot / "Cache";
    }

    std::filesystem::path Project::GetBuildPath()
    {
        return s_ProjectRoot / "Build";
    }

    std::filesystem::path Project::GetBuildPath(const BuildProfile profile)
    {
        const BuildProfileConfig& config = GetBuildProfileConfig(s_Config.build, profile);
        if (config.outputDirectory.empty())
        {
            return GetBuildPath();
        }

        std::filesystem::path outputPath(config.outputDirectory);
        if (!outputPath.is_absolute())
        {
            outputPath = s_ProjectRoot / outputPath;
        }
        return outputPath;
    }

    const Project::ProjectConfig& Project::GetConfig()
    {
        return s_Config;
    }

    BuildProfile Project::GetActiveBuildProfile()
    {
        return s_Config.build.activeProfile;
    }

    const Project::BuildSettings& Project::GetBuildSettings()
    {
        return s_Config.build;
    }

    bool Project::SaveConfig(const std::filesystem::path& file, const ProjectConfig& config)
    {
        std::ofstream output(file, std::ios::trunc);
        if (!output.is_open())
        {
            return false;
        }

        output << "SchemaVersion=" << config.schemaVersion << '\n';
        output << "Name=" << config.name << '\n';
        output << "EngineVersion=" << config.engineVersion << '\n';
        output << "ProjectVersion=" << config.projectVersion << '\n';
        output << "Template=" << config.templateName << '\n';
        output << "StartScene=" << config.startScene << '\n';
        output << "RenderPipeline=" << ToString(config.pipeline) << '\n';
        output << "RenderBackend=" << ToString(config.backend) << '\n';
        output << "VSync=" << (config.vsync ? "true" : "false") << '\n';
        for (const auto& plugin : config.plugins)
        {
            if (plugin.id.empty())
            {
                continue;
            }

            output << "Plugin." << plugin.id << ".Enabled=" << (plugin.enabled ? "true" : "false") << '\n';
        }
        output << "Build.ActiveProfile=" << ToString(config.build.activeProfile) << '\n';

        if (!SaveBuildProfileConfig(output, "Debug", config.build.debug))
        {
            return false;
        }
        if (!SaveBuildProfileConfig(output, "Development", config.build.development))
        {
            return false;
        }
        if (!SaveBuildProfileConfig(output, "Release", config.build.release))
        {
            return false;
        }

        return output.good();
    }

    bool Project::LoadConfig(const std::filesystem::path& file, ProjectConfig& config)
    {
        std::ifstream input(file);
        if (!input.is_open())
        {
            return false;
        }

        ProjectConfig parsedConfig = DefaultConfig(file.stem().string(), "Blank Project");
        std::string line;

        while (std::getline(input, line))
        {
            const std::size_t separator = line.find('=');
            if (separator == std::string::npos)
            {
                continue;
            }

            const std::string key = Trim(line.substr(0, separator));
            const std::string value = Trim(line.substr(separator + 1));

            if (key == "SchemaVersion")
            {
                TryParseUnsignedInt(value, parsedConfig.schemaVersion);
            }
            else if (key == "Name")
            {
                parsedConfig.name = value;
            }
            else if (key == "EngineVersion")
            {
                parsedConfig.engineVersion = NormalizeLegacyEngineVersion(value);
            }
            else if (key == "ProjectVersion")
            {
                parsedConfig.projectVersion = value;
            }
            else if (key == "Template")
            {
                parsedConfig.templateName = value;
            }
            else if (key == "StartScene")
            {
                parsedConfig.startScene = value;
            }
            else if (key == "RenderPipeline")
            {
                TryParseRenderPipelineProfile(value, parsedConfig.pipeline);
            }
            else if (key == "RenderBackend")
            {
                TryParseBackendPreference(value, parsedConfig.backend);
            }
            else if (key == "VSync")
            {
                ParseBool(value, parsedConfig.vsync);
            }
            else if (key.rfind("Plugin.", 0) == 0)
            {
                constexpr std::string_view kPrefix = "Plugin.";
                constexpr std::string_view kEnabledSuffix = ".Enabled";
                if (key.size() > kPrefix.size() + kEnabledSuffix.size() &&
                    key.compare(key.size() - kEnabledSuffix.size(), kEnabledSuffix.size(), kEnabledSuffix) == 0)
                {
                    const std::string pluginId =
                        key.substr(kPrefix.size(), key.size() - kPrefix.size() - kEnabledSuffix.size());
                    if (!pluginId.empty())
                    {
                        bool enabled = false;
                        if (ParseBool(value, enabled))
                        {
                            auto existingIt = std::find_if(
                                parsedConfig.plugins.begin(),
                                parsedConfig.plugins.end(),
                                [&pluginId](const Project::ProjectConfig::PluginConfig& plugin)
                                {
                                    return plugin.id == pluginId;
                                });
                            if (existingIt == parsedConfig.plugins.end())
                            {
                                parsedConfig.plugins.push_back({ pluginId, enabled });
                            }
                            else
                            {
                                existingIt->enabled = enabled;
                            }
                        }
                    }
                }
            }
            else if (key == "BuildProfile" || key == "Build.ActiveProfile")
            {
                TryParseBuildProfileInsensitive(value, parsedConfig.build.activeProfile);
            }
            else if (key.rfind("Build.", 0) == 0)
            {
                const std::string remainder = key.substr(6);
                const std::size_t fieldSeparator = remainder.find('.');
                if (fieldSeparator == std::string::npos)
                {
                    continue;
                }

                const std::string profileName = remainder.substr(0, fieldSeparator);
                const std::string fieldName = remainder.substr(fieldSeparator + 1);

                BuildProfile profile = BuildProfile::Development;
                if (!TryParseBuildProfileInsensitive(profileName, profile))
                {
                    continue;
                }

                BuildProfileConfig& profileConfig = GetBuildProfileConfig(parsedConfig.build, profile);

                if (fieldName == "EnableValidation")
                {
                    ParseBool(value, profileConfig.enableValidation);
                }
                else if (fieldName == "EnableOptimizations")
                {
                    ParseBool(value, profileConfig.enableOptimizations);
                }
                else if (fieldName == "EnableDebugSymbols")
                {
                    ParseBool(value, profileConfig.enableDebugSymbols);
                }
                else if (fieldName == "EnableHotReload")
                {
                    ParseBool(value, profileConfig.enableHotReload);
                }
                else if (fieldName == "OutputDirectory")
                {
                    profileConfig.outputDirectory = value;
                }
                else if (fieldName == "Defines")
                {
                    profileConfig.defines = value;
                }
            }
        }

        ApplyConfigDefaults(parsedConfig, file.stem().string(), "Blank Project");
        std::sort(
            parsedConfig.plugins.begin(),
            parsedConfig.plugins.end(),
            [](const Project::ProjectConfig::PluginConfig& lhs, const Project::ProjectConfig::PluginConfig& rhs)
            {
                return lhs.id < rhs.id;
            });
        config = std::move(parsedConfig);
        return true;
    }

    Project::BuildProfileConfig Project::DefaultBuildProfileConfig(const BuildProfile profile)
    {
        BuildProfileConfig config;
        switch (profile)
        {
        case BuildProfile::Debug:
            config.enableValidation = true;
            config.enableOptimizations = false;
            config.enableDebugSymbols = true;
            config.enableHotReload = true;
            config.outputDirectory = "Build/Debug";
            config.defines = "DEBUG;LUMA_DEBUG";
            break;
        case BuildProfile::Development:
            config.enableValidation = true;
            config.enableOptimizations = true;
            config.enableDebugSymbols = true;
            config.enableHotReload = true;
            config.outputDirectory = "Build/Development";
            config.defines = "DEVELOPMENT;LUMA_DEV";
            break;
        case BuildProfile::Release:
            config.enableValidation = false;
            config.enableOptimizations = true;
            config.enableDebugSymbols = false;
            config.enableHotReload = false;
            config.outputDirectory = "Build/Release";
            config.defines = "NDEBUG;LUMA_RELEASE";
            break;
        default:
            break;
        }
        return config;
    }

    Project::BuildProfileConfig& Project::GetBuildProfileConfig(BuildSettings& buildSettings, const BuildProfile profile)
    {
        switch (profile)
        {
        case BuildProfile::Debug:
            return buildSettings.debug;
        case BuildProfile::Development:
            return buildSettings.development;
        case BuildProfile::Release:
            return buildSettings.release;
        default:
            return buildSettings.development;
        }
    }

    const Project::BuildProfileConfig& Project::GetBuildProfileConfig(
        const BuildSettings& buildSettings,
        const BuildProfile profile)
    {
        switch (profile)
        {
        case BuildProfile::Debug:
            return buildSettings.debug;
        case BuildProfile::Development:
            return buildSettings.development;
        case BuildProfile::Release:
            return buildSettings.release;
        default:
            return buildSettings.development;
        }
    }
}
