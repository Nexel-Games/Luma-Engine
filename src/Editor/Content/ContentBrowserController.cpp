#include "Luma/Editor/Content/ContentBrowserController.h"

#include <algorithm>
#include <fstream>
#include <system_error>

#include <imgui.h>

#include "Luma/Input/Input.h"

namespace Luma::Editor
{
    namespace
    {
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

        std::filesystem::path NormalizePathForComparison(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return {};
            }

            std::error_code ec;
            std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
            if (ec)
            {
                normalized = path.lexically_normal();
            }

            return normalized.lexically_normal();
        }

        bool PathIsWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root)
        {
            if (path.empty() || root.empty())
            {
                return false;
            }

            std::string normalizedPath = NormalizePathForComparison(path).generic_string();
            std::string normalizedRoot = NormalizePathForComparison(root).generic_string();
#if defined(_WIN32)
            normalizedPath = ToLowerString(normalizedPath);
            normalizedRoot = ToLowerString(normalizedRoot);
#endif

            if (normalizedPath.size() < normalizedRoot.size())
            {
                return false;
            }

            if (normalizedPath.compare(0, normalizedRoot.size(), normalizedRoot) != 0)
            {
                return false;
            }

            if (normalizedPath.size() == normalizedRoot.size())
            {
                return true;
            }

            const char separator = normalizedPath[normalizedRoot.size()];
            return separator == '/' || separator == '\\';
        }

        std::string TrimString(std::string value)
        {
            auto isWhitespace = [](const unsigned char c)
            {
                return std::isspace(c) != 0;
            };

            value.erase(
                value.begin(),
                std::find_if(
                    value.begin(),
                    value.end(),
                    [&](const unsigned char c)
                    {
                        return !isWhitespace(c);
                    }));

            value.erase(
                std::find_if(
                    value.rbegin(),
                    value.rend(),
                    [&](const unsigned char c)
                    {
                        return !isWhitespace(c);
                    }).base(),
                value.end());

            return value;
        }

        std::string SanitizeScriptFileStem(std::string value)
        {
            value = TrimString(std::move(value));
            if (value.empty())
            {
                return "NewScript";
            }

            for (char& character : value)
            {
                const unsigned char code = static_cast<unsigned char>(character);
                if (code < 32 ||
                    character == '<' ||
                    character == '>' ||
                    character == ':' ||
                    character == '"' ||
                    character == '/' ||
                    character == '\\' ||
                    character == '|' ||
                    character == '?' ||
                    character == '*')
                {
                    character = '_';
                }
            }

            while (!value.empty() && (value.back() == ' ' || value.back() == '.'))
            {
                value.pop_back();
            }

            if (value.empty())
            {
                return "NewScript";
            }

            return value;
        }

        std::string BuildLuaIdentifier(std::string value)
        {
            value = TrimString(std::move(value));

            std::string identifier;
            identifier.reserve(value.size());
            for (const char character : value)
            {
                const unsigned char code = static_cast<unsigned char>(character);
                if (std::isalnum(code) != 0 || character == '_')
                {
                    identifier.push_back(character);
                }
                else if (character == ' ' || character == '-')
                {
                    identifier.push_back('_');
                }
            }

            if (identifier.empty())
            {
                identifier = "NewScript";
            }

            const unsigned char first = static_cast<unsigned char>(identifier.front());
            if (std::isdigit(first) != 0)
            {
                identifier.insert(identifier.begin(), '_');
            }

            return identifier;
        }

        std::string StripLuaExtension(std::string value)
        {
            constexpr std::string_view luaExtension = ".lua";
            if (value.size() >= luaExtension.size())
            {
                std::string lowered = ToLowerString(value);
                if (lowered.compare(lowered.size() - luaExtension.size(), luaExtension.size(), luaExtension) == 0)
                {
                    value.erase(value.size() - luaExtension.size());
                }
            }

            return value;
        }

        std::filesystem::path BuildUniqueScriptPath(
            const std::filesystem::path& directory,
            const std::string& requestedStem)
        {
            std::error_code ec;
            for (int suffixIndex = 0; suffixIndex < 1000; ++suffixIndex)
            {
                const std::string suffix = suffixIndex == 0 ? "" : " " + std::to_string(suffixIndex);
                const std::filesystem::path candidate = directory / (requestedStem + suffix + ".lua");
                if (!std::filesystem::exists(candidate, ec))
                {
                    return candidate;
                }
            }

            return {};
        }
    }

    void ContentBrowserController::Draw(bool* open, ContentBrowserControllerContext& context)
    {
        if (open == nullptr ||
            context.contentRoot == nullptr ||
            context.currentDirectory == nullptr ||
            context.selectedEntry == nullptr ||
            context.roots == nullptr ||
            context.activeRootIndex == nullptr ||
            context.status == nullptr ||
            context.cache == nullptr ||
            context.pendingImports == nullptr ||
            context.contentImportCursor == nullptr ||
            context.importedCount == nullptr ||
            context.skippedCount == nullptr ||
            context.failedCount == nullptr ||
            context.firstError == nullptr ||
            context.contentImportTask == nullptr ||
            context.contentImportActive == nullptr ||
            !context.resolveInitialContentRoot ||
            !context.refreshRoots ||
            !context.refreshEntries ||
            !context.invalidateFolderTreeCache ||
            !context.rebuildFolderTreeCache ||
            !context.requestLoadScene ||
            !context.openAsset)
        {
            return;
        }

        if (!ImGui::Begin("Content Browser", open))
        {
            ImGui::End();
            return;
        }

        auto& contentRoot = *context.contentRoot;
        auto& currentDirectory = *context.currentDirectory;
        auto& selectedEntry = *context.selectedEntry;
        auto& roots = *context.roots;
        auto& activeRootIndex = *context.activeRootIndex;
        auto& status = *context.status;
        ContentBrowserCache& cache = *context.cache;
        auto& pendingImports = *context.pendingImports;
        auto& contentImportCursor = *context.contentImportCursor;
        auto& importedCount = *context.importedCount;
        auto& skippedCount = *context.skippedCount;
        auto& failedCount = *context.failedCount;
        auto& firstError = *context.firstError;
        auto& contentImportTask = *context.contentImportTask;
        auto& contentImportActive = *context.contentImportActive;

        if (contentRoot.empty())
        {
            contentRoot = context.resolveInitialContentRoot();
            context.refreshRoots();
        }

        if (roots.empty())
        {
            context.refreshRoots();
        }

        const auto getActiveRoot = [&]() -> ContentBrowserRootState*
        {
            if (activeRootIndex < 0 || activeRootIndex >= static_cast<int>(roots.size()))
            {
                return nullptr;
            }

            return &roots[static_cast<std::size_t>(activeRootIndex)];
        };

        ContentBrowserRootState* activeRoot = nullptr;
        std::filesystem::path activeRootPath;
        std::string activeRootLabel;
        std::string activeRootSource;
        std::string activeRootBadge;
        bool activeRootReadOnly = false;
        auto syncActiveRootState = [&]()
        {
            activeRoot = getActiveRoot();
            if (activeRoot == nullptr)
            {
                activeRootPath.clear();
                activeRootLabel.clear();
                activeRootSource.clear();
                activeRootBadge.clear();
                activeRootReadOnly = false;
                return;
            }

            activeRootPath = activeRoot->path.lexically_normal();
            activeRootLabel = activeRoot->label;
            activeRootSource = activeRoot->source;
            activeRootBadge = activeRoot->badge;
            activeRootReadOnly = activeRoot->readOnly;
        };
        syncActiveRootState();

        if (activeRoot == nullptr)
        {
            ImGui::TextDisabled("No content root available.");
            ImGui::End();
            return;
        }

        std::error_code rootEc;
        if (!std::filesystem::exists(activeRootPath, rootEc) && !activeRootReadOnly)
        {
            std::filesystem::create_directories(activeRootPath, rootEc);
        }
        if (rootEc || !std::filesystem::exists(activeRootPath, rootEc))
        {
            ImGui::TextDisabled(
                "%s",
                activeRootReadOnly ? "Mounted package root is unavailable:" : "Unable to access content root:");
            ImGui::TextWrapped("%s", activeRootPath.string().c_str());
            ImGui::End();
            return;
        }

        if (currentDirectory.empty() ||
            !std::filesystem::exists(currentDirectory, rootEc) ||
            !std::filesystem::is_directory(currentDirectory, rootEc) ||
            !PathIsWithinRoot(currentDirectory, activeRootPath))
        {
            currentDirectory = activeRootPath;
            context.refreshEntries();
        }

        std::filesystem::path currentRelativePath;
        std::string currentRelativeString;
        auto syncCurrentRelative = [&]()
        {
            std::error_code relativeEc;
            currentRelativePath = std::filesystem::relative(currentDirectory, activeRootPath, relativeEc);
            if (relativeEc || currentRelativePath == "." || currentRelativePath.empty())
            {
                currentRelativePath.clear();
            }
            currentRelativeString = currentRelativePath.generic_string();
        };
        syncCurrentRelative();

        auto openDirectory = [&](const std::filesystem::path& directory) -> bool
        {
            if (directory.empty() || !PathIsWithinRoot(directory, activeRootPath))
            {
                return false;
            }

            std::error_code ec;
            if (!std::filesystem::exists(directory, ec) || !std::filesystem::is_directory(directory, ec))
            {
                return false;
            }

            currentDirectory = directory;
            context.refreshEntries();
            syncCurrentRelative();
            return true;
        };

        auto activateRoot = [&](const int rootIndex)
        {
            if (rootIndex < 0 || rootIndex >= static_cast<int>(roots.size()))
            {
                return;
            }

            activeRootIndex = rootIndex;
            syncActiveRootState();
            if (activeRoot == nullptr)
            {
                currentDirectory.clear();
                selectedEntry.clear();
                cache.ClearEntries();
                context.invalidateFolderTreeCache();
                return;
            }

            currentDirectory = activeRootPath;
            if (!selectedEntry.empty() && !PathIsWithinRoot(selectedEntry, activeRootPath))
            {
                selectedEntry.clear();
            }
            context.invalidateFolderTreeCache();
            context.refreshEntries();
            syncCurrentRelative();
            status = "Switched to " + activeRootLabel + ".";
        };

        const std::vector<std::string> droppedFiles = Input::ConsumeDroppedFiles();
        if (!droppedFiles.empty())
        {
            if (activeRootReadOnly)
            {
                status = "Drag/drop import is disabled for mounted package content.";
            }
            else if (!context.assetPipelineInitialized || !context.hasImportPipeline)
            {
                status = "Drag/drop import is unavailable: asset pipeline is not initialized.";
            }
            else
            {
                std::vector<PendingContentImport> newPendingImports;
                const std::filesystem::path assetsRoot = activeRootPath;
                const std::filesystem::path importDirectory =
                    (currentDirectory.empty() ? assetsRoot : currentDirectory).lexically_normal();

                auto normalizePath = [](std::filesystem::path path) -> std::filesystem::path
                {
                    std::error_code ec;
                    path = std::filesystem::weakly_canonical(path, ec);
                    if (ec)
                    {
                        path = path.lexically_normal();
                    }
                    return path;
                };

                const std::filesystem::path normalizedAssetsRoot = normalizePath(assetsRoot);
                auto isUnderAssetsRoot = [&](const std::filesystem::path& pathToCheck) -> bool
                {
                    std::string checked = normalizePath(pathToCheck).generic_string();
                    std::string root = normalizedAssetsRoot.generic_string();
#if defined(_WIN32)
                    checked = ToLowerString(checked);
                    root = ToLowerString(root);
#endif
                    if (checked.size() < root.size())
                    {
                        return false;
                    }
                    if (checked.compare(0, root.size(), root) != 0)
                    {
                        return false;
                    }
                    if (checked.size() == root.size())
                    {
                        return true;
                    }
                    const char separator = checked[root.size()];
                    return separator == '/' || separator == '\\';
                };

                auto toRelativeTarget = [&](const std::filesystem::path& absoluteDirectory) -> std::filesystem::path
                {
                    std::error_code relEc;
                    const std::filesystem::path relativePath = std::filesystem::relative(absoluteDirectory, assetsRoot, relEc);
                    if (!relEc && !relativePath.empty())
                    {
                        return relativePath.lexically_normal();
                    }
                    return ".";
                };

                auto queueSourceFile = [&](const std::filesystem::path& sourceFile, const std::filesystem::path& fallbackTargetDirectory)
                {
                    const std::filesystem::path normalizedSource = normalizePath(sourceFile);
                    if (!std::filesystem::exists(normalizedSource))
                    {
                        return;
                    }

                    std::filesystem::path sourceForImport = normalizedSource;
                    std::filesystem::path targetDirectoryAbsolute = fallbackTargetDirectory;

                    if (isUnderAssetsRoot(normalizedSource))
                    {
                        targetDirectoryAbsolute = normalizedSource.parent_path();
                    }
                    else
                    {
                        std::error_code copyEc;
                        std::filesystem::create_directories(targetDirectoryAbsolute, copyEc);
                        if (copyEc)
                        {
                            return;
                        }

                        const std::filesystem::path targetSourcePath = targetDirectoryAbsolute / normalizedSource.filename();
                        std::filesystem::copy_file(
                            normalizedSource,
                            targetSourcePath,
                            std::filesystem::copy_options::overwrite_existing,
                            copyEc);
                        if (copyEc)
                        {
                            return;
                        }

                        sourceForImport = targetSourcePath;
                    }

                    newPendingImports.push_back({
                        sourceForImport,
                        toRelativeTarget(targetDirectoryAbsolute)
                    });
                };

                for (const std::string& droppedFile : droppedFiles)
                {
                    std::error_code droppedEc;
                    std::filesystem::path droppedPath = std::filesystem::path(droppedFile);
                    if (!droppedPath.is_absolute())
                    {
                        droppedPath = std::filesystem::absolute(droppedPath, droppedEc);
                    }
                    droppedPath = droppedPath.lexically_normal();

                    if (std::filesystem::is_directory(droppedPath, droppedEc))
                    {
                        const std::string rootFolderName =
                            droppedPath.filename().string().empty() ? "ImportedFolder" : droppedPath.filename().string();
                        for (const auto& entry : std::filesystem::recursive_directory_iterator(
                                 droppedPath,
                                 std::filesystem::directory_options::skip_permission_denied,
                                 droppedEc))
                        {
                            if (droppedEc)
                            {
                                break;
                            }

                            if (entry.is_regular_file(droppedEc))
                            {
                                std::error_code relEc;
                                const std::filesystem::path relativeInFolder =
                                    std::filesystem::relative(entry.path(), droppedPath, relEc);
                                std::filesystem::path fallbackTargetDirectory = importDirectory / rootFolderName;
                                if (!relEc && !relativeInFolder.empty())
                                {
                                    fallbackTargetDirectory /= relativeInFolder.parent_path();
                                }

                                queueSourceFile(entry.path(), fallbackTargetDirectory.lexically_normal());
                            }
                        }
                        continue;
                    }

                    if (std::filesystem::is_regular_file(droppedPath, droppedEc))
                    {
                        queueSourceFile(droppedPath, importDirectory);
                    }
                }

                if (newPendingImports.empty())
                {
                    status = "Dropped files were ignored (no importable files detected).";
                }
                else
                {
                    const bool shouldStartNewTask = !contentImportActive;
                    if (shouldStartNewTask)
                    {
                        pendingImports.clear();
                        contentImportCursor = 0;
                        importedCount = 0;
                        skippedCount = 0;
                        failedCount = 0;
                        firstError.clear();
                        contentImportActive = true;

                        EditorTaskDesc importTaskDesc;
                        importTaskDesc.title = newPendingImports.size() > 1 ? "Importing Assets..." : "Importing Asset...";
                        importTaskDesc.subtask = "Queued import jobs...";
                        importTaskDesc.blocking = true;
                        importTaskDesc.cancellable = false;
                        contentImportTask = EditorTaskManager::BeginTask(importTaskDesc);
                        EditorTaskManager::SetProgress(contentImportTask, 0.01f);
                    }

                    pendingImports.insert(pendingImports.end(), newPendingImports.begin(), newPendingImports.end());

                    if (contentImportTask != 0)
                    {
                        EditorTaskManager::SetSubtask(
                            contentImportTask,
                            "Queued " + std::to_string(pendingImports.size()) + " file(s) for import...");
                    }

                    status = "Queued " + std::to_string(newPendingImports.size()) + " dropped file(s) for import.";
                }
            }
        }

        std::vector<ContentBrowserRootView> rootViews;
        rootViews.reserve(roots.size());
        for (const ContentBrowserRootState& root : roots)
        {
            ContentBrowserRootView rootView;
            rootView.id = root.id;
            rootView.label = root.label;
            rootView.badge = root.badge;
            rootView.source = root.source;
            rootView.path = root.path;
            rootView.readOnly = root.readOnly;
            rootView.packageRoot = root.packageRoot;
            rootViews.push_back(std::move(rootView));
        }

        ContentBrowserPanelContext panelContext;
        panelContext.roots = &rootViews;
        panelContext.activeRootIndex = &activeRootIndex;
        panelContext.activeRootPath = &activeRootPath;
        panelContext.activeRootLabel = &activeRootLabel;
        panelContext.activeRootSource = &activeRootSource;
        panelContext.activeRootBadge = &activeRootBadge;
        panelContext.activeRootReadOnly = &activeRootReadOnly;
        panelContext.currentRelativePath = &currentRelativePath;
        panelContext.currentRelativeString = &currentRelativeString;
        panelContext.selectedEntry = &selectedEntry;
        panelContext.status = &status;
        panelContext.cache = &cache;
        panelContext.thumbnailRenderer = context.thumbnailRenderer;
        panelContext.activateRoot = activateRoot;
        panelContext.requestFolderTreeRebuild = [&](const std::filesystem::path& rootPath)
        {
            context.rebuildFolderTreeCache(rootPath);
        };
        panelContext.openDirectory = openDirectory;
        panelContext.requestLoadScene = [&](const std::filesystem::path& scenePath)
        {
            context.requestLoadScene(scenePath);
        };
        panelContext.refresh = [&]()
        {
            context.invalidateFolderTreeCache();
            context.refreshEntries();
        };
        panelContext.createFolder = [&]()
        {
            std::error_code ec;
            std::filesystem::path newFolderPath;
            for (int i = 0; i < 1000; ++i)
            {
                const std::string suffix = i == 0 ? "" : " " + std::to_string(i);
                const std::filesystem::path candidate = currentDirectory / ("New Folder" + suffix);
                if (!std::filesystem::exists(candidate, ec))
                {
                    newFolderPath = candidate;
                    break;
                }
            }

            if (!newFolderPath.empty())
            {
                std::filesystem::create_directories(newFolderPath, ec);
                if (ec)
                {
                    status = "Failed to create folder: " + newFolderPath.filename().string();
                }
                else
                {
                    status = "Created folder: " + newFolderPath.filename().string();
                    context.invalidateFolderTreeCache();
                    context.refreshEntries();
                }
            }
        };
        panelContext.createScript = [&](const std::string& requestedName)
        {
            const std::string strippedName = StripLuaExtension(requestedName);
            const std::string scriptStem = SanitizeScriptFileStem(strippedName);

            std::error_code ec;
            std::filesystem::create_directories(currentDirectory, ec);
            if (ec)
            {
                status = "Failed to create script folder: " + currentDirectory.filename().string();
                return;
            }

            const std::filesystem::path scriptPath = BuildUniqueScriptPath(currentDirectory, scriptStem);
            if (scriptPath.empty())
            {
                status = "Failed to pick a script name in: " + currentDirectory.filename().string();
                return;
            }

            const std::string scriptIdentifier = BuildLuaIdentifier(scriptPath.stem().string());
            std::ofstream output(scriptPath, std::ios::out | std::ios::trunc);
            if (!output.is_open())
            {
                status = "Failed to create script: " + scriptPath.filename().string();
                return;
            }

            output
                << "local " << scriptIdentifier << " = {}\n\n"
                << "function " << scriptIdentifier << ":OnCreate()\n"
                << "end\n\n"
                << "function " << scriptIdentifier << ":OnUpdate(dt)\n"
                << "end\n\n"
                << "function " << scriptIdentifier << ":OnDestroy()\n"
                << "end\n\n"
                << "return " << scriptIdentifier << "\n";
            output.close();

            selectedEntry = scriptPath;
            status = "Created script: " + scriptPath.filename().string();
            context.refreshEntries();
        };
        panelContext.openAsset = [&](const std::filesystem::path& assetPath, const std::string& entryName)
        {
            context.openAsset(assetPath, entryName);
        };
        m_ContentBrowserPanel.Draw(panelContext);

        ImGui::End();
    }
}
