#include "Luma/RHI/TextureSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <stb_image.h>

#ifndef TINYEXR_USE_MINIZ
#define TINYEXR_USE_MINIZ (0)
#endif
#ifndef TINYEXR_USE_STB_ZLIB
#define TINYEXR_USE_STB_ZLIB (1)
#endif
#include <tinyexr.h>

#include "Luma/Asset/Streaming/IResourceStreamingService.h"

namespace Luma
{
    namespace
    {
        constexpr std::size_t kMaxTextureStreamRequestsPerTick = 6;
        constexpr std::size_t kMaxTextureUploadsPerTick = 2;

        std::string ToLowerCopy(std::string value)
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

        bool IsLdrTextureExtension(const std::string_view extension)
        {
            return extension == ".png" ||
                   extension == ".jpg" ||
                   extension == ".jpeg" ||
                   extension == ".bmp" ||
                   extension == ".tga";
        }

        bool IsHdrTextureExtension(const std::string_view extension)
        {
            return extension == ".hdr" || extension == ".exr";
        }

        bool IsIntermediateTextureExtension(const std::string_view extension)
        {
            return extension == ".lumatex" || extension == ".lumasky";
        }

        std::uint8_t LinearToSRGB8(const float linearValue)
        {
            const float clamped = std::clamp(linearValue, 0.0f, 1.0f);
            const float srgb = std::pow(clamped, 1.0f / 2.2f);
            return static_cast<std::uint8_t>(std::clamp(srgb * 255.0f, 0.0f, 255.0f));
        }

        void ToneMapToRGBA8(
            const float* linearPixels,
            const int width,
            const int height,
            const int componentStride,
            std::vector<std::uint8_t>& outPixels)
        {
            outPixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4ull);
            const std::size_t pixelCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
            for (std::size_t i = 0; i < pixelCount; ++i)
            {
                const float r = std::max(0.0f, linearPixels[i * static_cast<std::size_t>(componentStride) + 0]);
                const float g = std::max(0.0f, linearPixels[i * static_cast<std::size_t>(componentStride) + 1]);
                const float b = std::max(0.0f, linearPixels[i * static_cast<std::size_t>(componentStride) + 2]);

                const float mappedR = r / (1.0f + r);
                const float mappedG = g / (1.0f + g);
                const float mappedB = b / (1.0f + b);
                outPixels[i * 4 + 0] = LinearToSRGB8(mappedR);
                outPixels[i * 4 + 1] = LinearToSRGB8(mappedG);
                outPixels[i * 4 + 2] = LinearToSRGB8(mappedB);
                outPixels[i * 4 + 3] = 255;
            }
        }

        bool DecodeExrToRGBA8(
            const std::uint8_t* exrBytes,
            const std::size_t exrSize,
            std::vector<std::uint8_t>& outPixels,
            int& outWidth,
            int& outHeight)
        {
            float* exrRgba = nullptr;
            const char* exrError = nullptr;
            const int result = LoadEXRFromMemory(&exrRgba, &outWidth, &outHeight, exrBytes, exrSize, &exrError);
            if (result != TINYEXR_SUCCESS || exrRgba == nullptr || outWidth <= 0 || outHeight <= 0)
            {
                if (exrError != nullptr)
                {
                    FreeEXRErrorMessage(exrError);
                }
                return false;
            }

            ToneMapToRGBA8(exrRgba, outWidth, outHeight, 4, outPixels);
            std::free(exrRgba);
            return true;
        }

        bool DecodeLdrToRGBA8FromMemory(
            const std::uint8_t* sourceBytes,
            const std::size_t sourceSize,
            std::vector<std::uint8_t>& outPixels,
            int& outWidth,
            int& outHeight)
        {
            int channels = 0;
            stbi_uc* pixels = stbi_load_from_memory(
                sourceBytes,
                static_cast<int>(sourceSize),
                &outWidth,
                &outHeight,
                &channels,
                STBI_rgb_alpha);
            if (pixels == nullptr || outWidth <= 0 || outHeight <= 0)
            {
                return false;
            }

            outPixels.assign(
                pixels,
                pixels + static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight) * 4ull);
            stbi_image_free(pixels);
            return true;
        }

        bool DecodeHdrToRGBA8FromMemory(
            const std::uint8_t* sourceBytes,
            const std::size_t sourceSize,
            const bool exrPreferred,
            std::vector<std::uint8_t>& outPixels,
            int& outWidth,
            int& outHeight)
        {
            if (exrPreferred && DecodeExrToRGBA8(sourceBytes, sourceSize, outPixels, outWidth, outHeight))
            {
                return true;
            }

            int channels = 0;
            float* pixels = stbi_loadf_from_memory(
                sourceBytes,
                static_cast<int>(sourceSize),
                &outWidth,
                &outHeight,
                &channels,
                3);
            if (pixels != nullptr && outWidth > 0 && outHeight > 0)
            {
                ToneMapToRGBA8(pixels, outWidth, outHeight, 3, outPixels);
                stbi_image_free(pixels);
                return true;
            }

            if (!exrPreferred)
            {
                return DecodeExrToRGBA8(sourceBytes, sourceSize, outPixels, outWidth, outHeight);
            }

            return false;
        }

        struct IntermediateSourceData
        {
            const std::uint8_t* sourceBytes = nullptr;
            std::size_t sourceSize = 0;
            std::string sourceName;
        };

        bool LoadIntermediateSourceData(
            const std::uint8_t* payloadBytes,
            const std::size_t payloadSize,
            IntermediateSourceData& outData)
        {
            outData = {};
            if (payloadBytes == nullptr || payloadSize == 0)
            {
                return false;
            }

            const std::string_view payloadView(
                reinterpret_cast<const char*>(payloadBytes),
                payloadSize);

            std::size_t headerEnd = payloadView.find("\n\n");
            std::size_t separatorLength = 2;
            if (headerEnd == std::string_view::npos)
            {
                headerEnd = payloadView.find("\r\n\r\n");
                separatorLength = 4;
            }
            if (headerEnd == std::string_view::npos)
            {
                return false;
            }

            std::size_t lineStart = 0;
            while (lineStart < headerEnd)
            {
                std::size_t lineEnd = payloadView.find('\n', lineStart);
                if (lineEnd == std::string_view::npos || lineEnd > headerEnd)
                {
                    lineEnd = headerEnd;
                }

                std::string line(payloadView.substr(lineStart, lineEnd - lineStart));
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (line.rfind("Source=", 0) == 0)
                {
                    outData.sourceName = line.substr(std::strlen("Source="));
                    break;
                }

                lineStart = lineEnd + 1;
            }

            const std::size_t sourceOffset = headerEnd + separatorLength;
            if (sourceOffset >= payloadSize)
            {
                return false;
            }

            outData.sourceBytes = payloadBytes + sourceOffset;
            outData.sourceSize = payloadSize - sourceOffset;
            return outData.sourceSize > 0;
        }

        bool DecodeTexturePayloadToRGBA8(
            const std::filesystem::path& sourcePath,
            const std::vector<std::uint8_t>& payloadBytes,
            std::vector<std::uint8_t>& outPixels,
            int& outWidth,
            int& outHeight)
        {
            if (payloadBytes.empty())
            {
                return false;
            }

            const std::string extension = ToLowerCopy(sourcePath.extension().string());
            if (IsIntermediateTextureExtension(extension))
            {
                IntermediateSourceData intermediateSource;
                if (!LoadIntermediateSourceData(payloadBytes.data(), payloadBytes.size(), intermediateSource))
                {
                    return false;
                }

                const std::string sourceExtension =
                    ToLowerCopy(std::filesystem::path(intermediateSource.sourceName).extension().string());
                if (IsHdrTextureExtension(sourceExtension))
                {
                    return DecodeHdrToRGBA8FromMemory(
                        intermediateSource.sourceBytes,
                        intermediateSource.sourceSize,
                        sourceExtension == ".exr",
                        outPixels,
                        outWidth,
                        outHeight);
                }

                return IsLdrTextureExtension(sourceExtension) &&
                    DecodeLdrToRGBA8FromMemory(
                        intermediateSource.sourceBytes,
                        intermediateSource.sourceSize,
                        outPixels,
                        outWidth,
                        outHeight);
            }

            if (IsHdrTextureExtension(extension))
            {
                return DecodeHdrToRGBA8FromMemory(
                    payloadBytes.data(),
                    payloadBytes.size(),
                    extension == ".exr",
                    outPixels,
                    outWidth,
                    outHeight);
            }

            return IsLdrTextureExtension(extension) &&
                DecodeLdrToRGBA8FromMemory(
                    payloadBytes.data(),
                    payloadBytes.size(),
                    outPixels,
                    outWidth,
                    outHeight);
        }
    }

    bool TextureSystem::Initialize(GPUResourceManager& resourceManager)
    {
        m_ResourceManager = &resourceManager;
        m_Initialized = true;
        return true;
    }

    void TextureSystem::Shutdown()
    {
        m_StreamingTextures.clear();
        m_StreamingService = nullptr;

        if (m_ResourceManager != nullptr)
        {
            for (const auto& [handle, _] : m_Textures)
            {
                m_ResourceManager->DestroyTexture(handle, GPUResourceManager::DestroyMode::Deferred);
            }
        }

        m_Textures.clear();
        m_ResourceManager = nullptr;
        m_Initialized = false;
    }

    void TextureSystem::SetStreamingService(Assets::IResourceStreamingService* streamingService)
    {
        m_StreamingService = streamingService;
    }

    void TextureSystem::TickStreaming()
    {
        if (!m_Initialized || m_ResourceManager == nullptr || m_StreamingService == nullptr || m_StreamingTextures.empty())
        {
            return;
        }

        std::size_t requestsIssuedThisTick = 0;
        for (auto& [textureHandle, streamingState] : m_StreamingTextures)
        {
            if (requestsIssuedThisTick >= kMaxTextureStreamRequestsPerTick)
            {
                break;
            }

            if (textureHandle == InvalidTextureHandle ||
                streamingState.streamHandle != 0 ||
                streamingState.requestFailed ||
                streamingState.sourcePath.empty())
            {
                continue;
            }

            Assets::StreamRequestDesc request {};
            request.key = streamingState.requestKey.empty()
                ? "texture:" + streamingState.sourcePath.generic_string()
                : streamingState.requestKey;
            request.sourcePath = streamingState.sourcePath;
            request.resourceType = Assets::StreamResourceType::Texture;
            request.priority = streamingState.priority;
            request.lod.mode =
                streamingState.targetLod > 0 ? Assets::LODStreamingMode::Explicit : Assets::LODStreamingMode::Disabled;
            request.lod.targetLod = streamingState.targetLod;
            request.estimatedCpuBytes = 4;
            request.estimatedGpuBytes = 4;

            std::string error;
            streamingState.streamHandle = m_StreamingService->Request(request, error);
            if (streamingState.streamHandle == 0)
            {
                streamingState.requestFailed = true;
                continue;
            }

            ++requestsIssuedThisTick;
        }

        std::size_t uploadsCommittedThisTick = 0;
        for (auto& [textureHandle, streamingState] : m_StreamingTextures)
        {
            if (streamingState.streamHandle == 0)
            {
                continue;
            }

            Assets::StreamRecord record {};
            if (!m_StreamingService->TryGetRecord(streamingState.streamHandle, record))
            {
                continue;
            }

            if (record.state != Assets::StreamState::Resident)
            {
                if (record.state == Assets::StreamState::Queued || record.state == Assets::StreamState::Streaming)
                {
                    streamingState.uploadCommitted = false;
                    streamingState.uploadFailed = false;
                }
                continue;
            }

            if (streamingState.uploadCommitted && streamingState.lastAppliedLod == record.resolvedLod)
            {
                continue;
            }

            if (uploadsCommittedThisTick >= kMaxTextureUploadsPerTick)
            {
                continue;
            }

            Assets::StreamPayload payload {};
            if (!m_StreamingService->TryGetPayload(streamingState.streamHandle, payload))
            {
                continue;
            }

            auto descIt = m_Textures.find(textureHandle);
            if (descIt == m_Textures.end())
            {
                continue;
            }

            if (streamingState.uploadFailed && streamingState.lastAppliedLod == record.resolvedLod)
            {
                continue;
            }

            int width = 0;
            int height = 0;
            std::vector<std::uint8_t> decodedPixels;
            const std::filesystem::path decodePath =
                descIt->second.sourcePath.empty() ? payload.resolvedSourcePath : descIt->second.sourcePath;
            if (!DecodeTexturePayloadToRGBA8(decodePath, payload.bytes, decodedPixels, width, height))
            {
                streamingState.uploadFailed = true;
                streamingState.lastAppliedLod = record.resolvedLod;
                continue;
            }

            TextureCreateDesc& textureDesc = descIt->second;
            textureDesc.width = static_cast<std::uint32_t>(width);
            textureDesc.height = static_cast<std::uint32_t>(height);
            textureDesc.sourcePath = payload.resolvedSourcePath.empty() ? textureDesc.sourcePath : payload.resolvedSourcePath;
            textureDesc.pixelData = std::move(decodedPixels);

            TextureDesc gpuDesc;
            gpuDesc.debugName = textureDesc.debugName;
            gpuDesc.width = textureDesc.width;
            gpuDesc.height = textureDesc.height;
            gpuDesc.srgb = textureDesc.srgb;
            gpuDesc.pixelData = textureDesc.pixelData;
            m_ResourceManager->UpdateTexture(textureHandle, gpuDesc);

            streamingState.uploadCommitted = true;
            streamingState.uploadFailed = false;
            streamingState.lastAppliedLod = record.resolvedLod;
            ++uploadsCommittedThisTick;
        }
    }

    TextureHandle TextureSystem::CreateTexture2D(const TextureCreateDesc& desc)
    {
        if (!m_Initialized || m_ResourceManager == nullptr || desc.width == 0 || desc.height == 0)
        {
            return InvalidTextureHandle;
        }

        const std::size_t expectedSize = static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height) * 4ULL;
        if (!desc.pixelData.empty() && desc.pixelData.size() != expectedSize)
        {
            return InvalidTextureHandle;
        }

        TextureCreateDesc storedDesc = desc;
        if (storedDesc.pixelData.empty())
        {
            storedDesc.pixelData.resize(expectedSize, 0);
        }

        TextureDesc gpuDesc;
        gpuDesc.debugName = storedDesc.debugName;
        gpuDesc.width = storedDesc.width;
        gpuDesc.height = storedDesc.height;
        gpuDesc.srgb = storedDesc.srgb;
        gpuDesc.pixelData = storedDesc.pixelData;

        const TextureHandle handle = m_ResourceManager->CreateTexture(gpuDesc, storedDesc.debugName);
        if (handle == InvalidTextureHandle)
        {
            return InvalidTextureHandle;
        }

        m_Textures.emplace(handle, std::move(storedDesc));
        return handle;
    }

    TextureHandle TextureSystem::CreateStreamedTextureReference(
        const std::filesystem::path& sourcePath,
        const bool srgb,
        std::string debugName,
        const Assets::StreamPriority priority,
        const std::uint32_t targetLod)
    {
        if (sourcePath.empty())
        {
            return InvalidTextureHandle;
        }

        if (debugName.empty())
        {
            debugName = sourcePath.filename().string();
        }

        TextureCreateDesc desc;
        desc.debugName = std::move(debugName);
        desc.width = 1;
        desc.height = 1;
        desc.format = TextureFormat::RGBA8;
        desc.srgb = srgb;
        desc.sourcePath = sourcePath;
        desc.pixelData = { 255, 255, 255, 255 };
        const TextureHandle handle = CreateTexture2D(desc);
        if (handle == InvalidTextureHandle)
        {
            return InvalidTextureHandle;
        }

        if (m_StreamingService == nullptr)
        {
            return handle;
        }

        Assets::StreamRequestDesc request {};
        request.key = "texture:" + sourcePath.generic_string();
        m_StreamingTextures[handle] = StreamingTextureState {
            .sourcePath = sourcePath,
            .requestKey = std::move(request.key),
            .priority = priority,
            .targetLod = targetLod,
            .streamHandle = 0,
            .lastAppliedLod = 0,
            .requestFailed = false,
            .uploadCommitted = false,
            .uploadFailed = false };

        return handle;
    }

    TextureHandle TextureSystem::CreateSolidColorTexture(
        const std::string& debugName,
        const std::array<std::uint8_t, 4>& rgba,
        const bool srgb)
    {
        TextureCreateDesc desc;
        desc.debugName = debugName;
        desc.width = 1;
        desc.height = 1;
        desc.format = TextureFormat::RGBA8;
        desc.srgb = srgb;
        desc.pixelData = { rgba[0], rgba[1], rgba[2], rgba[3] };
        return CreateTexture2D(desc);
    }

    TextureHandle TextureSystem::CreateExternalTextureReference(
        const std::filesystem::path& sourcePath,
        const bool srgb,
        std::string debugName)
    {
        if (sourcePath.empty())
        {
            return InvalidTextureHandle;
        }

        if (m_StreamingService != nullptr)
        {
            return CreateStreamedTextureReference(sourcePath, srgb, std::move(debugName));
        }

        if (debugName.empty())
        {
            debugName = sourcePath.filename().string();
        }

        TextureCreateDesc desc;
        desc.debugName = std::move(debugName);
        desc.width = 1;
        desc.height = 1;
        desc.format = TextureFormat::RGBA8;
        desc.srgb = srgb;
        desc.sourcePath = sourcePath;
        desc.pixelData = { 255, 255, 255, 255 };
        return CreateTexture2D(desc);
    }

    bool TextureSystem::UpdateTexture2D(const TextureHandle handle, const TextureCreateDesc& desc)
    {
        if (!m_Initialized || m_ResourceManager == nullptr || handle == InvalidTextureHandle ||
            desc.width == 0 || desc.height == 0)
        {
            return false;
        }

        auto it = m_Textures.find(handle);
        if (it == m_Textures.end())
        {
            return false;
        }

        const std::size_t expectedSize = static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height) * 4ULL;
        if (desc.pixelData.size() != expectedSize)
        {
            return false;
        }

        TextureCreateDesc storedDesc = desc;
        TextureDesc gpuDesc;
        gpuDesc.debugName = storedDesc.debugName;
        gpuDesc.width = storedDesc.width;
        gpuDesc.height = storedDesc.height;
        gpuDesc.srgb = storedDesc.srgb;
        gpuDesc.pixelData = storedDesc.pixelData;
        m_ResourceManager->UpdateTexture(handle, gpuDesc);

        it->second = std::move(storedDesc);
        m_StreamingTextures.erase(handle);
        return true;
    }

    bool TextureSystem::IsValid(const TextureHandle handle) const
    {
        return m_Textures.find(handle) != m_Textures.end();
    }

    bool TextureSystem::TryGetDesc(const TextureHandle handle, TextureCreateDesc& outDesc) const
    {
        const auto it = m_Textures.find(handle);
        if (it == m_Textures.end())
        {
            return false;
        }

        outDesc = it->second;
        return true;
    }

    bool TextureSystem::TryGetMetadata(const TextureHandle handle, TextureMetadata& outMetadata) const
    {
        const auto it = m_Textures.find(handle);
        if (it == m_Textures.end())
        {
            return false;
        }

        outMetadata.width = it->second.width;
        outMetadata.height = it->second.height;
        outMetadata.srgb = it->second.srgb;
        outMetadata.sourcePath = it->second.sourcePath;
        outMetadata.pixelDataSize = it->second.pixelData.size();
        return true;
    }

    bool TextureSystem::TryGetStreamRecord(const TextureHandle handle, Assets::StreamRecord& outRecord) const
    {
        if (m_StreamingService == nullptr)
        {
            return false;
        }

        const auto it = m_StreamingTextures.find(handle);
        if (it == m_StreamingTextures.end() || it->second.streamHandle == 0)
        {
            return false;
        }

        return m_StreamingService->TryGetRecord(it->second.streamHandle, outRecord);
    }

    std::size_t TextureSystem::GetTextureCount() const
    {
        return m_Textures.size();
    }

    std::size_t TextureSystem::GetStreamingTextureCount() const
    {
        return m_StreamingTextures.size();
    }
}
