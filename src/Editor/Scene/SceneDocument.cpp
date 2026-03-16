#include "Luma/Editor/Scene/SceneDocument.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <fstream>
#include <iterator>

#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SceneSerializer.h"

namespace Luma::Editor
{
    namespace
    {
        std::string TrimCopy(std::string value)
        {
            auto notWhitespace = [](const unsigned char c)
            {
                return !std::isspace(c);
            };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notWhitespace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notWhitespace).base(), value.end());
            return value;
        }
    }

    const std::filesystem::path& SceneDocument::GetCurrentScenePath() const
    {
        return m_CurrentScenePath;
    }

    bool SceneDocument::HasCurrentScenePath() const
    {
        return !m_CurrentScenePath.empty();
    }

    bool SceneDocument::IsDirtyFlag() const
    {
        return m_IsSceneDirty;
    }

    float SceneDocument::GetDirtyRefreshAccumulator() const
    {
        return m_SceneDirtyRefreshAccumulator;
    }

    void SceneDocument::AccumulateDirtyRefresh(const float deltaTimeSeconds)
    {
        m_SceneDirtyRefreshAccumulator += deltaTimeSeconds;
    }

    std::string SceneDocument::GetDisplayName() const
    {
        if (!m_CurrentScenePath.empty())
        {
            const std::string sceneName = m_CurrentScenePath.stem().string();
            if (!sceneName.empty())
            {
                return sceneName;
            }
        }

        if (!m_UnsavedSceneName.empty())
        {
            return m_UnsavedSceneName;
        }

        return "Untitled";
    }

    std::string SceneDocument::BuildWindowTitle(const std::string_view projectName) const
    {
        std::string title = "Luma";
        if (!projectName.empty())
        {
            title += " - ";
            title += projectName;
        }

        title += " - " + GetDisplayName();
        if (m_IsSceneDirty)
        {
            title += "*";
        }

        return title;
    }

    const std::string& SceneDocument::GetLastWindowTitle() const
    {
        return m_LastWindowTitle;
    }

    void SceneDocument::SetLastWindowTitle(std::string title)
    {
        m_LastWindowTitle = std::move(title);
    }

    bool SceneDocument::LoadFromPath(
        Scene& scene,
        const std::filesystem::path& scenePath,
        std::string& ioStatus,
        const Callbacks& callbacks)
    {
        if (scenePath.empty())
        {
            ioStatus = "Scene load failed: no scene path was provided.";
            return false;
        }

        Scene loadedScene;
        std::string sceneError;
        if (!SceneSerializer::Deserialize(scenePath, loadedScene, sceneError))
        {
            ioStatus = "Scene load failed: " + sceneError;
            return false;
        }

        scene.Swap(loadedScene);
        m_CurrentScenePath = scenePath.lexically_normal();
        m_UnsavedSceneName = m_CurrentScenePath.stem().string();
        if (callbacks.onScenePathChanged)
        {
            callbacks.onScenePathChanged(m_CurrentScenePath);
        }

        scene.UpdateWorldTransforms();
        if (callbacks.afterLoad)
        {
            callbacks.afterLoad();
        }

        std::string sceneSnapshotError;
        if (!CaptureSnapshot(scene, m_LastSavedSceneSnapshot, sceneSnapshotError))
        {
            ioStatus = "Scene snapshot update failed: " + sceneSnapshotError;
            m_LastSavedSceneSnapshot.clear();
        }

        m_IsSceneDirty = false;
        m_SceneDirtyRefreshAccumulator = 0.0f;
        ioStatus = "Loaded scene: " + m_CurrentScenePath.filename().string();
        return true;
    }

    bool SceneDocument::SaveToPath(
        Scene& scene,
        const std::filesystem::path& scenePath,
        std::string& ioStatus,
        const Callbacks& callbacks)
    {
        if (scenePath.empty())
        {
            ioStatus = "Scene save failed: no scene path was provided.";
            return false;
        }

        if (callbacks.beforeSerialize)
        {
            callbacks.beforeSerialize();
        }

        std::string sceneError;
        if (!SceneSerializer::Serialize(scene, scenePath, sceneError))
        {
            ioStatus = "Scene save failed: " + sceneError;
            return false;
        }

        m_CurrentScenePath = scenePath.lexically_normal();
        m_UnsavedSceneName = m_CurrentScenePath.stem().string();
        if (callbacks.onScenePathChanged)
        {
            callbacks.onScenePathChanged(m_CurrentScenePath);
        }
        if (callbacks.afterSave)
        {
            callbacks.afterSave();
        }

        std::string sceneSnapshotError;
        if (!CaptureSnapshot(scene, m_LastSavedSceneSnapshot, sceneSnapshotError))
        {
            ioStatus = "Scene snapshot update failed: " + sceneSnapshotError;
            m_LastSavedSceneSnapshot.clear();
            return false;
        }

        m_IsSceneDirty = false;
        m_SceneDirtyRefreshAccumulator = 0.0f;
        ioStatus = "Saved scene: " + m_CurrentScenePath.filename().string();
        return true;
    }

    void SceneDocument::CreateNew(Scene& scene, std::string& ioStatus, const Callbacks& callbacks)
    {
        scene.Clear();
        m_CurrentScenePath.clear();
        m_UnsavedSceneName = "Untitled";
        m_LastSavedSceneSnapshot.clear();
        m_IsSceneDirty = true;
        m_SceneDirtyRefreshAccumulator = 0.0f;

        if (callbacks.onScenePathChanged)
        {
            callbacks.onScenePathChanged({});
        }
        if (callbacks.afterNewScene)
        {
            callbacks.afterNewScene();
        }

        ioStatus = "Created a new untitled scene. Use Save Scene to name it.";
    }

    bool SceneDocument::RestoreSnapshot(
        Scene& scene,
        const std::string_view snapshot,
        std::string& ioStatus,
        const Callbacks& callbacks)
    {
        if (snapshot.empty())
        {
            ioStatus = "Scene restore failed: snapshot is empty.";
            return false;
        }

        std::error_code ec;
        std::filesystem::path tempRoot = std::filesystem::temp_directory_path(ec);
        if (ec || tempRoot.empty())
        {
            tempRoot = std::filesystem::current_path();
        }

        const auto uniqueStamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path tempScenePath =
            tempRoot / ("luma-scene-restore-" + std::to_string(uniqueStamp) + ".scene");

        {
            std::ofstream output(tempScenePath, std::ios::binary);
            if (!output)
            {
                ioStatus = "Scene restore failed: unable to create temporary restore file.";
                return false;
            }

            output.write(snapshot.data(), static_cast<std::streamsize>(snapshot.size()));
            if (!output.good())
            {
                ioStatus = "Scene restore failed: unable to write temporary restore file.";
                std::filesystem::remove(tempScenePath, ec);
                return false;
            }
        }

        Scene restoredScene;
        std::string sceneError;
        const bool loaded = SceneSerializer::Deserialize(tempScenePath, restoredScene, sceneError);
        std::filesystem::remove(tempScenePath, ec);
        if (!loaded)
        {
            ioStatus = "Scene restore failed: " + sceneError;
            return false;
        }

        scene.Swap(restoredScene);
        scene.UpdateWorldTransforms();
        if (callbacks.afterLoad)
        {
            callbacks.afterLoad();
        }

        RefreshDirtyState(scene);
        ioStatus = "Restored pre-play scene state.";
        return true;
    }

    bool SceneDocument::CaptureSnapshot(const Scene& scene, std::string& outSnapshot, std::string& outError) const
    {
        outSnapshot.clear();
        outError.clear();

        std::error_code ec;
        std::filesystem::path tempRoot = std::filesystem::temp_directory_path(ec);
        if (ec || tempRoot.empty())
        {
            tempRoot = std::filesystem::current_path();
        }

        const auto uniqueStamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::filesystem::path tempScenePath =
            tempRoot / ("luma-scene-snapshot-" + std::to_string(uniqueStamp) + ".scene");

        if (!SceneSerializer::Serialize(scene, tempScenePath, outError))
        {
            return false;
        }

        std::ifstream input(tempScenePath, std::ios::binary);
        if (!input)
        {
            outError = "Failed to open temporary scene snapshot.";
            std::filesystem::remove(tempScenePath, ec);
            return false;
        }

        outSnapshot.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        std::filesystem::remove(tempScenePath, ec);
        return true;
    }

    void SceneDocument::RefreshDirtyState(const Scene& scene)
    {
        std::string snapshot;
        std::string snapshotError;
        if (!CaptureSnapshot(scene, snapshot, snapshotError))
        {
            m_IsSceneDirty = true;
            m_SceneDirtyRefreshAccumulator = 0.0f;
            return;
        }

        m_IsSceneDirty = snapshot != m_LastSavedSceneSnapshot;
        m_SceneDirtyRefreshAccumulator = 0.0f;
    }

    std::filesystem::path SceneDocument::ResolveScenePath(
        const std::string& scenePath,
        const bool projectLoaded,
        const std::filesystem::path& projectRoot) const
    {
        if (scenePath.empty())
        {
            return {};
        }

        const std::filesystem::path rawPath(scenePath);
        if (rawPath.is_absolute() || !projectLoaded)
        {
            return rawPath.lexically_normal();
        }

        return (projectRoot / rawPath).lexically_normal();
    }

    std::filesystem::path SceneDocument::GetDefaultScenePath(
        const bool projectLoaded,
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& scenesPath,
        const std::string& configuredStartScene) const
    {
        if (projectLoaded)
        {
            const std::filesystem::path configuredScenePath = ResolveScenePath(configuredStartScene, true, projectRoot);
            if (!configuredScenePath.empty())
            {
                return configuredScenePath;
            }

            return (scenesPath / "Main.scene").lexically_normal();
        }

        return m_CurrentScenePath.lexically_normal();
    }

    std::filesystem::path SceneDocument::BuildScenePathFromName(
        std::string_view sceneName,
        const bool projectLoaded,
        const std::filesystem::path& scenesPath) const
    {
        std::string sanitizedName = TrimCopy(std::string(sceneName));
        for (char& character : sanitizedName)
        {
            const unsigned char unsignedCharacter = static_cast<unsigned char>(character);
            if (unsignedCharacter < 32 ||
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

        sanitizedName = TrimCopy(sanitizedName);
        if (sanitizedName.empty())
        {
            return {};
        }

        std::filesystem::path sceneRoot;
        if (projectLoaded)
        {
            sceneRoot = scenesPath;
        }
        else if (!m_CurrentScenePath.empty())
        {
            sceneRoot = m_CurrentScenePath.parent_path();
        }
        else
        {
            sceneRoot = std::filesystem::current_path();
        }

        return (sceneRoot / (sanitizedName + ".scene")).lexically_normal();
    }
}
