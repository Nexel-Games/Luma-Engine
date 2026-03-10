#include "Luma/Editor/Panels/Packages/PackageManagerPanel.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <exception>

#include <imgui.h>

#include "Luma/Asset/Package/PackageSemVer.h"
#include "Luma/Core/App/Project.h"
#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Editor/Plugins/BuiltInPluginRegistry.h"
#include "Luma/Editor/UI/TooltipAPI.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace Luma::Editor
{
    namespace
    {
        void ShowTooltip(const std::string_view tooltip)
        {
            UI::Tooltip::Show(tooltip);
        }

        bool ButtonWithTooltip(const char* label, const std::string_view tooltip, const ImVec2 size = ImVec2(0.0f, 0.0f))
        {
            const bool pressed = ImGui::Button(label, size);
            ShowTooltip(tooltip);
            return pressed;
        }

        bool SmallButtonWithTooltip(const char* label, const std::string_view tooltip)
        {
            const bool pressed = ImGui::SmallButton(label);
            ShowTooltip(tooltip);
            return pressed;
        }

        std::string BuildEngineRequirementSummary(const Assets::PackageVersionInfo& versionInfo)
        {
            if (!versionInfo.engineConstraint.empty())
            {
                return versionInfo.engineConstraint;
            }

            if (!versionInfo.engineMin.empty() && !versionInfo.engineMax.empty())
            {
                return ">=" + versionInfo.engineMin + " and <=" + versionInfo.engineMax;
            }
            if (!versionInfo.engineMin.empty())
            {
                return ">=" + versionInfo.engineMin;
            }
            if (!versionInfo.engineMax.empty())
            {
                return "<=" + versionInfo.engineMax;
            }

            return "Any";
        }

        bool EvaluateEngineCompatibility(
            const Assets::PackageVersionInfo& versionInfo,
            const std::string& projectEngineVersion,
            std::string& outMessage)
        {
            if (!versionInfo.engineConstraint.empty())
            {
                const bool compatible =
                    Assets::SatisfiesVersionConstraint(projectEngineVersion, versionInfo.engineConstraint);
                outMessage =
                    compatible
                        ? "Compatible with project engine " + projectEngineVersion + "."
                        : "Incompatible: project engine " + projectEngineVersion +
                            " does not satisfy " + versionInfo.engineConstraint + ".";
                return compatible;
            }

            Assets::SemVer current {};
            if (!Assets::TryParseSemVer(projectEngineVersion, current))
            {
                outMessage = "Project engine version is invalid: " + projectEngineVersion;
                return false;
            }

            if (!versionInfo.engineMin.empty())
            {
                Assets::SemVer minVersion {};
                if (!Assets::TryParseSemVer(versionInfo.engineMin, minVersion))
                {
                    outMessage = "Package manifest has invalid minimum engine version: " + versionInfo.engineMin;
                    return false;
                }
                if (Assets::CompareSemVer(current, minVersion) < 0)
                {
                    outMessage =
                        "Incompatible: project engine " + projectEngineVersion +
                        " is below required minimum " + versionInfo.engineMin + ".";
                    return false;
                }
            }

            if (!versionInfo.engineMax.empty())
            {
                Assets::SemVer maxVersion {};
                if (!Assets::TryParseSemVer(versionInfo.engineMax, maxVersion))
                {
                    outMessage = "Package manifest has invalid maximum engine version: " + versionInfo.engineMax;
                    return false;
                }
                if (Assets::CompareSemVer(current, maxVersion) > 0)
                {
                    outMessage =
                        "Incompatible: project engine " + projectEngineVersion +
                        " is above supported maximum " + versionInfo.engineMax + ".";
                    return false;
                }
            }

            outMessage = "Compatible with project engine " + projectEngineVersion + ".";
            return true;
        }

        const Assets::InstalledPackageRecord* FindInstalledPackage(
            const std::vector<Assets::InstalledPackageRecord>& installedPackages,
            const std::string& packageId,
            const std::string& version = {})
        {
            const auto it = std::find_if(
                installedPackages.begin(),
                installedPackages.end(),
                [&packageId, &version](const Assets::InstalledPackageRecord& record)
                {
                    if (record.package.id != packageId)
                    {
                        return false;
                    }

                    return version.empty() || record.package.version == version;
                });
            return it != installedPackages.end() ? &(*it) : nullptr;
        }

#if defined(_WIN32)
        bool LaunchEditorToolProcess(
            const std::filesystem::path& executablePath,
            const std::filesystem::path& workingDirectory,
            std::string& outError)
        {
            const std::wstring wideExecutable = executablePath.wstring();
            const std::wstring wideWorkingDirectory = workingDirectory.wstring();
            std::wstring commandLine = L"\"" + wideExecutable + L"\"";
            std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
            mutableCommandLine.push_back(L'\0');

            STARTUPINFOW startupInfo {};
            startupInfo.cb = sizeof(startupInfo);
            PROCESS_INFORMATION processInfo {};
            const BOOL created = ::CreateProcessW(
                wideExecutable.c_str(),
                mutableCommandLine.data(),
                nullptr,
                nullptr,
                FALSE,
                0,
                nullptr,
                wideWorkingDirectory.empty() ? nullptr : wideWorkingDirectory.c_str(),
                &startupInfo,
                &processInfo);
            if (!created)
            {
                outError = "CreateProcessW failed for " + executablePath.string();
                return false;
            }

            ::CloseHandle(processInfo.hThread);
            ::CloseHandle(processInfo.hProcess);
            outError.clear();
            return true;
        }
#else
        bool LaunchEditorToolProcess(
            const std::filesystem::path& executablePath,
            const std::filesystem::path& workingDirectory,
            std::string& outError)
        {
            (void)workingDirectory;
            outError = "Editor tool launching is only implemented on Windows for now: " + executablePath.string();
            return false;
        }
#endif
    }

    void PackageManagerPanel::Reset()
    {
        m_ProjectRoot.clear();
        m_PackageManager.reset();
        m_Initialized = false;
        m_RegistryLoaded = false;
        m_MountRootsDirty = true;
        m_Status.clear();
        m_SearchQuery.clear();
        m_CategoryIndex = 0;
        m_SelectedPackageId.clear();
        m_SelectedVersionIndex = 0;
        if (m_OperationTask != 0)
        {
            EditorTaskManager::EndTask(m_OperationTask);
        }
        m_DeferredOperation.reset();
        m_ActiveOperation.reset();
        m_OperationRunning = false;
        m_OperationTask = 0;
    }

    bool PackageManagerPanel::EnsureInitialized(const std::filesystem::path& projectRoot)
    {
        const std::filesystem::path normalizedProjectRoot = NormalizeProjectRoot(projectRoot);
        if (m_Initialized && m_ProjectRoot == normalizedProjectRoot && m_PackageManager)
        {
            return true;
        }

        Reset();
        m_ProjectRoot = normalizedProjectRoot;
        m_PackageManager = std::make_unique<Assets::PackageManager>();

        std::string packageError;
        if (!m_PackageManager->Initialize(m_ProjectRoot, packageError))
        {
            SetStatus("Package manager initialization failed: " + packageError);
            LUMA_LOG_ERROR("Package", m_Status);
            return false;
        }

        m_PackageManager->SetProgressCallback(
            [this](const Assets::PackageProgressEvent& event)
            {
                const std::string status =
                    event.message.empty() ? "Package operation running..." : event.message;
                SetStatus(status);
                if (m_OperationTask != 0)
                {
                    const float progress = std::clamp(event.progress01, 0.01f, 1.0f);
                    EditorTaskManager::SetSubtask(m_OperationTask, status);
                    EditorTaskManager::SetProgress(m_OperationTask, progress);
                }
            });

        m_Initialized = true;
        m_MountRootsDirty = true;
        SetStatus("Package manager ready.");
        return true;
    }

    bool PackageManagerPanel::RefreshRegistry(const std::filesystem::path& projectRoot)
    {
        if (!EnsureInitialized(projectRoot))
        {
            return false;
        }

        std::string refreshError;
        if (!m_PackageManager->RefreshRegistrySources(refreshError))
        {
            m_RegistryLoaded = false;
            SetStatus("Package registry refresh failed: " + refreshError);
            LUMA_LOG_ERROR("Package", m_Status);
            return false;
        }

        m_RegistryLoaded = true;
        const Assets::PackageSearchResult allPackages = m_PackageManager->Search({});
        if (m_SelectedPackageId.empty() && !allPackages.packages.empty())
        {
            m_SelectedPackageId = allPackages.packages.front().id;
            m_SelectedVersionIndex = 0;
        }

        SetStatus("Package registry refreshed.");
        LUMA_LOG_INFO("Package", m_Status);
        return true;
    }

    bool PackageManagerPanel::RequestRefresh(const std::filesystem::path& projectRoot)
    {
        if (!EnsureInitialized(projectRoot))
        {
            return false;
        }

        QueueRefresh();
        return true;
    }

    bool PackageManagerPanel::RequestInstall(
        const std::filesystem::path& projectRoot,
        const Assets::PackageInstallRequest& request)
    {
        if (!EnsureInitialized(projectRoot))
        {
            return false;
        }

        QueueInstall(request);
        return true;
    }

    bool PackageManagerPanel::RequestUpdate(
        const std::filesystem::path& projectRoot,
        const std::string& packageId,
        const std::string& version)
    {
        if (!EnsureInitialized(projectRoot))
        {
            return false;
        }

        QueueUpdate(packageId, version);
        return true;
    }

    bool PackageManagerPanel::RequestRemove(
        const std::filesystem::path& projectRoot,
        const std::string& packageId)
    {
        if (!EnsureInitialized(projectRoot))
        {
            return false;
        }

        QueueRemove(packageId);
        return true;
    }

    bool PackageManagerPanel::RequestVerify(
        const std::filesystem::path& projectRoot,
        const std::string& packageIdOrEmpty)
    {
        if (!EnsureInitialized(projectRoot))
        {
            return false;
        }

        QueueVerify(packageIdOrEmpty);
        return true;
    }

    void PackageManagerPanel::Tick(const std::filesystem::path& projectRoot)
    {
        if (!m_OperationRunning && m_DeferredOperation.has_value())
        {
            m_ActiveOperation = std::move(m_DeferredOperation);
            m_DeferredOperation.reset();
            m_OperationRunning = true;
            BeginOperationTask(*m_ActiveOperation);
            return;
        }

        if (!m_OperationRunning || !m_ActiveOperation.has_value())
        {
            return;
        }

        ExecuteDeferredOperation(projectRoot);
    }

    void PackageManagerPanel::Draw(const std::filesystem::path& projectRoot, bool* open)
    {
        if (!ImGui::Begin("Package Manager", open))
        {
            ImGui::End();
            return;
        }

        if (!EnsureInitialized(projectRoot))
        {
            ImGui::TextWrapped("%s", m_Status.c_str());
            ImGui::End();
            return;
        }

        const bool operationBusy = HasPendingOrRunningOperation();
        ImGui::BeginDisabled(operationBusy);
        if (ButtonWithTooltip("Refresh Registry", "Refresh package sources and rebuild the registry catalog."))
        {
            RequestRefresh(projectRoot);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", m_Status.empty() ? "Ready." : m_Status.c_str());

        if (ImGui::BeginTabBar("##PackageManagerTabs"))
        {
            if (ImGui::BeginTabItem("Registry"))
            {
                std::array<char, 256> searchBuffer {};
                std::snprintf(searchBuffer.data(), searchBuffer.size(), "%s", m_SearchQuery.c_str());
                ImGui::SetNextItemWidth(280.0f);
                if (ImGui::InputTextWithHint("##PackageRegistrySearch", "Search packages...", searchBuffer.data(), searchBuffer.size()))
                {
                    m_SearchQuery = searchBuffer.data();
                }
                ShowTooltip("Search the registry by package id, version, or category.");
                ImGui::SameLine();

                static constexpr const char* kCategories[] = { "All", "registry", "assets-store" };
                ImGui::SetNextItemWidth(140.0f);
                if (ImGui::Combo("##PackageCategory", &m_CategoryIndex, kCategories, IM_ARRAYSIZE(kCategories)))
                {
                }
                ShowTooltip("Filter registry results by source category.");

                Assets::PackageSearchQuery searchQuery {};
                searchQuery.text = m_SearchQuery;
                if (m_CategoryIndex > 0 && m_CategoryIndex < IM_ARRAYSIZE(kCategories))
                {
                    searchQuery.category = kCategories[m_CategoryIndex];
                }
                const Assets::PackageSearchResult searchResult = m_PackageManager->Search(searchQuery);

                ImGui::BeginChild("##PackageRegistryList", ImVec2(320.0f, 0.0f), true);
                if (!searchResult.success)
                {
                    ImGui::TextWrapped("%s", searchResult.message.c_str());
                }
                else if (searchResult.packages.empty())
                {
                    ImGui::TextDisabled("No packages match the current filter.");
                }
                else
                {
                    for (const Assets::PackageCatalogEntry& entry : searchResult.packages)
                    {
                        const bool selected = m_SelectedPackageId == entry.id;
                        const std::string label = entry.id + "##" + entry.id;
                        if (ImGui::Selectable(label.c_str(), selected))
                        {
                            m_SelectedPackageId = entry.id;
                            m_SelectedVersionIndex = 0;
                        }
                        ShowTooltip("Select this package to inspect installable versions.");
                        if (ImGui::IsItemHovered())
                        {
                            ImGui::SetTooltip(
                                "Latest: %s\nCategory: %s",
                                entry.latest.c_str(),
                                entry.category.c_str());
                        }
                        ImGui::SameLine();
                        ImGui::TextDisabled("%s", entry.latest.c_str());
                    }
                }
                ImGui::EndChild();

                ImGui::SameLine();

                ImGui::BeginChild("##PackageRegistryDetails", ImVec2(0.0f, 0.0f), true);
                if (m_SelectedPackageId.empty())
                {
                    ImGui::TextDisabled("Select a package to inspect and install.");
                }
                else
                {
                    Assets::PackageDetails details {};
                    std::string packageError;
                    if (!m_PackageManager->GetPackage(m_SelectedPackageId, details, packageError))
                    {
                        ImGui::TextWrapped("%s", packageError.c_str());
                    }
                    else
                    {
                        ImGui::TextUnformatted(details.catalog.id.c_str());
                        ImGui::TextDisabled("Latest: %s", details.catalog.latest.c_str());
                        ImGui::TextDisabled("Category: %s", details.catalog.category.c_str());
                        ImGui::Separator();

                        if (!details.versions.empty())
                        {
                            if (m_SelectedVersionIndex < 0 ||
                                m_SelectedVersionIndex >= static_cast<int>(details.versions.size()))
                            {
                                m_SelectedVersionIndex = 0;
                            }

                            std::vector<const char*> versionLabels;
                            versionLabels.reserve(details.versions.size());
                            for (const Assets::PackageVersionInfo& versionInfo : details.versions)
                            {
                                versionLabels.push_back(versionInfo.version.c_str());
                            }

                            ImGui::SetNextItemWidth(200.0f);
                            ImGui::Combo(
                                "Version",
                                &m_SelectedVersionIndex,
                                versionLabels.data(),
                                static_cast<int>(versionLabels.size()));
                            ShowTooltip("Select which package version to install.");

                            const Assets::PackageVersionInfo& selectedVersion =
                                details.versions[static_cast<std::size_t>(m_SelectedVersionIndex)];
                            const std::string projectEngineVersion = Assets::NormalizeSemVerString(
                                Project::IsLoaded() ? Project::GetConfig().engineVersion : std::string("0.0.1"));
                            const std::string requirementSummary = BuildEngineRequirementSummary(selectedVersion);
                            const Editor::BuiltInPluginDescriptor* builtInPlugin =
                                Editor::FindBuiltInPlugin(details.catalog.id);
                            const bool managedAsBuiltInPlugin = builtInPlugin != nullptr;
                            std::string engineMessage;
                            const bool engineCompatible =
                                EvaluateEngineCompatibility(selectedVersion, projectEngineVersion, engineMessage);
                            const bool metadataComplete =
                                !selectedVersion.sha256.empty() &&
                                selectedVersion.sizeBytes > 0 &&
                                !selectedVersion.url.empty();

                            const std::string integrityMessage =
                                metadataComplete
                                    ? "Registry metadata looks complete. Install will verify package integrity during the install transaction."
                                    : "Registry metadata is incomplete. Expected archive URL, SHA256, and size.";

                            if (!selectedVersion.description.empty())
                            {
                                ImGui::Spacing();
                                ImGui::TextWrapped("%s", selectedVersion.description.c_str());
                            }

                            const auto installedPackages = m_PackageManager->GetInstalled();
                            const Assets::InstalledPackageRecord* installedRecord =
                                FindInstalledPackage(installedPackages, details.catalog.id, selectedVersion.version);
                            const bool alreadyInstalled = installedRecord != nullptr;

                            ImGui::Spacing();
                            ImGui::Separator();
                            ImGui::TextDisabled("Compatibility");
                            ImGui::Text("Project Engine: %s", projectEngineVersion.c_str());
                            ImGui::Text("Package Requirement: %s", requirementSummary.c_str());
                            ImGui::Text("Archive Size: %llu bytes", static_cast<unsigned long long>(selectedVersion.sizeBytes));
                            ImGui::TextWrapped("Archive URL: %s", selectedVersion.url.c_str());

                            const ImVec4 okColor(0.48f, 0.88f, 0.58f, 1.0f);
                            const ImVec4 errorColor(0.96f, 0.43f, 0.43f, 1.0f);

                            ImGui::TextColored(
                                engineCompatible ? okColor : errorColor,
                                "%s",
                                engineMessage.c_str());
                            ImGui::TextColored(metadataComplete ? okColor : errorColor, "%s", integrityMessage.c_str());
                            if (managedAsBuiltInPlugin)
                            {
                                ImGui::TextColored(
                                    okColor,
                                    "Managed as built-in plugin. Enable it from Edit > Plugins instead of installing it as a package.");
                            }

                            const bool installAllowed =
                                !managedAsBuiltInPlugin &&
                                engineCompatible &&
                                metadataComplete;
                            ImGui::Spacing();
                            ImGui::BeginDisabled(operationBusy || !installAllowed);
                            if (ButtonWithTooltip(
                                    operationBusy ? "Working..." : (alreadyInstalled ? "Reinstall / Update" : "Install"),
                                    alreadyInstalled
                                        ? "Reinstall or update the selected package version."
                                        : "Install the selected package version into the active project.",
                                    ImVec2(140.0f, 0.0f)))
                            {
                                Assets::PackageInstallRequest request {};
                                request.id = details.catalog.id;
                                request.version = selectedVersion.version;
                                request.attemptHotLoad = true;
                                RequestInstall(projectRoot, request);
                            }
                            ImGui::EndDisabled();
                            ImGui::SameLine();
                            ImGui::BeginDisabled(operationBusy);
                            if (ButtonWithTooltip("Verify", "Verify package install integrity and cache hashes.", ImVec2(100.0f, 0.0f)))
                            {
                                QueueVerify(details.catalog.id);
                            }
                            ImGui::EndDisabled();
                            if (alreadyInstalled && installedRecord->editorTool.enabled)
                            {
                                ImGui::SameLine();
                                const std::string launchLabel =
                                    installedRecord->editorTool.displayName.empty()
                                        ? std::string("Launch")
                                        : "Launch " + installedRecord->editorTool.displayName;
                                if (ButtonWithTooltip(
                                        launchLabel.c_str(),
                                        "Launch the installed editor tool directly from this package.",
                                        ImVec2(160.0f, 0.0f)))
                                {
                                    LaunchInstalledEditorTool(projectRoot, installedRecord->package.id);
                                }
                            }

                            if (!installAllowed)
                            {
                                ImGui::Spacing();
                                if (!engineCompatible)
                                {
                                    ImGui::TextColored(errorColor, "Install blocked: project engine is incompatible.");
                                }
                                else
                                {
                                    ImGui::TextColored(errorColor, "Install blocked: registry metadata is incomplete.");
                                }
                            }

                            ImGui::Spacing();
                            ImGui::TextDisabled("Dependencies");
                            if (selectedVersion.dependencies.empty())
                            {
                                ImGui::TextDisabled("None");
                            }
                            else
                            {
                                for (const Assets::PackageDependency& dependency : selectedVersion.dependencies)
                                {
                                    ImGui::BulletText("%s %s", dependency.id.c_str(), dependency.constraint.c_str());
                                }
                            }

                            ImGui::Spacing();
                            ImGui::TextDisabled("Mount Roots");
                            const std::vector<Assets::PackageMountRoot> mountRoots = m_PackageManager->GetMountRoots();
                            bool anyMount = false;
                            for (const Assets::PackageMountRoot& mountRoot : mountRoots)
                            {
                                if (mountRoot.packageId != details.catalog.id)
                                {
                                    continue;
                                }
                                anyMount = true;
                                ImGui::BulletText("%s: %s", mountRoot.kind.c_str(), mountRoot.path.string().c_str());
                            }
                            if (!anyMount)
                            {
                                ImGui::TextDisabled("No mounted content for this package.");
                            }

                            if (alreadyInstalled && installedRecord->editorTool.enabled)
                            {
                                ImGui::Spacing();
                                ImGui::TextDisabled("Editor Tool");
                                ImGui::Text(
                                    "Display Name: %s",
                                    installedRecord->editorTool.displayName.empty()
                                        ? installedRecord->displayName.c_str()
                                        : installedRecord->editorTool.displayName.c_str());
                                if (installedRecord->editorTool.entryRelativePath.empty())
                                {
                                    ImGui::TextDisabled("Entry: Embedded editor panel");
                                }
                                else
                                {
                                    ImGui::TextWrapped(
                                        "Entry: %s",
                                        installedRecord->editorTool.entryRelativePath.generic_string().c_str());
                                    ImGui::TextWrapped(
                                        "Working Directory: %s",
                                        installedRecord->editorTool.workingDirectoryRelativePath.generic_string().c_str());
                                }
                            }
                        }
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Installed"))
            {
                const std::vector<Assets::InstalledPackageRecord> installedPackages = m_PackageManager->GetInstalled();
                if (installedPackages.empty())
                {
                    ImGui::TextDisabled("No packages installed in this project.");
                }
                else if (ImGui::BeginTable("##InstalledPackagesTable", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("Package");
                    ImGui::TableSetupColumn("Version");
                    ImGui::TableSetupColumn("Status");
                    ImGui::TableSetupColumn("Install Path");
                    ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 250.0f);
                    ImGui::TableHeadersRow();

                    for (const Assets::InstalledPackageRecord& record : installedPackages)
                    {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        const std::string packageLabel =
                            record.displayName.empty() ? record.package.id : record.displayName;
                        ImGui::TextUnformatted(packageLabel.c_str());
                        if (!record.displayName.empty() && record.displayName != record.package.id)
                        {
                            ImGui::TextDisabled("%s", record.package.id.c_str());
                        }
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextUnformatted(record.package.version.c_str());
                        ImGui::TableSetColumnIndex(2);
                        if (record.editorTool.enabled)
                        {
                            ImGui::TextUnformatted(record.restartRequired ? "Editor tool | Restart required" : "Editor tool | Loaded");
                        }
                        else
                        {
                            ImGui::TextUnformatted(record.restartRequired ? "Restart required" : "Loaded");
                        }
                        ImGui::TableSetColumnIndex(3);
                        ImGui::TextUnformatted(record.installPath.string().c_str());
                        ImGui::TableSetColumnIndex(4);
                        ImGui::PushID(record.package.id.c_str());
                        ImGui::BeginDisabled(operationBusy);
                        if (record.editorTool.enabled)
                        {
                            if (SmallButtonWithTooltip("Launch", "Launch this installed editor tool."))
                            {
                                LaunchInstalledEditorTool(projectRoot, record.package.id);
                            }
                            ImGui::SameLine();
                        }
                        if (SmallButtonWithTooltip("Verify", "Verify the installed package payload and cache integrity."))
                        {
                            QueueVerify(record.package.id);
                        }
                        ImGui::SameLine();
                        if (SmallButtonWithTooltip("Remove", "Remove this installed package from the active project."))
                        {
                            RequestRemove(projectRoot, record.package.id);
                            ImGui::EndDisabled();
                            ImGui::PopID();
                            break;
                        }
                        ImGui::EndDisabled();
                        ImGui::PopID();
                    }

                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Logs"))
            {
                const std::vector<std::string> logs = m_PackageManager->GetOperationLog();
                ImGui::BeginChild("##PackageManagerLogs", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
                if (logs.empty())
                {
                    ImGui::TextDisabled("No package manager log entries yet.");
                }
                else
                {
                    for (const std::string& line : logs)
                    {
                        ImGui::TextUnformatted(line.c_str());
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    Assets::PackageOperationResult PackageManagerPanel::Install(
        const std::filesystem::path& projectRoot,
        const Assets::PackageInstallRequest& request)
    {
        if (!EnsureInitialized(projectRoot))
        {
            return Assets::PackageOperationResult { false, false, m_Status, {}, {} };
        }

        const Assets::PackageOperationResult result = m_PackageManager->Install(request);
        SetStatus(result.message);
        LogOperationResult(result, "Installed package.", "Package install failed.");
        if (result.success)
        {
            m_MountRootsDirty = true;
            RefreshRegistry(projectRoot);
        }
        return result;
    }

    Assets::PackageOperationResult PackageManagerPanel::Update(
        const std::filesystem::path& projectRoot,
        const std::string& packageId,
        const std::string& version)
    {
        if (!EnsureInitialized(projectRoot))
        {
            return Assets::PackageOperationResult { false, false, m_Status, {}, {} };
        }

        const Assets::PackageOperationResult result = m_PackageManager->Update(packageId, version);
        SetStatus(result.message);
        LogOperationResult(result, "Updated package.", "Package update failed.");
        if (result.success)
        {
            m_MountRootsDirty = true;
            RefreshRegistry(projectRoot);
        }
        return result;
    }

    Assets::PackageOperationResult PackageManagerPanel::Remove(
        const std::filesystem::path& projectRoot,
        const std::string& packageId)
    {
        if (!EnsureInitialized(projectRoot))
        {
            return Assets::PackageOperationResult { false, false, m_Status, {}, {} };
        }

        const Assets::PackageOperationResult result = m_PackageManager->Remove(packageId);
        SetStatus(result.message);
        LogOperationResult(result, "Removed package.", "Package removal failed.");
        if (result.success)
        {
            m_MountRootsDirty = true;
            RefreshRegistry(projectRoot);
        }
        return result;
    }

    Assets::PackageOperationResult PackageManagerPanel::Verify(
        const std::filesystem::path& projectRoot,
        const std::string& packageIdOrEmpty)
    {
        if (!EnsureInitialized(projectRoot))
        {
            return Assets::PackageOperationResult { false, false, m_Status, {}, {} };
        }

        const Assets::PackageOperationResult result = m_PackageManager->Verify(packageIdOrEmpty);
        SetStatus(result.message);
        LogOperationResult(result, "Verified package state.", "Package verification failed.");
        return result;
    }

    bool PackageManagerPanel::HasLaunchableInstalledEditorTool(
        const std::filesystem::path& projectRoot,
        const std::string& packageId,
        std::string* outDisplayName)
    {
        if (!EnsureInitialized(projectRoot))
        {
            return false;
        }

        const std::vector<Assets::InstalledPackageRecord> installedPackages = m_PackageManager->GetInstalled();
        const Assets::InstalledPackageRecord* record = FindInstalledPackageRecord(installedPackages, packageId);
        if (record == nullptr || !record->editorTool.enabled || record->editorTool.entryRelativePath.empty())
        {
            return false;
        }

        const std::filesystem::path entryPath = record->installPath / record->editorTool.entryRelativePath;
        if (!std::filesystem::exists(entryPath))
        {
            return false;
        }

        if (outDisplayName != nullptr)
        {
            *outDisplayName =
                !record->editorTool.displayName.empty()
                    ? record->editorTool.displayName
                    : (!record->displayName.empty() ? record->displayName : record->package.id);
        }
        return true;
    }

    bool PackageManagerPanel::LaunchInstalledEditorTool(
        const std::filesystem::path& projectRoot,
        const std::string& packageId)
    {
        if (!EnsureInitialized(projectRoot))
        {
            return false;
        }

        if (m_EditorToolLaunchOverride && m_EditorToolLaunchOverride(projectRoot, packageId))
        {
            SetStatus("Opened " + packageId + " inside the editor.");
            LUMA_LOG_INFO("Package", m_Status);
            return true;
        }

        const std::vector<Assets::InstalledPackageRecord> installedPackages = m_PackageManager->GetInstalled();
        const Assets::InstalledPackageRecord* record = FindInstalledPackageRecord(installedPackages, packageId);
        if (record == nullptr || !record->editorTool.enabled || record->editorTool.entryRelativePath.empty())
        {
            SetStatus("No launchable editor tool metadata found for " + packageId + ".");
            LUMA_LOG_WARN("Package", m_Status);
            return false;
        }

        const std::filesystem::path entryPath = record->installPath / record->editorTool.entryRelativePath;
        const std::filesystem::path workingDirectory =
            record->editorTool.workingDirectoryRelativePath.empty()
                ? entryPath.parent_path()
                : (record->installPath / record->editorTool.workingDirectoryRelativePath);
        if (!std::filesystem::exists(entryPath))
        {
            SetStatus("Editor tool entry is missing: " + entryPath.string());
            LUMA_LOG_ERROR("Package", m_Status);
            return false;
        }

        std::string launchError;
        if (!LaunchEditorToolProcess(entryPath, workingDirectory, launchError))
        {
            SetStatus("Failed to launch editor tool: " + launchError);
            LUMA_LOG_ERROR("Package", m_Status);
            return false;
        }

        const std::string toolName =
            !record->editorTool.displayName.empty()
                ? record->editorTool.displayName
                : (!record->displayName.empty() ? record->displayName : record->package.id);
        SetStatus("Launched " + toolName + ".");
        LUMA_LOG_INFO("Package", m_Status);
        return true;
    }

    void PackageManagerPanel::SetEditorToolLaunchOverride(
        std::function<bool(const std::filesystem::path&, const std::string&)> callback)
    {
        m_EditorToolLaunchOverride = std::move(callback);
    }

    const std::string& PackageManagerPanel::GetStatus() const
    {
        return m_Status;
    }

    bool PackageManagerPanel::IsRegistryLoaded() const
    {
        return m_RegistryLoaded;
    }

    std::vector<Assets::PackageMountRoot> PackageManagerPanel::GetMountRoots() const
    {
        if (const Assets::PackageManager* manager = GetManager())
        {
            return manager->GetMountRoots();
        }

        return {};
    }

    const Assets::InstalledPackageRecord* PackageManagerPanel::FindInstalledPackageRecord(
        const std::vector<Assets::InstalledPackageRecord>& installedPackages,
        const std::string& packageId,
        const std::string& version) const
    {
        return FindInstalledPackage(installedPackages, packageId, version);
    }

    bool PackageManagerPanel::ConsumeMountRootsDirty()
    {
        const bool dirty = m_MountRootsDirty;
        m_MountRootsDirty = false;
        return dirty;
    }

    Assets::PackageManager* PackageManagerPanel::GetManager()
    {
        return m_PackageManager.get();
    }

    const Assets::PackageManager* PackageManagerPanel::GetManager() const
    {
        return m_PackageManager.get();
    }

    std::filesystem::path PackageManagerPanel::NormalizeProjectRoot(const std::filesystem::path& projectRoot) const
    {
        if (projectRoot.empty())
        {
            return std::filesystem::current_path().lexically_normal();
        }

        std::error_code ec;
        const std::filesystem::path normalized = std::filesystem::weakly_canonical(projectRoot, ec);
        if (ec)
        {
            return projectRoot.lexically_normal();
        }
        return normalized.lexically_normal();
    }

    void PackageManagerPanel::SetStatus(std::string status)
    {
        m_Status = std::move(status);
    }

    void PackageManagerPanel::LogOperationResult(
        const Assets::PackageOperationResult& result,
        const char* successVerb,
        const char* failureVerb)
    {
        if (result.success)
        {
            LUMA_LOG_INFO("Package", result.message.empty() ? successVerb : result.message);
        }
        else
        {
            LUMA_LOG_ERROR("Package", result.message.empty() ? failureVerb : result.message);
        }

        for (const std::string& diagnostic : result.diagnostics)
        {
            LUMA_LOG_WARN("Package", diagnostic);
        }
    }

    bool PackageManagerPanel::HasPendingOrRunningOperation() const
    {
        return m_OperationRunning || m_DeferredOperation.has_value() || m_ActiveOperation.has_value();
    }

    bool PackageManagerPanel::QueueOperation(DeferredOperation operation, std::string queuedStatus)
    {
        if (HasPendingOrRunningOperation())
        {
            SetStatus("A package operation is already in progress.");
            LUMA_LOG_WARN("Package", m_Status);
            return false;
        }

        m_DeferredOperation = std::move(operation);
        SetStatus(std::move(queuedStatus));
        return true;
    }

    EditorTaskDesc PackageManagerPanel::BuildOperationTaskDesc(const DeferredOperation& operation) const
    {
        EditorTaskDesc desc {};
        desc.blocking = true;
        desc.cancellable = false;

        switch (operation.type)
        {
        case DeferredOperationType::Refresh:
            desc.title = "Refreshing Package Registry...";
            desc.subtask = "Loading registry manifests and updating package catalog...";
            break;
        case DeferredOperationType::Install:
            desc.title = "Installing Package...";
            desc.subtask = "Preparing install for " + operation.installRequest.id + "...";
            break;
        case DeferredOperationType::Update:
            desc.title = "Updating Package...";
            desc.subtask = "Preparing update for " + operation.packageId + "...";
            break;
        case DeferredOperationType::Remove:
            desc.title = "Removing Package...";
            desc.subtask = "Removing " + operation.packageId + " from the project...";
            break;
        case DeferredOperationType::Verify:
            desc.title = "Verifying Package...";
            desc.subtask =
                operation.packageId.empty()
                    ? "Verifying installed package state..."
                    : "Verifying " + operation.packageId + "...";
            break;
        case DeferredOperationType::None:
        default:
            desc.title = "Working...";
            desc.subtask = "Running package operation...";
            break;
        }

        return desc;
    }

    void PackageManagerPanel::BeginOperationTask(const DeferredOperation& operation)
    {
        if (m_OperationTask != 0)
        {
            EditorTaskManager::EndTask(m_OperationTask);
            m_OperationTask = 0;
        }

        const EditorTaskDesc taskDesc = BuildOperationTaskDesc(operation);
        m_OperationTask = EditorTaskManager::BeginTask(taskDesc);
        EditorTaskManager::SetProgress(m_OperationTask, 0.01f);
    }

    void PackageManagerPanel::FinishOperationTask()
    {
        if (m_OperationTask == 0)
        {
            return;
        }

        EditorTaskManager::SetProgress(m_OperationTask, 1.0f);
        EditorTaskManager::EndTask(m_OperationTask);
        m_OperationTask = 0;
    }

    void PackageManagerPanel::QueueInstall(const Assets::PackageInstallRequest& request)
    {
        DeferredOperation operation;
        operation.type = DeferredOperationType::Install;
        operation.installRequest = request;
        QueueOperation(std::move(operation), "Queued package install for " + request.id + ".");
    }

    void PackageManagerPanel::QueueUpdate(std::string packageId, std::string version)
    {
        DeferredOperation operation;
        operation.type = DeferredOperationType::Update;
        operation.packageId = std::move(packageId);
        operation.version = std::move(version);
        QueueOperation(std::move(operation), "Queued package update for " + operation.packageId + ".");
    }

    void PackageManagerPanel::QueueRefresh()
    {
        DeferredOperation operation;
        operation.type = DeferredOperationType::Refresh;
        QueueOperation(std::move(operation), "Queued package registry refresh.");
    }

    void PackageManagerPanel::QueueVerify(std::string packageIdOrEmpty)
    {
        DeferredOperation operation;
        operation.type = DeferredOperationType::Verify;
        const std::string status =
            packageIdOrEmpty.empty()
                ? "Queued package verification."
                : "Queued package verification for " + packageIdOrEmpty + ".";
        operation.packageId = std::move(packageIdOrEmpty);
        QueueOperation(std::move(operation), status);
    }

    void PackageManagerPanel::QueueRemove(std::string packageId)
    {
        DeferredOperation operation;
        operation.type = DeferredOperationType::Remove;
        const std::string status = "Queued package removal for " + packageId + ".";
        operation.packageId = std::move(packageId);
        QueueOperation(std::move(operation), status);
    }

    void PackageManagerPanel::ExecuteDeferredOperation(const std::filesystem::path& projectRoot)
    {
        if (!m_ActiveOperation.has_value())
        {
            return;
        }

        const DeferredOperation operation = *m_ActiveOperation;
        m_ActiveOperation.reset();

        try
        {
            switch (operation.type)
            {
            case DeferredOperationType::Refresh:
                RefreshRegistry(projectRoot);
                break;
            case DeferredOperationType::Install:
                Install(projectRoot, operation.installRequest);
                break;
            case DeferredOperationType::Update:
                Update(projectRoot, operation.packageId, operation.version);
                break;
            case DeferredOperationType::Remove:
                Remove(projectRoot, operation.packageId);
                break;
            case DeferredOperationType::Verify:
                Verify(projectRoot, operation.packageId);
                break;
            case DeferredOperationType::None:
            default:
                break;
            }
        }
        catch (const std::exception& exception)
        {
            SetStatus("Package operation crashed: " + std::string(exception.what()));
            LUMA_LOG_ERROR("Package", m_Status);
        }
        catch (...)
        {
            SetStatus("Package operation crashed with an unknown exception.");
            LUMA_LOG_ERROR("Package", m_Status);
        }

        FinishOperationTask();
        m_OperationRunning = false;
    }

}
