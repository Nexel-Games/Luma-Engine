#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Luma
{
    enum class RenderPipelineProfile
    {
        CoreLite,
        CoreX
    };

    enum class BackendPreference
    {
        Auto,
        OpenGL
    };

    enum class BuildProfile : std::uint8_t
    {
        Debug,
        Development,
        Release
    };

    inline std::string_view ToString(const RenderPipelineProfile pipeline)
    {
        switch (pipeline)
        {
        case RenderPipelineProfile::CoreLite:
            return "CoreLite";
        case RenderPipelineProfile::CoreX:
            return "CoreX";
        default:
            return "CoreLite";
        }
    }

    inline std::string_view ToString(const BackendPreference backend)
    {
        switch (backend)
        {
        case BackendPreference::Auto:
            return "Auto";
        case BackendPreference::OpenGL:
            return "OpenGL";
        default:
            return "Auto";
        }
    }

    inline bool TryParseRenderPipelineProfile(const std::string_view value, RenderPipelineProfile& outPipeline)
    {
        if (value == "CoreLite")
        {
            outPipeline = RenderPipelineProfile::CoreLite;
            return true;
        }

        if (value == "CoreX")
        {
            outPipeline = RenderPipelineProfile::CoreX;
            return true;
        }

        return false;
    }

    inline bool TryParseBackendPreference(const std::string_view value, BackendPreference& outBackend)
    {
        if (value == "Auto")
        {
            outBackend = BackendPreference::Auto;
            return true;
        }

        if (value == "OpenGL")
        {
            outBackend = BackendPreference::OpenGL;
            return true;
        }

        return false;
    }

    inline std::string_view ToString(const BuildProfile profile)
    {
        switch (profile)
        {
        case BuildProfile::Debug:
            return "Debug";
        case BuildProfile::Development:
            return "Development";
        case BuildProfile::Release:
            return "Release";
        default:
            return "Development";
        }
    }

    inline bool TryParseBuildProfile(const std::string_view value, BuildProfile& outProfile)
    {
        if (value == "Debug")
        {
            outProfile = BuildProfile::Debug;
            return true;
        }
        if (value == "Development")
        {
            outProfile = BuildProfile::Development;
            return true;
        }
        if (value == "Release")
        {
            outProfile = BuildProfile::Release;
            return true;
        }
        return false;
    }

    class Project
    {
    public:
        struct BuildProfileConfig
        {
            bool enableValidation = true;
            bool enableOptimizations = false;
            bool enableDebugSymbols = true;
            bool enableHotReload = true;
            std::string outputDirectory;
            std::string defines;
        };

        struct BuildSettings
        {
            BuildProfile activeProfile = BuildProfile::Development;
            BuildProfileConfig debug {};
            BuildProfileConfig development {};
            BuildProfileConfig release {};
        };

        struct ProjectConfig
        {
            struct PluginConfig
            {
                std::string id;
                bool enabled = false;
            };

            std::uint32_t schemaVersion = 5;
            std::string name;
            std::string engineVersion = "0.0.1";
            std::string projectVersion = "0.1.0";
            std::string templateName = "Blank Project";
            std::string startScene = "Assets/Scenes/Main.scene";
            RenderPipelineProfile pipeline = RenderPipelineProfile::CoreLite;
            BackendPreference backend = BackendPreference::OpenGL;
            bool vsync = true;
            std::vector<std::string> tags;
            std::vector<std::string> layers;
            std::vector<PluginConfig> plugins;
            BuildSettings build {};
        };

        struct CreateProjectDesc
        {
            std::filesystem::path rootPath;
            std::string name;
            std::string templateName = "Blank Project";
            ProjectConfig config {};
        };

    public:
        static ProjectConfig DefaultConfig(std::string_view name, std::string_view templateName = "Blank Project");

        static bool Create(const std::filesystem::path& path, const std::string& name);
        static bool Create(const CreateProjectDesc& createDesc, std::filesystem::path* outProjectFile = nullptr);
        static bool Load(const std::filesystem::path& projectFile);
        static bool PeekConfig(const std::filesystem::path& projectFile, ProjectConfig& outConfig);
        static bool SerializeConfig(const std::filesystem::path& file, const ProjectConfig& config);
        static bool DeserializeConfig(const std::filesystem::path& file, ProjectConfig& config);
        static bool UpdateSettings(const ProjectConfig& config, bool saveImmediately = true);
        static void SetRenderingConfig(RenderPipelineProfile pipeline, BackendPreference backend, bool vsync);
        static bool SetActiveBuildProfile(BuildProfile profile, bool saveImmediately = false);
        static bool SaveLoadedConfig();
        static void Unload();
        static bool IsLoaded();

        static const std::filesystem::path& GetProjectRoot();
        static const std::filesystem::path& GetProjectFilePath();
        static std::filesystem::path GetAssetsPath();
        static std::filesystem::path GetScenesPath();
        static std::filesystem::path GetScriptsPath();
        static std::filesystem::path GetConfigPath();
        static std::filesystem::path GetCachePath();
        static std::filesystem::path GetBuildPath();
        static std::filesystem::path GetBuildPath(BuildProfile profile);

        static const ProjectConfig& GetConfig();
        static BuildProfile GetActiveBuildProfile();
        static const BuildSettings& GetBuildSettings();
        static BuildProfileConfig DefaultBuildProfileConfig(BuildProfile profile);

    private:
        static bool SaveConfig(const std::filesystem::path& file, const ProjectConfig& config);
        static bool LoadConfig(const std::filesystem::path& file, ProjectConfig& config);
        static BuildProfileConfig& GetBuildProfileConfig(BuildSettings& buildSettings, BuildProfile profile);
        static const BuildProfileConfig& GetBuildProfileConfig(const BuildSettings& buildSettings, BuildProfile profile);

        static std::filesystem::path s_ProjectRoot;
        static std::filesystem::path s_ProjectFile;
        static ProjectConfig s_Config;
        static bool s_Loaded;
    };
}
