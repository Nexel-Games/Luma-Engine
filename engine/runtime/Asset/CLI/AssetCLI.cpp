#include "Luma/Asset/CLI/AssetCLI.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "Luma/Asset/Cook/AssetCooker.h"
#include "Luma/Asset/Import/BuiltInImporters.h"
#include "Luma/Asset/Import/ImportPipeline.h"
#include "Luma/Asset/Package/PackageManager.h"
#include "Luma/Core/App/Project.h"

namespace Luma::Assets
{
    namespace
    {
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

        std::optional<std::string> ConsumeOptionValue(const int argc, char** argv, int& index, std::string_view option)
        {
            const std::string current = argv[index];
            const std::string prefix = std::string(option) + "=";
            if (current.rfind(prefix, 0) == 0)
            {
                return current.substr(prefix.size());
            }
            if (current == option && (index + 1) < argc)
            {
                ++index;
                return std::string(argv[index]);
            }
            return std::nullopt;
        }

        std::filesystem::path ResolveProjectFilePath(const std::filesystem::path& projectArg)
        {
            std::filesystem::path normalized = projectArg;
            if (normalized.empty())
            {
                return {};
            }

            if (!normalized.is_absolute())
            {
                normalized = std::filesystem::absolute(normalized);
            }

            if (normalized.extension() == ".ep")
            {
                return normalized.lexically_normal();
            }

            if (std::filesystem::is_directory(normalized))
            {
                return (normalized / (normalized.filename().string() + ".ep")).lexically_normal();
            }

            return normalized.lexically_normal();
        }

        bool LoadProjectContext(const std::optional<std::filesystem::path>& projectArg, std::string& outError)
        {
            if (!projectArg.has_value())
            {
                outError = "Missing required --project argument.";
                return false;
            }

            const std::filesystem::path projectFile = ResolveProjectFilePath(projectArg.value());
            if (!std::filesystem::exists(projectFile))
            {
                outError = "Project file not found: " + projectFile.string();
                return false;
            }

            if (!Project::Load(projectFile))
            {
                outError = "Failed to load project config: " + projectFile.string();
                return false;
            }

            return true;
        }

        int RunImportCommand(const int argc, char** argv)
        {
            std::optional<std::filesystem::path> projectArg;
            std::optional<std::filesystem::path> targetDirectory;
            std::string preset;
            ImportSettingsMap settings;
            std::filesystem::path sourcePath;

            for (int i = 2; i < argc; ++i)
            {
                if (auto value = ConsumeOptionValue(argc, argv, i, "--project"))
                {
                    projectArg = std::filesystem::path(*value);
                    continue;
                }
                if (auto value = ConsumeOptionValue(argc, argv, i, "--target"))
                {
                    targetDirectory = std::filesystem::path(*value);
                    continue;
                }
                if (auto value = ConsumeOptionValue(argc, argv, i, "--preset"))
                {
                    preset = *value;
                    continue;
                }
                if (auto value = ConsumeOptionValue(argc, argv, i, "--set"))
                {
                    const std::string kv = *value;
                    const std::size_t separator = kv.find('=');
                    if (separator != std::string::npos)
                    {
                        settings[kv.substr(0, separator)] = kv.substr(separator + 1);
                    }
                    continue;
                }

                const std::string arg = argv[i];
                if (!arg.empty() && arg[0] != '-')
                {
                    sourcePath = arg;
                }
            }

            if (sourcePath.empty())
            {
                std::cerr << "Import requires a source path.\n";
                return 1;
            }

            std::string projectError;
            if (!LoadProjectContext(projectArg, projectError))
            {
                std::cerr << projectError << '\n';
                return 1;
            }

            AssetRegistry registry;
            ImporterRegistry importerRegistry;
            RegisterBuiltInImporters(importerRegistry);

            ImportPipeline pipeline(registry, importerRegistry);
            std::string initError;
            if (!pipeline.Initialize(Project::GetProjectRoot(), initError))
            {
                std::cerr << initError << '\n';
                return 1;
            }

            ImportRequest request;
            request.sourcePaths = { sourcePath };
            request.targetDirectory = targetDirectory.value_or(std::filesystem::path("Imported"));
            request.importPreset = preset;
            request.headless = true;
            request.settingsOverrides = std::move(settings);

            const ImportResult result = pipeline.Import(request);
            std::cout << result.message << '\n';
            return result.success ? 0 : 1;
        }

        int RunReimportCommand(const int argc, char** argv)
        {
            std::optional<std::filesystem::path> projectArg;
            bool changedOnly = false;

            for (int i = 2; i < argc; ++i)
            {
                if (auto value = ConsumeOptionValue(argc, argv, i, "--project"))
                {
                    projectArg = std::filesystem::path(*value);
                    continue;
                }

                const std::string arg = ToLower(argv[i]);
                if (arg == "--changed")
                {
                    changedOnly = true;
                }
            }

            std::string projectError;
            if (!LoadProjectContext(projectArg, projectError))
            {
                std::cerr << projectError << '\n';
                return 1;
            }

            AssetRegistry registry;
            ImporterRegistry importerRegistry;
            RegisterBuiltInImporters(importerRegistry);

            ImportPipeline pipeline(registry, importerRegistry);
            std::string initError;
            if (!pipeline.Initialize(Project::GetProjectRoot(), initError))
            {
                std::cerr << initError << '\n';
                return 1;
            }

            if (!changedOnly)
            {
                std::cout << "Only --changed reimport is supported in v1.\n";
                return 1;
            }

            const std::vector<ImportResult> results = pipeline.ReimportChanged();
            int failures = 0;
            for (const ImportResult& result : results)
            {
                if (!result.success)
                {
                    ++failures;
                }
                std::cout << result.message << '\n';
            }

            std::cout << "Reimported " << results.size() << " asset(s).\n";
            return failures == 0 ? 0 : 1;
        }

        int RunCookCommand(const int argc, char** argv)
        {
            std::optional<std::filesystem::path> projectArg;
            std::string platformName = "windows";

            for (int i = 2; i < argc; ++i)
            {
                if (auto value = ConsumeOptionValue(argc, argv, i, "--project"))
                {
                    projectArg = std::filesystem::path(*value);
                    continue;
                }
                if (auto value = ConsumeOptionValue(argc, argv, i, "--platform"))
                {
                    platformName = ToLower(*value);
                    continue;
                }
            }

            std::string projectError;
            if (!LoadProjectContext(projectArg, projectError))
            {
                std::cerr << projectError << '\n';
                return 1;
            }

            AssetCooker cooker;
            std::string initError;
            if (!cooker.Initialize(Project::GetProjectRoot(), initError))
            {
                std::cerr << initError << '\n';
                return 1;
            }

            std::string cookReport;
            if (!cooker.CookAll(platformName, cookReport))
            {
                std::cerr << cookReport << '\n';
                return 1;
            }

            std::cout << cookReport << '\n';
            return 0;
        }

        int RunPackagesCommand(const int argc, char** argv)
        {
            if (argc < 3)
            {
                std::cerr << "Usage:\n"
                    << "  luma packages search [query] [--category <name>] --project <project.ep>\n"
                    << "  luma packages install <package-id|archive-path> [--version <semver>] [--samples] [--no-hotload] --project <project.ep>\n"
                    << "  luma packages update <package-id> [--version <semver>] --project <project.ep>\n"
                    << "  luma packages remove <package-id> --project <project.ep>\n"
                    << "  luma packages verify [package-id] --project <project.ep>\n";
                return 1;
            }

            const std::string subcommand = ToLower(argv[2]);
            std::optional<std::filesystem::path> projectArg;
            std::optional<std::string> positionalArg;
            std::string version;
            std::string category;
            bool importSamples = false;
            bool attemptHotLoad = true;

            for (int i = 3; i < argc; ++i)
            {
                if (auto value = ConsumeOptionValue(argc, argv, i, "--project"))
                {
                    projectArg = std::filesystem::path(*value);
                    continue;
                }
                if (auto value = ConsumeOptionValue(argc, argv, i, "--version"))
                {
                    version = *value;
                    continue;
                }
                if (auto value = ConsumeOptionValue(argc, argv, i, "--category"))
                {
                    category = *value;
                    continue;
                }

                const std::string arg = argv[i];
                const std::string lowerArg = ToLower(arg);
                if (lowerArg == "--samples")
                {
                    importSamples = true;
                    continue;
                }
                if (lowerArg == "--no-hotload")
                {
                    attemptHotLoad = false;
                    continue;
                }
                if (!arg.empty() && arg[0] != '-')
                {
                    if (!positionalArg.has_value())
                    {
                        positionalArg = arg;
                    }
                }
            }

            std::string projectError;
            if (!LoadProjectContext(projectArg, projectError))
            {
                std::cerr << projectError << '\n';
                return 1;
            }

            PackageManager packageManager;
            std::string initError;
            if (!packageManager.Initialize(Project::GetProjectRoot(), initError))
            {
                std::cerr << initError << '\n';
                return 1;
            }
            packageManager.SetProgressCallback(
                [](const PackageProgressEvent& event)
                {
                    if (event.message.empty())
                    {
                        return;
                    }

                    std::cout << "[packages]";
                    if (!event.packageId.empty())
                    {
                        std::cout << ' ' << event.packageId;
                    }
                    if (event.progress01 > 0.0f)
                    {
                        std::cout << " (" << static_cast<int>(event.progress01 * 100.0f) << "%)";
                    }
                    std::cout << ' ' << event.message << '\n';
                });

            auto printOperationResult =
                [](const PackageOperationResult& result, std::ostream& stream)
                {
                    stream << result.message << '\n';
                    for (const std::string& diagnostic : result.diagnostics)
                    {
                        stream << "  - " << diagnostic << '\n';
                    }
                    if (!result.affectedPackages.empty())
                    {
                        stream << "Affected packages:\n";
                        for (const PackageRef& package : result.affectedPackages)
                        {
                            stream << "  * " << package.id;
                            if (!package.version.empty())
                            {
                                stream << '@' << package.version;
                            }
                            stream << '\n';
                        }
                    }
                    if (result.restartRequired)
                    {
                        stream << "Restart required to finish module loading.\n";
                    }
                };

            if (subcommand == "search")
            {
                std::string refreshError;
                if (!packageManager.RefreshRegistrySources(refreshError))
                {
                    std::cerr << refreshError << '\n';
                    return 1;
                }

                PackageSearchQuery query;
                query.text = positionalArg.value_or("");
                query.category = category;
                const PackageSearchResult result = packageManager.Search(query);
                if (!result.success)
                {
                    std::cerr << result.message << '\n';
                    return 1;
                }

                std::cout << result.message << '\n';
                for (const PackageCatalogEntry& package : result.packages)
                {
                    std::cout << package.id;
                    if (!package.latest.empty())
                    {
                        std::cout << " @" << package.latest;
                    }
                    if (!package.category.empty())
                    {
                        std::cout << " [" << package.category << "]";
                    }
                    if (!package.versions.empty())
                    {
                        std::cout << " versions=" << package.versions.size();
                    }
                    std::cout << '\n';
                }
                return 0;
            }

            if (subcommand == "install")
            {
                if (!positionalArg.has_value())
                {
                    std::cerr << "packages install requires a package id or archive path.\n";
                    return 1;
                }

                const std::filesystem::path packageSource = positionalArg.value();
                if (std::filesystem::exists(packageSource))
                {
                    std::string installMessage;
                    if (!packageManager.InstallPackage(packageSource, installMessage))
                    {
                        std::cerr << installMessage << '\n';
                        return 1;
                    }

                    std::cout << installMessage << '\n';
                    return 0;
                }

                std::string refreshError;
                if (!packageManager.RefreshRegistrySources(refreshError))
                {
                    std::cerr << refreshError << '\n';
                    return 1;
                }

                PackageInstallRequest request;
                request.id = positionalArg.value();
                request.version = version;
                request.importSamplesToAssets = importSamples;
                request.attemptHotLoad = attemptHotLoad;

                const PackageOperationResult result = packageManager.Install(request);
                printOperationResult(result, result.success ? std::cout : std::cerr);
                return result.success ? 0 : 1;
            }

            if (subcommand == "update")
            {
                if (!positionalArg.has_value())
                {
                    std::cerr << "packages update requires a package id.\n";
                    return 1;
                }

                std::string refreshError;
                if (!packageManager.RefreshRegistrySources(refreshError))
                {
                    std::cerr << refreshError << '\n';
                    return 1;
                }

                const PackageOperationResult result = packageManager.Update(positionalArg.value(), version);
                printOperationResult(result, result.success ? std::cout : std::cerr);
                return result.success ? 0 : 1;
            }

            if (subcommand == "remove")
            {
                if (!positionalArg.has_value())
                {
                    std::cerr << "packages remove requires a package id.\n";
                    return 1;
                }

                const PackageOperationResult result = packageManager.Remove(positionalArg.value());
                printOperationResult(result, result.success ? std::cout : std::cerr);
                return result.success ? 0 : 1;
            }

            if (subcommand == "verify")
            {
                const PackageOperationResult result = packageManager.Verify(positionalArg.value_or(""));
                printOperationResult(result, result.success ? std::cout : std::cerr);
                return result.success ? 0 : 1;
            }

            std::cerr << "Unsupported packages command: " << argv[2] << '\n';
            return 1;
        }
    }

    std::optional<int> TryRunAssetCli(const int argc, char** argv)
    {
        if (argc <= 1 || argv == nullptr || argv[1] == nullptr)
        {
            return std::nullopt;
        }

        const std::string command = ToLower(argv[1]);
        if (command == "import")
        {
            return RunImportCommand(argc, argv);
        }
        if (command == "reimport")
        {
            return RunReimportCommand(argc, argv);
        }
        if (command == "cook")
        {
            return RunCookCommand(argc, argv);
        }
        if (command == "packages")
        {
            return RunPackagesCommand(argc, argv);
        }

        return std::nullopt;
    }
}
