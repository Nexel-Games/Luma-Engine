#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "Luma/Audio/Core/AudioSystem.h"
#include "Luma/Core/App/Application.h"
#include "Luma/Core/Foundation/Assert.h"
#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Core/Foundation/Platform.h"
#include "Luma/Core/App/Project.h"
#include "Luma/Asset/CLI/AssetCLI.h"
#include "Luma/Core/App/RenderSelection.h"
#include "Luma/RHI/RendererAPI.h"
#include "Luma/Layers/EditorLayer.h"
#include "Luma/Scene/AudioListenerComponent.h"
#include "Luma/Scene/AudioSourceComponent.h"
#include "Luma/Scene/CameraComponent.h"
#include "Luma/Scene/IDComponent.h"
#include "Luma/Scene/LuaScriptComponent.h"
#include "Luma/Scene/MaterialComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/PrefabInstanceComponent.h"
#include "Luma/Scene/PrefabSerializer.h"
#include "Luma/Scene/RelationshipComponent.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SceneSerializer.h"
#include "Luma/Scene/TagComponent.h"
#include "Luma/Scene/TransformComponent.h"
#include "Luma/Scripting/LuaScriptRuntime.h"
#include "Luma/Scripting/ScriptProperty.h"
#include "Luma/Scripting/ScriptEngine.h"

namespace
{
    struct LaunchArgs
    {
        std::optional<std::filesystem::path> projectFile;
        std::optional<Luma::RenderPipelineProfile> pipelineOverride;
        std::optional<Luma::BackendPreference> backendOverride;
        std::optional<Luma::RendererAPI> apiOverride;
        std::optional<std::uint32_t> smokeTestFrames;
        std::optional<std::filesystem::path> luaSmokeTestScript;
        bool luaRuntimeSmokeTest = false;
        bool audioRuntimeSmokeTest = false;
        bool sceneRuntimeSmokeTest = false;
        bool prefabRuntimeSmokeTest = false;
    };

    bool WriteTextFile(const std::filesystem::path& path, const std::string_view contents)
    {
        std::ofstream output(path, std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            return false;
        }

        output << contents;
        return output.good();
    }

    bool NearlyEqual(const float lhs, const float rhs, const float epsilon = 1.0e-4f)
    {
        return std::abs(lhs - rhs) <= epsilon;
    }

    bool ExpectCondition(const bool condition, const std::string& message)
    {
        if (!condition)
        {
            std::cerr << message << '\n';
            return false;
        }
        return true;
    }

    Luma::EntityID FindEntityByName(const Luma::Scene& scene, const std::string_view name)
    {
        const auto view = scene.GetRegistry().view<Luma::TagComponent>();
        for (const Luma::EntityID entity : view)
        {
            if (view.get<Luma::TagComponent>(entity).name == name)
            {
                return entity;
            }
        }

        return entt::null;
    }

    void AppendLittleEndian16(std::vector<std::uint8_t>& bytes, const std::uint16_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
    }

    void AppendLittleEndian32(std::vector<std::uint8_t>& bytes, const std::uint32_t value)
    {
        bytes.push_back(static_cast<std::uint8_t>(value & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
        bytes.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xFFu));
    }

    bool WriteMonoPcmWav(
        const std::filesystem::path& path,
        const std::uint32_t sampleRate,
        const std::vector<std::int16_t>& samples)
    {
        const std::uint16_t channels = 1;
        const std::uint16_t bitsPerSample = 16;
        const std::uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8u);
        const std::uint16_t blockAlign = channels * (bitsPerSample / 8u);
        const std::uint32_t dataSize = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
        const std::uint32_t riffSize = 36u + dataSize;

        std::vector<std::uint8_t> bytes;
        bytes.reserve(44u + dataSize);
        bytes.insert(bytes.end(), { 'R', 'I', 'F', 'F' });
        AppendLittleEndian32(bytes, riffSize);
        bytes.insert(bytes.end(), { 'W', 'A', 'V', 'E' });
        bytes.insert(bytes.end(), { 'f', 'm', 't', ' ' });
        AppendLittleEndian32(bytes, 16u);
        AppendLittleEndian16(bytes, 1u);
        AppendLittleEndian16(bytes, channels);
        AppendLittleEndian32(bytes, sampleRate);
        AppendLittleEndian32(bytes, byteRate);
        AppendLittleEndian16(bytes, blockAlign);
        AppendLittleEndian16(bytes, bitsPerSample);
        bytes.insert(bytes.end(), { 'd', 'a', 't', 'a' });
        AppendLittleEndian32(bytes, dataSize);

        for (const std::int16_t sample : samples)
        {
            AppendLittleEndian16(bytes, static_cast<std::uint16_t>(sample));
        }

        std::ofstream output(path, std::ios::binary | std::ios::out | std::ios::trunc);
        if (!output.is_open())
        {
            return false;
        }

        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        return output.good();
    }

    bool RunLuaRuntimeSmokeTest()
    {
        using namespace std::chrono_literals;

        std::error_code ec;
        const std::filesystem::path tempRoot =
            (std::filesystem::temp_directory_path(ec) / "LumaLuaRuntimeSmoke").lexically_normal();
        if (ec)
        {
            std::cerr << "Failed to resolve temp directory for Lua runtime smoke test.\n";
            return false;
        }

        std::filesystem::create_directories(tempRoot, ec);
        if (ec)
        {
            std::cerr << "Failed to create temp directory: " << tempRoot.string() << '\n';
            return false;
        }

        const std::filesystem::path scriptPath = tempRoot / "lua_runtime_smoke.lua";

        constexpr std::string_view scriptVersion1 = R"(local RuntimeSmoke = {}

RuntimeSmoke.Properties = {
    Step = 2.0
}

function RuntimeSmoke:OnCreate()
    Luma.Entity.SetName(self.entity, "LuaCreated")
end

function RuntimeSmoke:OnStart()
    Luma.Transform.SetPosition(self.entity, 1.0, 0.0, 0.0)
end

function RuntimeSmoke:OnUpdate(dt)
    local position = Luma.Transform.GetPosition(self.entity)
    Luma.Transform.SetPosition(self.entity, position.x + self.Properties.Step, position.y, position.z)
end

function RuntimeSmoke:OnFixedUpdate(dt)
    local position = Luma.Transform.GetPosition(self.entity)
    Luma.Transform.SetPosition(self.entity, position.x, position.y + 2.0, position.z)
end

function RuntimeSmoke:OnLateUpdate(dt)
    local position = Luma.Transform.GetPosition(self.entity)
    Luma.Transform.SetPosition(self.entity, position.x, position.y, position.z + 3.0)
end

function RuntimeSmoke:OnDestroy()
    Luma.Entity.SetName(self.entity, "LuaDestroyed")
end

return RuntimeSmoke
)";

        constexpr std::string_view scriptFaulted = R"(local RuntimeSmoke = {}

RuntimeSmoke.Properties = {
    Step = 9.0
}

function RuntimeSmoke:OnCreate()
    Luma.Entity.SetName(self.entity, "LuaFaulted")
end

function RuntimeSmoke:OnUpdate(dt)
    local broken = 7
    return broken.value
end

return RuntimeSmoke
)";

        constexpr std::string_view scriptVersion2 = R"(local RuntimeSmoke = {}

RuntimeSmoke.Properties = {
    Step = 9.0
}

function RuntimeSmoke:OnCreate()
    Luma.Entity.SetName(self.entity, "LuaRecovered")
end

function RuntimeSmoke:OnUpdate(dt)
    local position = Luma.Transform.GetPosition(self.entity)
    Luma.Transform.SetPosition(self.entity, position.x + self.Properties.Step, position.y, position.z)
end

function RuntimeSmoke:OnDestroy()
    Luma.Entity.SetName(self.entity, "LuaStopped")
end

return RuntimeSmoke
)";

        if (!WriteTextFile(scriptPath, scriptVersion1))
        {
            std::cerr << "Failed to write initial Lua runtime smoke script.\n";
            return false;
        }

        Luma::Scene scene;
        Luma::Entity entity = scene.CreateEntity("LuaRuntimeSmoke");
        auto& scriptComponent = entity.AddComponent<Luma::LuaScriptComponent>();
        scriptComponent.scriptAsset = scriptPath.string();
        Luma::ScriptValue stepOverride;
        stepOverride.type = Luma::ScriptValueType::Float;
        stepOverride.floatValue = 4.0f;
        scriptComponent.propertyOverrides["Step"] = stepOverride;

        Luma::LuaScriptRuntime runtime;
        runtime.Start(scene);

        auto& registry = scene.GetRegistry();
        const Luma::EntityID entityId = entity.GetHandle();
        auto& tag = registry.get<Luma::TagComponent>(entityId);
        auto& transform = registry.get<Luma::TransformComponent>(entityId);

        if (!ExpectCondition(tag.name == "LuaCreated", "Lua runtime smoke: OnCreate did not run.") ||
            !ExpectCondition(NearlyEqual(transform.position[0], 1.0f), "Lua runtime smoke: OnStart did not set X position."))
        {
            runtime.Stop();
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        runtime.Update(scene, 1.0f / 60.0f);
        runtime.RunFixedUpdates(scene, 1, 1.0f / 60.0f);

        if (!ExpectCondition(NearlyEqual(transform.position[0], 5.0f), "Lua runtime smoke: property override was not applied to OnUpdate.") ||
            !ExpectCondition(NearlyEqual(transform.position[1], 2.0f), "Lua runtime smoke: OnFixedUpdate did not run.") ||
            !ExpectCondition(NearlyEqual(transform.position[2], 3.0f), "Lua runtime smoke: OnLateUpdate did not run."))
        {
            runtime.Stop();
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        std::this_thread::sleep_for(1200ms);
        if (!WriteTextFile(scriptPath, scriptFaulted))
        {
            std::cerr << "Failed to write faulted Lua runtime smoke script.\n";
            runtime.Stop();
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        runtime.Update(scene, 1.0f / 60.0f);
        const std::string* faultMessage = runtime.FindLastError(entityId);
        if (!ExpectCondition(runtime.IsFaulted(entityId), "Lua runtime smoke: runtime did not fault after broken hot reload.") ||
            !ExpectCondition(faultMessage != nullptr && faultMessage->find("attempt to index") != std::string::npos,
                "Lua runtime smoke: fault message did not contain the expected Lua error."))
        {
            runtime.Stop();
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        const float xBeforeRecovery = transform.position[0];

        std::this_thread::sleep_for(1200ms);
        if (!WriteTextFile(scriptPath, scriptVersion2))
        {
            std::cerr << "Failed to write recovered Lua runtime smoke script.\n";
            runtime.Stop();
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        runtime.Update(scene, 1.0f / 60.0f);
        if (!ExpectCondition(!runtime.IsFaulted(entityId), "Lua runtime smoke: runtime did not recover after valid hot reload.") ||
            !ExpectCondition(tag.name == "LuaRecovered", "Lua runtime smoke: recovered script did not rerun OnCreate.") ||
            !ExpectCondition(NearlyEqual(transform.position[0], xBeforeRecovery + 4.0f),
                "Lua runtime smoke: property override did not survive hot reload recovery."))
        {
            runtime.Stop();
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        runtime.Stop();
        if (!ExpectCondition(tag.name == "LuaStopped", "Lua runtime smoke: OnDestroy did not run on Stop()."))
        {
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        std::filesystem::remove_all(tempRoot, ec);
        return true;
    }

    bool RunAudioRuntimeSmokeTest()
    {
        using namespace std::chrono_literals;

        std::error_code ec;
        const std::filesystem::path tempRoot =
            (std::filesystem::temp_directory_path(ec) / "LumaAudioRuntimeSmoke").lexically_normal();
        if (ec)
        {
            std::cerr << "Failed to resolve temp directory for audio runtime smoke test.\n";
            return false;
        }

        std::filesystem::create_directories(tempRoot, ec);
        if (ec)
        {
            std::cerr << "Failed to create temp directory: " << tempRoot.string() << '\n';
            return false;
        }

        const std::filesystem::path wavPath = tempRoot / "audio_runtime_smoke.wav";
        constexpr std::uint32_t sampleRate = 48000u;
        constexpr std::size_t sampleCount = 4800u;
        std::vector<std::int16_t> samples(sampleCount, 0);
        for (std::size_t index = 0; index < samples.size(); ++index)
        {
            const float t = static_cast<float>(index) / static_cast<float>(sampleRate);
            const float value = std::sin(2.0f * 3.14159265359f * 440.0f * t);
            samples[index] = static_cast<std::int16_t>(value * 16000.0f);
        }

        if (!WriteMonoPcmWav(wavPath, sampleRate, samples))
        {
            std::cerr << "Failed to write WAV file for audio runtime smoke test.\n";
            return false;
        }

        if (!Luma::Audio::AudioSystem::Initialize())
        {
            std::filesystem::remove_all(tempRoot, ec);
            std::cerr << "Failed to initialize audio system for smoke test.\n";
            return false;
        }

        const auto clip = Luma::Audio::AudioSystem::LoadClip(wavPath);
        const auto cachedClip = Luma::Audio::AudioSystem::LoadClip(wavPath);
        const bool hasPlaybackDevice = Luma::Audio::AudioSystem::HasPlaybackDevice();
        const bool clipValid =
            clip != nullptr &&
            clip->IsValid() &&
            clip->GetChannels() == 1u &&
            clip->GetSampleRate() == sampleRate &&
            clip->GetDuration() > 0.05f &&
            clip == cachedClip;

        if (!ExpectCondition(Luma::Audio::AudioSystem::IsInitialized(), "Audio runtime smoke: audio system did not initialize.") ||
            !ExpectCondition(clipValid, "Audio runtime smoke: clip load/cache validation failed."))
        {
            Luma::Audio::AudioSystem::Shutdown();
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        Luma::Audio::PlaySettings play2DSettings;
        play2DSettings.volume = 0.6f;
        play2DSettings.pitch = 1.1f;
        const Luma::Audio::AudioHandle handle2D = Luma::Audio::AudioSystem::Play2D(clip, play2DSettings);
        if (hasPlaybackDevice &&
            !ExpectCondition(handle2D != 0, "Audio runtime smoke: Play2D returned an invalid handle with a playback device present."))
        {
            Luma::Audio::AudioSystem::Shutdown();
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        if (handle2D != 0)
        {
            Luma::Audio::AudioSystem::SetVolume(handle2D, 0.5f);
            Luma::Audio::AudioSystem::SetPitch(handle2D, 0.95f);
            Luma::Audio::AudioSystem::Pause(handle2D);
            Luma::Audio::AudioSystem::Resume(handle2D);
        }

        Luma::Audio::PlaySettings play3DSettings;
        play3DSettings.spatialized = true;
        play3DSettings.minDistance = 1.0f;
        play3DSettings.maxDistance = 20.0f;
        const Luma::Audio::AudioHandle handle3D =
            Luma::Audio::AudioSystem::Play3D(clip, { 0.0f, 0.0f, 1.0f }, play3DSettings);
        if (hasPlaybackDevice &&
            !ExpectCondition(handle3D != 0, "Audio runtime smoke: Play3D returned an invalid handle with a playback device present."))
        {
            Luma::Audio::AudioSystem::StopAll();
            Luma::Audio::AudioSystem::Shutdown();
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        if (handle3D != 0)
        {
            Luma::Audio::AudioSystem::SetPosition(handle3D, { 1.0f, 2.0f, 3.0f });
        }

        Luma::Audio::AudioSystem::SetListenerTransform(
            { 0.0f, 0.0f, 0.0f },
            { 0.0f, 0.0f, -1.0f },
            { 0.0f, 1.0f, 0.0f });
        Luma::Audio::AudioSystem::SetMasterVolume(0.75f);
        if (!ExpectCondition(NearlyEqual(Luma::Audio::AudioSystem::GetMasterVolume(), 0.75f),
                "Audio runtime smoke: master volume did not round-trip correctly."))
        {
            Luma::Audio::AudioSystem::StopAll();
            Luma::Audio::AudioSystem::Shutdown();
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        std::this_thread::sleep_for(100ms);
        Luma::Audio::AudioSystem::Update(0.1f);
        Luma::Audio::AudioSystem::StopAll();
        Luma::Audio::AudioSystem::Shutdown();
        std::filesystem::remove_all(tempRoot, ec);
        return true;
    }

    bool RunSceneRuntimeSmokeTest()
    {
        std::error_code ec;
        const std::filesystem::path tempRoot =
            (std::filesystem::temp_directory_path(ec) / "LumaSceneRuntimeSmoke").lexically_normal();
        if (ec)
        {
            std::cerr << "Failed to resolve temp directory for scene runtime smoke test.\n";
            return false;
        }

        std::filesystem::create_directories(tempRoot, ec);
        if (ec)
        {
            std::cerr << "Failed to create temp directory: " << tempRoot.string() << '\n';
            return false;
        }

        const std::filesystem::path scenePath = tempRoot / "scene_runtime_smoke.lumascene";

        Luma::Scene sourceScene;
        Luma::Entity root = sourceScene.CreateEntity("SceneRoot");
        auto& rootTag = root.GetComponent<Luma::TagComponent>();
        rootTag.tag = "Gameplay";
        auto& rootTransform = root.GetComponent<Luma::TransformComponent>();
        rootTransform.position = { 1.0f, 2.0f, 3.0f };
        rootTransform.rotation = { 10.0f, 20.0f, 30.0f };
        rootTransform.scale = { 1.5f, 2.0f, 2.5f };

        auto& rootMesh = root.AddComponent<Luma::MeshRendererComponent>();
        rootMesh.usePrimitive = true;
        rootMesh.primitive = Luma::PrimitiveType::Sphere;
        rootMesh.color = { 0.2f, 0.4f, 0.6f, 1.0f };

        auto& rootMaterial = root.AddComponent<Luma::MaterialComponent>();
        rootMaterial.name = "SceneSmokeMaterial";
        rootMaterial.albedoColor = { 0.3f, 0.5f, 0.7f, 1.0f };
        rootMaterial.metallic = 0.45f;
        rootMaterial.smoothness = 0.85f;

        auto& rootCamera = root.AddComponent<Luma::CameraComponent>();
        rootCamera.primary = false;
        rootCamera.renderPriority = 7;

        auto& rootListener = root.AddComponent<Luma::AudioListenerComponent>();
        rootListener.enabled = true;
        rootListener.volume = 0.75f;

        Luma::Entity child = sourceScene.CreateEntity("SceneChild");
        sourceScene.SetParent(child.GetHandle(), root.GetHandle());
        auto& childTag = child.GetComponent<Luma::TagComponent>();
        childTag.tag = "Interactable";
        auto& childTransform = child.GetComponent<Luma::TransformComponent>();
        childTransform.position = { 4.0f, 5.0f, 6.0f };

        auto& childScript = child.AddComponent<Luma::LuaScriptComponent>();
        childScript.enabled = false;
        childScript.scriptAsset = "Assets/Scripts/SceneSmoke.lua";

        Luma::ScriptValue speedOverride {};
        speedOverride.type = Luma::ScriptValueType::Float;
        speedOverride.floatValue = 5.5f;
        childScript.propertyOverrides["Speed"] = speedOverride;

        Luma::ScriptValue enabledOverride {};
        enabledOverride.type = Luma::ScriptValueType::Bool;
        enabledOverride.boolValue = true;
        childScript.propertyOverrides["Enabled"] = enabledOverride;

        Luma::ScriptValue targetOverride {};
        targetOverride.type = Luma::ScriptValueType::Entity;
        targetOverride.entityValue = root.GetComponent<Luma::IDComponent>().id;
        childScript.propertyOverrides["Target"] = targetOverride;

        auto& childSource = child.AddComponent<Luma::AudioSourceComponent>();
        childSource.clipAsset = "Assets/Audio/scene_smoke.wav";
        childSource.playOnAwake = true;
        childSource.looping = true;
        childSource.spatialized = false;
        childSource.volume = 0.6f;
        childSource.pitch = 1.15f;
        childSource.minDistance = 2.0f;
        childSource.maxDistance = 40.0f;

        sourceScene.UpdateWorldTransforms();

        std::string error;
        if (!Luma::SceneSerializer::Serialize(sourceScene, scenePath, error))
        {
            std::cerr << "Scene runtime smoke: failed to serialize scene: " << error << '\n';
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        Luma::Scene loadedScene;
        if (!Luma::SceneSerializer::Deserialize(scenePath, loadedScene, error))
        {
            std::cerr << "Scene runtime smoke: failed to deserialize scene: " << error << '\n';
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        loadedScene.UpdateWorldTransforms();

        const auto roots = loadedScene.GetRootEntities();
        if (!ExpectCondition(roots.size() == 1u, "Scene runtime smoke: expected exactly one root entity after load."))
        {
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        const Luma::EntityID loadedRoot = FindEntityByName(loadedScene, "SceneRoot");
        const Luma::EntityID loadedChild = FindEntityByName(loadedScene, "SceneChild");
        const auto& registry = loadedScene.GetRegistry();
        if (!ExpectCondition(loadedRoot != entt::null && loadedChild != entt::null,
                "Scene runtime smoke: failed to find named entities after load."))
        {
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        const auto& loadedRootTag = registry.get<Luma::TagComponent>(loadedRoot);
        const auto& loadedRootTransform = registry.get<Luma::TransformComponent>(loadedRoot);
        const auto& loadedRootMesh = registry.get<Luma::MeshRendererComponent>(loadedRoot);
        const auto& loadedRootMaterial = registry.get<Luma::MaterialComponent>(loadedRoot);
        const auto& loadedRootCamera = registry.get<Luma::CameraComponent>(loadedRoot);
        const auto& loadedRootListener = registry.get<Luma::AudioListenerComponent>(loadedRoot);
        const auto& loadedChildRelationship = registry.get<Luma::RelationshipComponent>(loadedChild);
        const auto& loadedChildTag = registry.get<Luma::TagComponent>(loadedChild);
        const auto& loadedChildTransform = registry.get<Luma::TransformComponent>(loadedChild);
        const auto& loadedChildScript = registry.get<Luma::LuaScriptComponent>(loadedChild);
        const auto& loadedChildSource = registry.get<Luma::AudioSourceComponent>(loadedChild);

        const auto speedIt = loadedChildScript.propertyOverrides.find("Speed");
        const auto enabledIt = loadedChildScript.propertyOverrides.find("Enabled");
        const auto targetIt = loadedChildScript.propertyOverrides.find("Target");

        const bool valuesValid =
            loadedRootTag.tag == "Gameplay" &&
            loadedChildTag.tag == "Interactable" &&
            loadedChildRelationship.parent == loadedRoot &&
            loadedRootMesh.primitive == Luma::PrimitiveType::Sphere &&
            NearlyEqual(loadedRootMaterial.metallic, 0.45f) &&
            NearlyEqual(loadedRootMaterial.smoothness, 0.85f) &&
            !loadedRootCamera.primary &&
            loadedRootCamera.renderPriority == 7 &&
            loadedRootListener.enabled &&
            NearlyEqual(loadedRootListener.volume, 0.75f) &&
            NearlyEqual(loadedRootTransform.position[0], 1.0f) &&
            NearlyEqual(loadedChildTransform.worldPosition[0], 5.0f) &&
            NearlyEqual(loadedChildTransform.worldPosition[1], 7.0f) &&
            NearlyEqual(loadedChildTransform.worldPosition[2], 9.0f) &&
            !loadedChildScript.enabled &&
            loadedChildScript.scriptAsset == "Assets/Scripts/SceneSmoke.lua" &&
            speedIt != loadedChildScript.propertyOverrides.end() &&
            speedIt->second.type == Luma::ScriptValueType::Float &&
            NearlyEqual(speedIt->second.floatValue, 5.5f) &&
            enabledIt != loadedChildScript.propertyOverrides.end() &&
            enabledIt->second.type == Luma::ScriptValueType::Bool &&
            enabledIt->second.boolValue &&
            targetIt != loadedChildScript.propertyOverrides.end() &&
            targetIt->second.type == Luma::ScriptValueType::Entity &&
            targetIt->second.entityValue == registry.get<Luma::IDComponent>(loadedRoot).id &&
            loadedChildSource.clipAsset == "Assets/Audio/scene_smoke.wav" &&
            loadedChildSource.playOnAwake &&
            loadedChildSource.looping &&
            !loadedChildSource.spatialized &&
            NearlyEqual(loadedChildSource.volume, 0.6f) &&
            loadedChildSource.runtimeHandle == 0;

        if (!ExpectCondition(valuesValid, "Scene runtime smoke: round-trip component values did not match expectations."))
        {
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        std::filesystem::remove_all(tempRoot, ec);
        return true;
    }

    bool RunPrefabRuntimeSmokeTest()
    {
        std::error_code ec;
        const std::filesystem::path tempRoot =
            (std::filesystem::temp_directory_path(ec) / "LumaPrefabRuntimeSmoke").lexically_normal();
        if (ec)
        {
            std::cerr << "Failed to resolve temp directory for prefab runtime smoke test.\n";
            return false;
        }

        std::filesystem::create_directories(tempRoot, ec);
        if (ec)
        {
            std::cerr << "Failed to create temp directory: " << tempRoot.string() << '\n';
            return false;
        }

        const std::filesystem::path prefabPath = tempRoot / "prefab_runtime_smoke.lumaprefab";
        std::string error;

        Luma::Scene sourceScene;
        Luma::Entity root = sourceScene.CreateEntity("PrefabRoot");
        root.GetComponent<Luma::TransformComponent>().position = { 1.0f, 0.0f, 0.0f };
        auto& rootSource = root.AddComponent<Luma::AudioSourceComponent>();
        rootSource.clipAsset = "Assets/Audio/prefab_smoke.wav";
        rootSource.volume = 0.9f;
        rootSource.looping = true;

        Luma::Entity child = sourceScene.CreateEntity("PrefabChild");
        sourceScene.SetParent(child.GetHandle(), root.GetHandle());
        child.GetComponent<Luma::TransformComponent>().position = { 0.0f, 2.0f, 0.0f };
        auto& childListener = child.AddComponent<Luma::AudioListenerComponent>();
        childListener.enabled = true;
        childListener.volume = 0.5f;
        auto& childScript = child.AddComponent<Luma::LuaScriptComponent>();
        childScript.scriptAsset = "Assets/Scripts/PrefabSmoke.lua";
        Luma::ScriptValue stepOverride {};
        stepOverride.type = Luma::ScriptValueType::Float;
        stepOverride.floatValue = 3.0f;
        childScript.propertyOverrides["Step"] = stepOverride;

        const Luma::UUID sourceRootId = root.GetComponent<Luma::IDComponent>().id;
        const Luma::UUID sourceChildId = child.GetComponent<Luma::IDComponent>().id;

        if (!Luma::PrefabSerializer::SerializePrefab(sourceScene, root.GetHandle(), prefabPath, error))
        {
            std::cerr << "Prefab runtime smoke: failed to create prefab: " << error << '\n';
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        Luma::Scene revertScene;
        Luma::EntityID revertRoot = entt::null;
        if (!Luma::PrefabSerializer::InstantiatePrefab(revertScene, prefabPath, &revertRoot, error) || revertRoot == entt::null)
        {
            std::cerr << "Prefab runtime smoke: failed to instantiate prefab for revert path: " << error << '\n';
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        const auto& revertRegistry = revertScene.GetRegistry();
        const auto revertChildren = revertScene.GetChildren(revertRoot);
        if (!ExpectCondition(revertChildren.size() == 1u, "Prefab runtime smoke: expected one child in prefab instance hierarchy."))
        {
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        const Luma::EntityID revertChild = revertChildren.front();
        const auto& revertRootPrefab = revertRegistry.get<Luma::PrefabInstanceComponent>(revertRoot);
        const auto& revertChildPrefab = revertRegistry.get<Luma::PrefabInstanceComponent>(revertChild);
        const auto& revertRootSource = revertRegistry.get<Luma::AudioSourceComponent>(revertRoot);
        const auto& revertChildListener = revertRegistry.get<Luma::AudioListenerComponent>(revertChild);
        const auto& revertChildScript = revertRegistry.get<Luma::LuaScriptComponent>(revertChild);
        const auto revertStepIt = revertChildScript.propertyOverrides.find("Step");

        const bool instantiateValid =
            revertRootPrefab.isRoot &&
            !revertChildPrefab.isRoot &&
            revertRootPrefab.prefabAsset == prefabPath.generic_string() &&
            revertChildPrefab.prefabAsset == prefabPath.generic_string() &&
            revertRootPrefab.sourceEntityId == sourceRootId &&
            revertChildPrefab.sourceEntityId == sourceChildId &&
            revertRootSource.clipAsset == "Assets/Audio/prefab_smoke.wav" &&
            NearlyEqual(revertRootSource.volume, 0.9f) &&
            revertChildListener.enabled &&
            NearlyEqual(revertChildListener.volume, 0.5f) &&
            revertStepIt != revertChildScript.propertyOverrides.end() &&
            NearlyEqual(revertStepIt->second.floatValue, 3.0f);

        if (!ExpectCondition(instantiateValid, "Prefab runtime smoke: instantiated prefab data did not match source expectations."))
        {
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        revertScene.GetRegistry().get<Luma::TransformComponent>(revertRoot).position[0] = 99.0f;
        revertScene.GetRegistry().get<Luma::AudioSourceComponent>(revertRoot).volume = 0.25f;
        revertScene.GetRegistry().get<Luma::LuaScriptComponent>(revertChild).propertyOverrides["Step"].floatValue = 12.0f;
        revertScene.DestroyEntity(revertRoot);

        Luma::EntityID revertedRoot = entt::null;
        if (!Luma::PrefabSerializer::InstantiatePrefab(revertScene, prefabPath, &revertedRoot, error) || revertedRoot == entt::null)
        {
            std::cerr << "Prefab runtime smoke: failed to reinstantiate prefab for revert validation: " << error << '\n';
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        const auto revertedChildren = revertScene.GetChildren(revertedRoot);
        if (!ExpectCondition(revertedChildren.size() == 1u, "Prefab runtime smoke: revert validation instance did not restore child hierarchy."))
        {
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        const auto& revertedRegistry = revertScene.GetRegistry();
        const auto& revertedRootTransform = revertedRegistry.get<Luma::TransformComponent>(revertedRoot);
        const auto& revertedRootSource = revertedRegistry.get<Luma::AudioSourceComponent>(revertedRoot);
        const auto& revertedChildScript = revertedRegistry.get<Luma::LuaScriptComponent>(revertedChildren.front());
        const auto revertedStepIt = revertedChildScript.propertyOverrides.find("Step");
        const bool revertValid =
            NearlyEqual(revertedRootTransform.position[0], 1.0f) &&
            NearlyEqual(revertedRootSource.volume, 0.9f) &&
            revertedStepIt != revertedChildScript.propertyOverrides.end() &&
            NearlyEqual(revertedStepIt->second.floatValue, 3.0f);

        if (!ExpectCondition(revertValid, "Prefab runtime smoke: fresh instantiate did not revert back to prefab source values."))
        {
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        Luma::Scene applyScene;
        Luma::EntityID applyRoot = entt::null;
        if (!Luma::PrefabSerializer::InstantiatePrefab(applyScene, prefabPath, &applyRoot, error) || applyRoot == entt::null)
        {
            std::cerr << "Prefab runtime smoke: failed to instantiate prefab for apply validation: " << error << '\n';
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        auto& applyRegistry = applyScene.GetRegistry();
        auto& applyRootTransform = applyRegistry.get<Luma::TransformComponent>(applyRoot);
        auto& applyRootSource = applyRegistry.get<Luma::AudioSourceComponent>(applyRoot);
        const auto applyChildren = applyScene.GetChildren(applyRoot);
        auto& applyChildScript = applyRegistry.get<Luma::LuaScriptComponent>(applyChildren.front());

        applyRootTransform.position[0] = 21.0f;
        applyRootSource.volume = 0.35f;
        applyChildScript.propertyOverrides["Step"].floatValue = 8.0f;

        if (!Luma::PrefabSerializer::SerializePrefab(applyScene, applyRoot, prefabPath, error))
        {
            std::cerr << "Prefab runtime smoke: failed to apply prefab back to asset: " << error << '\n';
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        Luma::Scene appliedScene;
        Luma::EntityID appliedRoot = entt::null;
        if (!Luma::PrefabSerializer::InstantiatePrefab(appliedScene, prefabPath, &appliedRoot, error) || appliedRoot == entt::null)
        {
            std::cerr << "Prefab runtime smoke: failed to instantiate applied prefab: " << error << '\n';
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        const auto appliedChildren = appliedScene.GetChildren(appliedRoot);
        if (!ExpectCondition(appliedChildren.size() == 1u, "Prefab runtime smoke: applied prefab instance lost child hierarchy."))
        {
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        const auto& appliedRegistry = appliedScene.GetRegistry();
        const auto& appliedRootTransform = appliedRegistry.get<Luma::TransformComponent>(appliedRoot);
        const auto& appliedRootSource = appliedRegistry.get<Luma::AudioSourceComponent>(appliedRoot);
        const auto& appliedChildScript = appliedRegistry.get<Luma::LuaScriptComponent>(appliedChildren.front());
        const auto appliedStepIt = appliedChildScript.propertyOverrides.find("Step");
        const bool applyValid =
            NearlyEqual(appliedRootTransform.position[0], 21.0f) &&
            NearlyEqual(appliedRootSource.volume, 0.35f) &&
            appliedStepIt != appliedChildScript.propertyOverrides.end() &&
            NearlyEqual(appliedStepIt->second.floatValue, 8.0f);

        if (!ExpectCondition(applyValid, "Prefab runtime smoke: applied prefab values did not persist into new instances."))
        {
            std::filesystem::remove_all(tempRoot, ec);
            return false;
        }

        std::filesystem::remove_all(tempRoot, ec);
        return true;
    }

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

            if (loweredArg.rfind("--lua-smoke-test-script=", 0) == 0)
            {
                const std::string value = arg.substr(24);
                args.luaSmokeTestScript = std::filesystem::path(value);
                continue;
            }

            if (loweredArg == "--lua-smoke-test-script" && (i + 1) < argc)
            {
                args.luaSmokeTestScript = std::filesystem::path(argv[++i]);
                continue;
            }

            if (loweredArg == "--lua-runtime-smoke-test")
            {
                args.luaRuntimeSmokeTest = true;
                continue;
            }

            if (loweredArg == "--audio-runtime-smoke-test")
            {
                args.audioRuntimeSmokeTest = true;
                continue;
            }

            if (loweredArg == "--scene-runtime-smoke-test")
            {
                args.sceneRuntimeSmokeTest = true;
                continue;
            }

            if (loweredArg == "--prefab-runtime-smoke-test")
            {
                args.prefabRuntimeSmokeTest = true;
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

        if (launchArgs.luaSmokeTestScript.has_value())
        {
            if (!Luma::ScriptEngine::Initialize())
            {
                Luma::Logger::Shutdown();
                return 1;
            }

            std::string error;
            const bool success = Luma::ScriptEngine::ExecuteFile(*launchArgs.luaSmokeTestScript, &error);
            Luma::ScriptEngine::Shutdown();
            Luma::Logger::Shutdown();
            return success ? 0 : 1;
        }

        if (launchArgs.luaRuntimeSmokeTest)
        {
            const bool success = RunLuaRuntimeSmokeTest();
            Luma::Logger::Shutdown();
            return success ? 0 : 1;
        }

        if (launchArgs.audioRuntimeSmokeTest)
        {
            const bool success = RunAudioRuntimeSmokeTest();
            Luma::Logger::Shutdown();
            return success ? 0 : 1;
        }

        if (launchArgs.sceneRuntimeSmokeTest)
        {
            const bool success = RunSceneRuntimeSmokeTest();
            Luma::Logger::Shutdown();
            return success ? 0 : 1;
        }

        if (launchArgs.prefabRuntimeSmokeTest)
        {
            const bool success = RunPrefabRuntimeSmokeTest();
            Luma::Logger::Shutdown();
            return success ? 0 : 1;
        }

        Luma::ApplicationConfig config;
        config.title = hasProject
                           ? ("Luma - " + Luma::Project::GetConfig().name)
                           : "Project Browser";
        config.width = 1000;
        config.height = 650;
        config.rendererAPI = selection.rendererAPI;
        config.vsyncEnabled = hasProject ? Luma::Project::GetConfig().vsync : true;
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
