#include "Luma/Audio/Core/AudioSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <optional>
#include <system_error>
#include <unordered_map>

#include <miniaudio/miniaudio.h>

#include "Luma/Asset/Core/AssetMetaIO.h"
#include "Luma/Core/App/Project.h"
#include "Luma/Core/Foundation/Logging.h"

namespace Luma::Audio
{
    namespace
    {
        struct VoiceState
        {
            AudioHandle handle = 0;
            std::shared_ptr<AudioClip> clip;
            ma_sound sound {};
            bool initialized = false;
            bool looping = false;
            bool paused = false;
            ma_uint64 pausedCursorFrame = 0;
        };

        struct AudioRuntimeState
        {
            ma_engine engine {};
            bool initialized = false;
            bool hasPlaybackDevice = false;
            AudioHandle nextHandle = 1;
            std::mutex mutex;
            std::unordered_map<std::string, std::weak_ptr<AudioClip>> clipCache;
            std::unordered_map<AudioHandle, std::unique_ptr<VoiceState>> voices;
        };

        AudioRuntimeState g_AudioRuntime;

        std::string NormalizePathKey(const std::filesystem::path& path)
        {
            std::string key = path.lexically_normal().generic_string();
#if defined(_WIN32)
            std::transform(
                key.begin(),
                key.end(),
                key.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
#endif
            return key;
        }

        std::string MakeDisplayPath(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return {};
            }

            std::error_code errorCode;
            const std::filesystem::path normalizedPath = path.lexically_normal();
            if (Project::IsLoaded())
            {
                const std::filesystem::path& projectRoot = Project::GetProjectRoot();
                if (!projectRoot.empty())
                {
                    const std::filesystem::path relative = std::filesystem::relative(normalizedPath, projectRoot, errorCode);
                    if (!errorCode)
                    {
                        return relative.generic_string();
                    }
                }
            }

            return normalizedPath.generic_string();
        }

        std::filesystem::path ResolveCandidatePath(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return {};
            }

            std::error_code errorCode;
            if (path.is_absolute())
            {
                return std::filesystem::exists(path, errorCode) ? path.lexically_normal() : std::filesystem::path {};
            }

            std::vector<std::filesystem::path> candidates;
            candidates.push_back(path);

            if (Project::IsLoaded())
            {
                const std::filesystem::path projectRoot = Project::GetProjectRoot();
                const std::filesystem::path assetsPath = Project::GetAssetsPath();
                candidates.push_back(projectRoot / path);
                candidates.push_back(assetsPath / path);

                const std::string genericPath = path.generic_string();
                if (genericPath.rfind("Assets/", 0) == 0)
                {
                    candidates.push_back(projectRoot / path);
                }
            }

            for (const std::filesystem::path& candidate : candidates)
            {
                const std::filesystem::path normalized = candidate.lexically_normal();
                if (std::filesystem::exists(normalized, errorCode))
                {
                    return normalized;
                }
            }

            return {};
        }

        bool TryResolveImportedAudioSourcePath(
            const std::filesystem::path& assetPath,
            std::filesystem::path& outResolvedSourcePath)
        {
            std::vector<std::filesystem::path> metaCandidates;
            metaCandidates.push_back(assetPath);
            metaCandidates.push_back(std::filesystem::path(assetPath.string() + ".meta"));

            for (const std::filesystem::path& metaCandidate : metaCandidates)
            {
                std::error_code errorCode;
                if (!std::filesystem::exists(metaCandidate, errorCode))
                {
                    continue;
                }

                Assets::AssetMeta meta {};
                std::string readError;
                if (!Assets::ReadMetaFile(metaCandidate, meta, readError) || meta.sourcePaths.empty())
                {
                    continue;
                }

                std::filesystem::path sourcePath = meta.sourcePaths.front();
                if (!sourcePath.is_absolute())
                {
                    if (Project::IsLoaded())
                    {
                        sourcePath = (Project::GetProjectRoot() / sourcePath).lexically_normal();
                    }
                    else
                    {
                        sourcePath = (metaCandidate.parent_path() / sourcePath).lexically_normal();
                    }
                }

                if (std::filesystem::exists(sourcePath, errorCode))
                {
                    outResolvedSourcePath = sourcePath;
                    return true;
                }
            }

            return false;
        }

        std::filesystem::path ResolveAudioSourcePath(const std::filesystem::path& path)
        {
            const std::filesystem::path resolvedPath = ResolveCandidatePath(path);
            if (resolvedPath.empty())
            {
                return {};
            }

            if (NormalizePathKey(resolvedPath.extension()) == ".lumaaudio")
            {
                std::filesystem::path sourcePath;
                if (TryResolveImportedAudioSourcePath(resolvedPath, sourcePath))
                {
                    return sourcePath;
                }
            }

            return resolvedPath;
        }

        AudioLoadMode SelectLoadMode(const float durationSeconds)
        {
            return durationSeconds >= 10.0f ? AudioLoadMode::Stream : AudioLoadMode::DecompressOnLoad;
        }

        std::shared_ptr<AudioClip> InspectAudioClip(const std::filesystem::path& requestedPath)
        {
            const std::filesystem::path resolvedSourcePath = ResolveAudioSourcePath(requestedPath);
            if (resolvedSourcePath.empty())
            {
                LUMA_LOG_ERROR("Audio", "Audio clip load failed | Missing source: " + MakeDisplayPath(requestedPath));
                return nullptr;
            }

            ma_decoder decoder {};
            if (ma_decoder_init_file(resolvedSourcePath.string().c_str(), nullptr, &decoder) != MA_SUCCESS)
            {
                LUMA_LOG_ERROR(
                    "Audio",
                    "Audio clip load failed | Could not decode source: " + MakeDisplayPath(resolvedSourcePath));
                return nullptr;
            }

            float durationSeconds = 0.0f;
            (void)ma_data_source_get_length_in_seconds(&decoder, &durationSeconds);
            const std::uint32_t sampleRate = decoder.outputSampleRate;
            const std::uint16_t channels = static_cast<std::uint16_t>(decoder.outputChannels);
            const AudioLoadMode loadMode = SelectLoadMode(durationSeconds);

            ma_decoder_uninit(&decoder);
            return std::make_shared<AudioClip>(
                requestedPath.lexically_normal(),
                resolvedSourcePath.lexically_normal(),
                durationSeconds,
                sampleRate,
                channels,
                loadMode);
        }

        std::uint32_t MakeSoundFlags(const PlaySettings& settings, const AudioClip& clip)
        {
            std::uint32_t flags = 0;
            if (clip.GetLoadMode() == AudioLoadMode::Stream)
            {
                flags |= MA_SOUND_FLAG_STREAM;
            }
            if (!settings.spatialized)
            {
                flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
            }
            return flags;
        }

        AudioHandle PlayInternal(
            const std::shared_ptr<AudioClip>& clip,
            const std::optional<std::array<float, 3>>& position,
            const PlaySettings& settings)
        {
            if (!g_AudioRuntime.initialized || !clip || !clip->IsValid())
            {
                return 0;
            }

            std::lock_guard lock(g_AudioRuntime.mutex);

            auto voice = std::make_unique<VoiceState>();
            voice->handle = g_AudioRuntime.nextHandle++;
            voice->clip = clip;
            voice->looping = settings.looping;

            const ma_result initResult = ma_sound_init_from_file(
                &g_AudioRuntime.engine,
                clip->GetResolvedSourcePath().string().c_str(),
                MakeSoundFlags(settings, *clip),
                nullptr,
                nullptr,
                &voice->sound);
            if (initResult != MA_SUCCESS)
            {
                LUMA_LOG_ERROR(
                    "Audio",
                    "Audio playback init failed | Clip: " + MakeDisplayPath(clip->GetResolvedSourcePath()) +
                        " | Error: " + std::to_string(static_cast<int>(initResult)));
                return 0;
            }

            voice->initialized = true;
            ma_sound_set_volume(&voice->sound, std::max(0.0f, settings.volume));
            ma_sound_set_pitch(&voice->sound, std::max(0.01f, settings.pitch));
            ma_sound_set_looping(&voice->sound, settings.looping ? MA_TRUE : MA_FALSE);
            ma_sound_set_spatialization_enabled(&voice->sound, settings.spatialized ? MA_TRUE : MA_FALSE);
            ma_sound_set_min_distance(&voice->sound, std::max(0.0f, settings.minDistance));
            ma_sound_set_max_distance(&voice->sound, std::max(settings.minDistance, settings.maxDistance));

            if (position.has_value())
            {
                ma_sound_set_position(&voice->sound, (*position)[0], (*position)[1], (*position)[2]);
            }

            const ma_result startResult = ma_sound_start(&voice->sound);
            if (startResult != MA_SUCCESS)
            {
                ma_sound_uninit(&voice->sound);
                LUMA_LOG_ERROR(
                    "Audio",
                    "Audio playback start failed | Clip: " + MakeDisplayPath(clip->GetResolvedSourcePath()) +
                        " | Error: " + std::to_string(static_cast<int>(startResult)));
                return 0;
            }

            const AudioHandle handle = voice->handle;
            g_AudioRuntime.voices.emplace(handle, std::move(voice));
            return handle;
        }
    }

    bool AudioSystem::Initialize()
    {
        if (g_AudioRuntime.initialized)
        {
            return true;
        }

        ma_engine_config config = ma_engine_config_init();
        ma_result result = ma_engine_init(&config, &g_AudioRuntime.engine);
        if (result != MA_SUCCESS)
        {
            config = ma_engine_config_init();
            config.noDevice = MA_TRUE;
            result = ma_engine_init(&config, &g_AudioRuntime.engine);
            if (result != MA_SUCCESS)
            {
                LUMA_LOG_ERROR(
                    "Audio",
                    "Failed to initialize audio engine. Error: " + std::to_string(static_cast<int>(result)));
                return false;
            }

            g_AudioRuntime.hasPlaybackDevice = false;
            g_AudioRuntime.initialized = true;
            LUMA_LOG_WARN("Audio", "Initialized audio engine in no-device mode. Playback is disabled on this machine.");
            return true;
        }

        g_AudioRuntime.hasPlaybackDevice = true;
        g_AudioRuntime.initialized = true;
        LUMA_LOG_INFO("Audio", "Initialized audio engine.");
        return true;
    }

    void AudioSystem::Shutdown()
    {
        if (!g_AudioRuntime.initialized)
        {
            return;
        }

        {
            std::lock_guard lock(g_AudioRuntime.mutex);
            for (auto& [handle, voice] : g_AudioRuntime.voices)
            {
                (void)handle;
                if (voice && voice->initialized)
                {
                    ma_sound_uninit(&voice->sound);
                    voice->initialized = false;
                }
            }
            g_AudioRuntime.voices.clear();
            g_AudioRuntime.clipCache.clear();
        }

        ma_engine_uninit(&g_AudioRuntime.engine);
        g_AudioRuntime.initialized = false;
        g_AudioRuntime.hasPlaybackDevice = false;
        g_AudioRuntime.nextHandle = 1;
        LUMA_LOG_INFO("Audio", "Audio engine shutdown complete.");
    }

    void AudioSystem::Update(float deltaTimeSeconds)
    {
        (void)deltaTimeSeconds;
        if (!g_AudioRuntime.initialized)
        {
            return;
        }

        std::lock_guard lock(g_AudioRuntime.mutex);
        for (auto it = g_AudioRuntime.voices.begin(); it != g_AudioRuntime.voices.end();)
        {
            VoiceState& voice = *it->second;
            if (voice.paused || voice.looping || !voice.initialized)
            {
                ++it;
                continue;
            }

            if (ma_sound_at_end(&voice.sound) && ma_sound_is_playing(&voice.sound) == MA_FALSE)
            {
                ma_sound_uninit(&voice.sound);
                voice.initialized = false;
                it = g_AudioRuntime.voices.erase(it);
                continue;
            }

            ++it;
        }
    }

    bool AudioSystem::IsInitialized()
    {
        return g_AudioRuntime.initialized;
    }

    bool AudioSystem::HasPlaybackDevice()
    {
        return g_AudioRuntime.initialized && g_AudioRuntime.hasPlaybackDevice;
    }

    std::shared_ptr<AudioClip> AudioSystem::LoadClip(const std::filesystem::path& path)
    {
        const std::string cacheKey = NormalizePathKey(path);
        {
            std::lock_guard lock(g_AudioRuntime.mutex);
            if (const auto it = g_AudioRuntime.clipCache.find(cacheKey); it != g_AudioRuntime.clipCache.end())
            {
                if (std::shared_ptr<AudioClip> cached = it->second.lock())
                {
                    return cached;
                }
                g_AudioRuntime.clipCache.erase(it);
            }
        }

        std::shared_ptr<AudioClip> clip = InspectAudioClip(path);
        if (!clip)
        {
            return nullptr;
        }

        std::lock_guard lock(g_AudioRuntime.mutex);
        g_AudioRuntime.clipCache[cacheKey] = clip;
        return clip;
    }

    AudioHandle AudioSystem::Play2D(const std::shared_ptr<AudioClip>& clip, const PlaySettings& settings)
    {
        PlaySettings effectiveSettings = settings;
        effectiveSettings.spatialized = false;
        return PlayInternal(clip, std::nullopt, effectiveSettings);
    }

    AudioHandle AudioSystem::Play3D(
        const std::shared_ptr<AudioClip>& clip,
        const std::array<float, 3>& position,
        const PlaySettings& settings)
    {
        PlaySettings effectiveSettings = settings;
        effectiveSettings.spatialized = true;
        return PlayInternal(clip, position, effectiveSettings);
    }

    void AudioSystem::Stop(const AudioHandle handle)
    {
        if (!g_AudioRuntime.initialized || handle == 0)
        {
            return;
        }

        std::lock_guard lock(g_AudioRuntime.mutex);
        const auto it = g_AudioRuntime.voices.find(handle);
        if (it == g_AudioRuntime.voices.end())
        {
            return;
        }

        if (it->second->initialized)
        {
            ma_sound_stop(&it->second->sound);
            ma_sound_uninit(&it->second->sound);
            it->second->initialized = false;
        }
        g_AudioRuntime.voices.erase(it);
    }

    void AudioSystem::StopAll()
    {
        if (!g_AudioRuntime.initialized)
        {
            return;
        }

        std::lock_guard lock(g_AudioRuntime.mutex);
        for (auto& [handle, voice] : g_AudioRuntime.voices)
        {
            (void)handle;
            if (voice && voice->initialized)
            {
                ma_sound_stop(&voice->sound);
                ma_sound_uninit(&voice->sound);
                voice->initialized = false;
            }
        }
        g_AudioRuntime.voices.clear();
    }

    void AudioSystem::Pause(const AudioHandle handle)
    {
        if (!g_AudioRuntime.initialized || handle == 0)
        {
            return;
        }

        std::lock_guard lock(g_AudioRuntime.mutex);
        const auto it = g_AudioRuntime.voices.find(handle);
        if (it == g_AudioRuntime.voices.end() || !it->second->initialized || it->second->paused)
        {
            return;
        }

        ma_sound_get_cursor_in_pcm_frames(&it->second->sound, &it->second->pausedCursorFrame);
        ma_sound_stop(&it->second->sound);
        it->second->paused = true;
    }

    void AudioSystem::Resume(const AudioHandle handle)
    {
        if (!g_AudioRuntime.initialized || handle == 0)
        {
            return;
        }

        std::lock_guard lock(g_AudioRuntime.mutex);
        const auto it = g_AudioRuntime.voices.find(handle);
        if (it == g_AudioRuntime.voices.end() || !it->second->initialized || !it->second->paused)
        {
            return;
        }

        ma_sound_seek_to_pcm_frame(&it->second->sound, it->second->pausedCursorFrame);
        ma_sound_start(&it->second->sound);
        it->second->paused = false;
    }

    void AudioSystem::SetVolume(const AudioHandle handle, const float volume)
    {
        if (!g_AudioRuntime.initialized || handle == 0)
        {
            return;
        }

        std::lock_guard lock(g_AudioRuntime.mutex);
        if (const auto it = g_AudioRuntime.voices.find(handle);
            it != g_AudioRuntime.voices.end() && it->second->initialized)
        {
            ma_sound_set_volume(&it->second->sound, std::max(0.0f, volume));
        }
    }

    void AudioSystem::SetPitch(const AudioHandle handle, const float pitch)
    {
        if (!g_AudioRuntime.initialized || handle == 0)
        {
            return;
        }

        std::lock_guard lock(g_AudioRuntime.mutex);
        if (const auto it = g_AudioRuntime.voices.find(handle);
            it != g_AudioRuntime.voices.end() && it->second->initialized)
        {
            ma_sound_set_pitch(&it->second->sound, std::max(0.01f, pitch));
        }
    }

    void AudioSystem::SetPosition(const AudioHandle handle, const std::array<float, 3>& position)
    {
        if (!g_AudioRuntime.initialized || handle == 0)
        {
            return;
        }

        std::lock_guard lock(g_AudioRuntime.mutex);
        if (const auto it = g_AudioRuntime.voices.find(handle);
            it != g_AudioRuntime.voices.end() && it->second->initialized)
        {
            ma_sound_set_position(&it->second->sound, position[0], position[1], position[2]);
        }
    }

    bool AudioSystem::IsPlaying(const AudioHandle handle)
    {
        if (!g_AudioRuntime.initialized || handle == 0)
        {
            return false;
        }

        std::lock_guard lock(g_AudioRuntime.mutex);
        if (const auto it = g_AudioRuntime.voices.find(handle);
            it != g_AudioRuntime.voices.end() && it->second->initialized)
        {
            return !it->second->paused && ma_sound_is_playing(&it->second->sound) == MA_TRUE;
        }

        return false;
    }

    void AudioSystem::SetMasterVolume(const float volume)
    {
        if (!g_AudioRuntime.initialized)
        {
            return;
        }

        ma_engine_set_volume(&g_AudioRuntime.engine, std::max(0.0f, volume));
    }

    float AudioSystem::GetMasterVolume()
    {
        if (!g_AudioRuntime.initialized)
        {
            return 1.0f;
        }

        return ma_engine_get_volume(&g_AudioRuntime.engine);
    }

    void AudioSystem::SetListenerTransform(
        const std::array<float, 3>& position,
        const std::array<float, 3>& forward,
        const std::array<float, 3>& up)
    {
        if (!g_AudioRuntime.initialized)
        {
            return;
        }

        ma_engine_listener_set_position(&g_AudioRuntime.engine, 0, position[0], position[1], position[2]);
        ma_engine_listener_set_direction(&g_AudioRuntime.engine, 0, forward[0], forward[1], forward[2]);
        ma_engine_listener_set_world_up(&g_AudioRuntime.engine, 0, up[0], up[1], up[2]);
    }
}
