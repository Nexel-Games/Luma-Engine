#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "Luma/Asset/Streaming/ResourceStreamingTypes.h"
#include "Luma/RHI/GPUResourceManager.h"

namespace Luma
{
    namespace Assets
    {
        class IResourceStreamingService;
    }

    enum class TextureFormat
    {
        RGBA8
    };

    struct TextureCreateDesc
    {
        std::string debugName;
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        TextureFormat format = TextureFormat::RGBA8;
        bool srgb = true;
        std::filesystem::path sourcePath;
        std::vector<std::uint8_t> pixelData;
    };

    struct TextureMetadata
    {
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        bool srgb = true;
        std::filesystem::path sourcePath;
        std::size_t pixelDataSize = 0;
    };

    class TextureSystem
    {
    public:
        bool Initialize(GPUResourceManager& resourceManager);
        void Shutdown();
        void SetStreamingService(Assets::IResourceStreamingService* streamingService);
        void TickStreaming();

        TextureHandle CreateTexture2D(const TextureCreateDesc& desc);
        TextureHandle CreateStreamedTextureReference(
            const std::filesystem::path& sourcePath,
            bool srgb,
            std::string debugName = {},
            Assets::StreamPriority priority = Assets::StreamPriority::Normal,
            std::uint32_t targetLod = 0);
        TextureHandle CreateSolidColorTexture(const std::string& debugName, const std::array<std::uint8_t, 4>& rgba, bool srgb);
        TextureHandle CreateExternalTextureReference(const std::filesystem::path& sourcePath, bool srgb, std::string debugName = {});
        bool UpdateTexture2D(TextureHandle handle, const TextureCreateDesc& desc);

        bool IsValid(TextureHandle handle) const;
        bool TryGetDesc(TextureHandle handle, TextureCreateDesc& outDesc) const;
        bool TryGetMetadata(TextureHandle handle, TextureMetadata& outMetadata) const;
        bool TryGetStreamRecord(TextureHandle handle, Assets::StreamRecord& outRecord) const;
        std::size_t GetTextureCount() const;
        std::size_t GetStreamingTextureCount() const;

    private:
        struct StreamingTextureState
        {
            std::filesystem::path sourcePath;
            std::string requestKey;
            Assets::StreamPriority priority = Assets::StreamPriority::Normal;
            std::uint32_t targetLod = 0;
            Assets::StreamRequestHandle streamHandle = 0;
            std::uint32_t lastAppliedLod = 0;
            bool requestFailed = false;
            bool uploadCommitted = false;
            bool uploadFailed = false;
        };

        bool m_Initialized = false;
        GPUResourceManager* m_ResourceManager = nullptr;
        Assets::IResourceStreamingService* m_StreamingService = nullptr;
        std::unordered_map<TextureHandle, TextureCreateDesc> m_Textures;
        std::unordered_map<TextureHandle, StreamingTextureState> m_StreamingTextures;
    };
}
