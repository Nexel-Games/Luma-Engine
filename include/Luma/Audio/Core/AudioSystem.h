#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace Luma::Audio
{
    using AudioHandle = std::uint64_t;

    enum class AudioLoadMode : std::uint8_t
    {
        DecompressOnLoad = 0,
        Stream
    };

    struct PlaySettings
    {
        float volume = 1.0f;
        float pitch = 1.0f;
        bool looping = false;
        bool spatialized = false;
        float minDistance = 1.0f;
        float maxDistance = 50.0f;
    };

    class AudioClip
    {
    public:
        AudioClip() = default;
        AudioClip(
            std::filesystem::path path,
            std::filesystem::path resolvedSourcePath,
            const float durationSeconds,
            const std::uint32_t sampleRate,
            const std::uint16_t channels,
            const AudioLoadMode loadMode)
            : m_Path(std::move(path)),
              m_ResolvedSourcePath(std::move(resolvedSourcePath)),
              m_DurationSeconds(durationSeconds),
              m_SampleRate(sampleRate),
              m_Channels(channels),
              m_LoadMode(loadMode)
        {
        }

        const std::filesystem::path& GetPath() const
        {
            return m_Path;
        }

        const std::filesystem::path& GetResolvedSourcePath() const
        {
            return m_ResolvedSourcePath;
        }

        float GetDuration() const
        {
            return m_DurationSeconds;
        }

        std::uint32_t GetSampleRate() const
        {
            return m_SampleRate;
        }

        std::uint16_t GetChannels() const
        {
            return m_Channels;
        }

        AudioLoadMode GetLoadMode() const
        {
            return m_LoadMode;
        }

        bool IsValid() const
        {
            return !m_ResolvedSourcePath.empty();
        }

    private:
        std::filesystem::path m_Path;
        std::filesystem::path m_ResolvedSourcePath;
        float m_DurationSeconds = 0.0f;
        std::uint32_t m_SampleRate = 0;
        std::uint16_t m_Channels = 0;
        AudioLoadMode m_LoadMode = AudioLoadMode::DecompressOnLoad;
    };

    class AudioSystem
    {
    public:
        static bool Initialize();
        static void Shutdown();
        static void Update(float deltaTimeSeconds);

        static bool IsInitialized();
        static bool HasPlaybackDevice();

        static std::shared_ptr<AudioClip> LoadClip(const std::filesystem::path& path);

        static AudioHandle Play2D(
            const std::shared_ptr<AudioClip>& clip,
            const PlaySettings& settings = {});
        static AudioHandle Play3D(
            const std::shared_ptr<AudioClip>& clip,
            const std::array<float, 3>& position,
            const PlaySettings& settings = {});

        static void Stop(AudioHandle handle);
        static void StopAll();
        static void Pause(AudioHandle handle);
        static void Resume(AudioHandle handle);

        static void SetVolume(AudioHandle handle, float volume);
        static void SetPitch(AudioHandle handle, float pitch);
        static void SetPosition(AudioHandle handle, const std::array<float, 3>& position);

        static bool IsPlaying(AudioHandle handle);

        static void SetMasterVolume(float volume);
        static float GetMasterVolume();

        static void SetListenerTransform(
            const std::array<float, 3>& position,
            const std::array<float, 3>& forward,
            const std::array<float, 3>& up);
    };
}
