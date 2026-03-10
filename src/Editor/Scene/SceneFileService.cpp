#include "Luma/Editor/Scene/SceneFileService.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <optional>

#include "Luma/Core/App/Project.h"
#include "Luma/Editor/Scene/SceneDocument.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

namespace Luma::Editor
{
    namespace
    {
        std::string TrimCopy(std::string value)
        {
            const auto isNotSpace = [](const unsigned char character)
            {
                return !std::isspace(character);
            };

            const auto first = std::find_if(value.begin(), value.end(), isNotSpace);
            if (first == value.end())
            {
                return {};
            }

            const auto last = std::find_if(value.rbegin(), value.rend(), isNotSpace).base();
            return std::string(first, last);
        }

#if defined(_WIN32)
        std::optional<std::filesystem::path> ShowNativeSaveSceneDialog(
            const std::filesystem::path& initialDirectory,
            const std::filesystem::path& initialFileName)
        {
            std::array<wchar_t, 4096> fileBuffer {};
            const std::wstring initialFile = initialFileName.wstring();
            if (!initialFile.empty())
            {
                const std::size_t copyLength = std::min(initialFile.size(), fileBuffer.size() - 1);
                initialFile.copy(fileBuffer.data(), static_cast<std::streamsize>(copyLength));
                fileBuffer[copyLength] = L'\0';
            }

            const std::wstring initialDirectoryWide = initialDirectory.empty() ? std::wstring {} : initialDirectory.wstring();
            const wchar_t filter[] = L"Luma Scene (*.scene)\0*.scene\0All Files (*.*)\0*.*\0";

            OPENFILENAMEW dialog {};
            dialog.lStructSize = sizeof(dialog);
            dialog.hwndOwner = nullptr;
            dialog.lpstrFile = fileBuffer.data();
            dialog.nMaxFile = static_cast<DWORD>(fileBuffer.size());
            dialog.lpstrFilter = filter;
            dialog.nFilterIndex = 1;
            dialog.lpstrDefExt = L"scene";
            dialog.lpstrInitialDir = initialDirectoryWide.empty() ? nullptr : initialDirectoryWide.c_str();
            dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER;

            if (!GetSaveFileNameW(&dialog))
            {
                return std::nullopt;
            }

            return std::filesystem::path(dialog.lpstrFile).lexically_normal();
        }
#endif
    }

    bool SceneFileService::OpenSaveAsPrompt(
        const SceneDocument& sceneDocument,
        std::string& contentStatus,
        const std::function<bool(const std::filesystem::path&)>& saveSceneToPath,
        const std::string_view suggestedName) const
    {
        std::string saveName = TrimCopy(std::string(suggestedName));
        if (saveName.empty())
        {
            if (sceneDocument.HasCurrentScenePath())
            {
                saveName = sceneDocument.GetCurrentScenePath().stem().string();
            }
            else
            {
                saveName = sceneDocument.GetDisplayName();
            }
        }

        if (saveName.empty())
        {
            saveName = "Untitled";
        }

        std::filesystem::path initialDirectory;
        if (Project::IsLoaded())
        {
            initialDirectory = Project::GetScenesPath();
        }
        else if (sceneDocument.HasCurrentScenePath())
        {
            initialDirectory = sceneDocument.GetCurrentScenePath().parent_path();
        }
        else
        {
            initialDirectory = std::filesystem::current_path();
        }

        std::filesystem::path initialFileName = saveName;
        initialFileName.replace_extension(".scene");

#if defined(_WIN32)
        const std::optional<std::filesystem::path> selectedPath =
            ShowNativeSaveSceneDialog(initialDirectory, initialFileName);
        if (!selectedPath.has_value())
        {
            contentStatus = "Scene save cancelled.";
            return false;
        }

        return saveSceneToPath ? saveSceneToPath(*selectedPath) : false;
#else
        const std::filesystem::path savePath = (initialDirectory / initialFileName).lexically_normal();
        return saveSceneToPath ? saveSceneToPath(savePath) : false;
#endif
    }

    bool SceneFileService::SetProjectStartScene(
        const std::filesystem::path& scenePath,
        std::string& projectConfigStatus,
        const std::function<void()>& invalidateProjectSettingsDraft) const
    {
        if (!Project::IsLoaded())
        {
            projectConfigStatus = "No project is loaded.";
            return false;
        }
        if (scenePath.empty())
        {
            projectConfigStatus = "No scene is active.";
            return false;
        }

        Project::ProjectConfig updatedConfig = Project::GetConfig();
        std::error_code ec;
        const std::filesystem::path relativePath = std::filesystem::relative(scenePath, Project::GetProjectRoot(), ec);
        updatedConfig.startScene = ec ? scenePath.lexically_normal().generic_string() : relativePath.generic_string();
        if (!Project::UpdateSettings(updatedConfig, true))
        {
            projectConfigStatus = "Failed to update project start scene.";
            return false;
        }

        if (invalidateProjectSettingsDraft)
        {
            invalidateProjectSettingsDraft();
        }
        projectConfigStatus = "Project start scene updated.";
        return true;
    }

    std::filesystem::path SceneFileService::ResolveScenePath(
        const SceneDocument& sceneDocument,
        const std::string& scenePath) const
    {
        return sceneDocument.ResolveScenePath(
            scenePath,
            Project::IsLoaded(),
            Project::IsLoaded() ? Project::GetProjectRoot() : std::filesystem::path {});
    }

    std::filesystem::path SceneFileService::GetDefaultScenePath(const SceneDocument& sceneDocument) const
    {
        return sceneDocument.GetDefaultScenePath(
            Project::IsLoaded(),
            Project::IsLoaded() ? Project::GetProjectRoot() : std::filesystem::path {},
            Project::IsLoaded() ? Project::GetScenesPath() : std::filesystem::path {},
            Project::IsLoaded() ? Project::GetConfig().startScene : std::string {});
    }

    std::filesystem::path SceneFileService::BuildScenePathFromName(
        const SceneDocument& sceneDocument,
        const std::string_view sceneName) const
    {
        return sceneDocument.BuildScenePathFromName(
            sceneName,
            Project::IsLoaded(),
            Project::IsLoaded() ? Project::GetScenesPath() : std::filesystem::path {});
    }

    std::string SceneFileService::GetActiveSceneDisplayName(const SceneDocument& sceneDocument) const
    {
        return sceneDocument.GetDisplayName();
    }
}
