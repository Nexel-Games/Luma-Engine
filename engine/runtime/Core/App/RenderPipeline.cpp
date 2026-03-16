#include "Luma/Core/App/RenderPipeline.h"

#include <array>
#include <cstddef>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Luma/RHI/IRenderBackend.h"
#include "Luma/RHI/LightingSystem.h"
#include "Luma/RHI/RenderGraph.h"
#include "Luma/RHI/ShaderSystem.h"
#include "Luma/RHI/TextureSystem.h"
#include "Luma/Asset/Streaming/IResourceStreamingService.h"

namespace Luma
{
    namespace
    {
        struct TriangleVertex
        {
            float position[3];
            float color[3];
            float uv[2];
        };

        enum class SceneBlendMode : std::uint32_t
        {
            Opaque = 0,
            Masked = 1,
            Translucent = 2,
            Additive = 3,
            Modulate = 4
        };

        struct PerDrawData
        {
            float viewProjection[16] = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
            float worldTransform[16] = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
            float tint[4];
            float emissiveColorIntensity[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            float surfaceParameters[4] = { 0.0f, 0.5f, 0.5f, 1.0f };
            float opacityAndNormal[4] = { 1.0f, 0.333f, 1.0f, 0.0f };
            float uvTransform0[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
            float uvTransform1[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            float materialParameters2[4] = { 1.0f, 0.0f, 1.0f, 0.0f };
            float subsurfaceAndCoat[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
            float materialParameters3[4] = { 0.1f, 0.0f, 0.0f, 0.0f };
            float lightmapParams[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
        };

        struct FloatImage
        {
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            std::vector<float> pixels;
        };

        inline void HashCombine(std::size_t& seed, const std::size_t value)
        {
            seed ^= value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        }

        Color ToColor(const std::array<float, 4>& value)
        {
            return { value[0], value[1], value[2], value[3] };
        }

        bool ColorsDiffer(const Color& lhs, const Color& rhs, const float epsilon = 1.0e-5f)
        {
            return
                std::abs(lhs.r - rhs.r) > epsilon ||
                std::abs(lhs.g - rhs.g) > epsilon ||
                std::abs(lhs.b - rhs.b) > epsilon ||
                std::abs(lhs.a - rhs.a) > epsilon;
        }

        struct ShadowPerDrawData
        {
            float viewProjection[16] = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
            float worldTransform[16] = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
            float uvTransform0[4] = { 1.0f, 1.0f, 0.0f, 0.0f };
            float shadowMaterialParams[4] = { 0.0f, 1.0f, 0.333f, 0.0f };
        };

        struct Vec3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct Mat4
        {
            std::array<float, 16> elements = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
        };

        Vec3 operator+(const Vec3& lhs, const Vec3& rhs)
        {
            return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
        }

        Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
        {
            return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
        }

        Vec3 operator*(const Vec3& value, const float scalar)
        {
            return { value.x * scalar, value.y * scalar, value.z * scalar };
        }

        float Dot(const Vec3& lhs, const Vec3& rhs)
        {
            return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
        }

        float Luminance(const std::array<float, 3>& color)
        {
            return
                color[0] * 0.2126f +
                color[1] * 0.7152f +
                color[2] * 0.0722f;
        }

        Vec3 Cross(const Vec3& lhs, const Vec3& rhs)
        {
            return {
                lhs.y * rhs.z - lhs.z * rhs.y,
                lhs.z * rhs.x - lhs.x * rhs.z,
                lhs.x * rhs.y - lhs.y * rhs.x
            };
        }

        Vec3 Normalize(const Vec3& value)
        {
            const float lengthSq = Dot(value, value);
            if (lengthSq <= 1.0e-8f)
            {
                return { 0.0f, 0.0f, 0.0f };
            }

            const float invLength = 1.0f / std::sqrt(lengthSq);
            return { value.x * invLength, value.y * invLength, value.z * invLength };
        }

        Mat4 Multiply(const Mat4& lhs, const Mat4& rhs)
        {
            Mat4 result {};
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    float value = 0.0f;
                    for (int k = 0; k < 4; ++k)
                    {
                        value += lhs.elements[k * 4 + row] * rhs.elements[column * 4 + k];
                    }
                    result.elements[column * 4 + row] = value;
                }
            }
            return result;
        }

        Mat4 BuildLookAt(const Vec3& eye, const Vec3& center, const Vec3& worldUp)
        {
            const Vec3 forward = Normalize(center - eye);
            const Vec3 right = Normalize(Cross(forward, worldUp));
            const Vec3 up = Cross(right, forward);

            Mat4 result {};
            result.elements = {
                right.x, up.x, -forward.x, 0.0f,
                right.y, up.y, -forward.y, 0.0f,
                right.z, up.z, -forward.z, 0.0f,
                -Dot(right, eye), -Dot(up, eye), Dot(forward, eye), 1.0f
            };
            return result;
        }

        Mat4 BuildPerspective(const float fovRadians, const float aspectRatio, const float nearPlane, const float farPlane)
        {
            Mat4 result {};
            result.elements.fill(0.0f);
            const float tanHalfFov = std::tan(fovRadians * 0.5f);
            if (std::abs(tanHalfFov) <= 1.0e-6f || std::abs(aspectRatio) <= 1.0e-6f)
            {
                return Mat4 {};
            }

            const float f = 1.0f / tanHalfFov;
            result.elements[0] = f / aspectRatio;
            result.elements[5] = f;
            result.elements[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
            result.elements[11] = -1.0f;
            result.elements[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
            return result;
        }

        Mat4 BuildPerspectiveZeroToOne(
            const float fovRadians,
            const float aspectRatio,
            const float nearPlane,
            const float farPlane)
        {
            Mat4 result {};
            result.elements.fill(0.0f);
            const float tanHalfFov = std::tan(fovRadians * 0.5f);
            if (std::abs(tanHalfFov) <= 1.0e-6f || std::abs(aspectRatio) <= 1.0e-6f)
            {
                return Mat4 {};
            }

            const float f = 1.0f / tanHalfFov;
            result.elements[0] = f / aspectRatio;
            result.elements[5] = f;
            result.elements[10] = farPlane / (nearPlane - farPlane);
            result.elements[11] = -1.0f;
            result.elements[14] = (farPlane * nearPlane) / (nearPlane - farPlane);
            return result;
        }

        Mat4 BuildOrthographic(
            const float left,
            const float right,
            const float bottom,
            const float top,
            const float nearPlane,
            const float farPlane)
        {
            Mat4 result {};
            result.elements.fill(0.0f);
            result.elements[0] = 2.0f / (right - left);
            result.elements[5] = 2.0f / (top - bottom);
            result.elements[10] = -2.0f / (farPlane - nearPlane);
            result.elements[12] = -(right + left) / (right - left);
            result.elements[13] = -(top + bottom) / (top - bottom);
            result.elements[14] = -(farPlane + nearPlane) / (farPlane - nearPlane);
            result.elements[15] = 1.0f;
            return result;
        }

        Mat4 BuildOrthographicZeroToOne(
            const float left,
            const float right,
            const float bottom,
            const float top,
            const float nearPlane,
            const float farPlane)
        {
            Mat4 result {};
            result.elements.fill(0.0f);
            result.elements[0] = 2.0f / (right - left);
            result.elements[5] = 2.0f / (top - bottom);
            result.elements[10] = -1.0f / (farPlane - nearPlane);
            result.elements[12] = -(right + left) / (right - left);
            result.elements[13] = -(top + bottom) / (top - bottom);
            result.elements[14] = -nearPlane / (farPlane - nearPlane);
            result.elements[15] = 1.0f;
            return result;
        }

        Mat4 BuildShadowTextureMatrix(const RendererAPI api)
        {
            (void)api;
            Mat4 bias {};
            bias.elements = {
                0.5f, 0.0f, 0.0f, 0.0f,
                0.0f, 0.5f, 0.0f, 0.0f,
                0.0f, 0.0f, 0.5f, 0.0f,
                0.5f, 0.5f, 0.5f, 1.0f
            };
            return bias;
        }

        Mat4 BuildAtlasTransform(
            const float offsetX,
            const float offsetY,
            const float scaleX,
            const float scaleY)
        {
            Mat4 result {};
            result.elements = {
                scaleX, 0.0f, 0.0f, 0.0f,
                0.0f, scaleY, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                offsetX, offsetY, 0.0f, 1.0f
            };
            return result;
        }

        constexpr std::array<float, 16> kIdentityMatrix = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        constexpr std::uint32_t kIrradianceWidth = 64;
        constexpr std::uint32_t kIrradianceHeight = 32;
        constexpr std::uint32_t kPrefilterWidth = 128;
        constexpr std::uint32_t kPrefilterHeight = 64;
        constexpr std::uint32_t kPrefilterLevelCount = 5;
        constexpr std::uint32_t kDirectionalShadowMapSize = 1024;
        constexpr std::uint32_t kSpotShadowMapSize = 768;
        constexpr std::uint32_t kPointShadowMapSize = 512;
        constexpr std::uint32_t kPointShadowAtlasColumns = 3;
        constexpr std::uint32_t kPointShadowAtlasRows = 2;
        constexpr std::uint32_t kPointShadowAtlasWidth = kPointShadowMapSize * kPointShadowAtlasColumns;
        constexpr std::uint32_t kPointShadowAtlasHeight = kPointShadowMapSize * kPointShadowAtlasRows;

        std::uint32_t NormalizePointShadowResolution(const std::uint32_t resolution)
        {
            std::uint32_t clamped = std::clamp<std::uint32_t>(resolution, 128u, 2048u);
            std::uint32_t normalized = 128u;
            while (normalized < clamped && normalized < 2048u)
            {
                normalized <<= 1u;
            }

            const std::uint32_t lower = normalized >> 1u;
            if (lower >= 128u && normalized - clamped > clamped - lower)
            {
                normalized = lower;
            }

            return std::clamp<std::uint32_t>(normalized, 128u, 2048u);
        }

        float SRGB8ToLinear(const std::uint8_t value)
        {
            return std::pow(static_cast<float>(value) / 255.0f, 2.2f);
        }

        std::uint8_t LinearToSRGB8(const float value)
        {
            const float clamped = std::clamp(value, 0.0f, 1.0f);
            const float encoded = std::pow(clamped, 1.0f / 2.2f);
            return static_cast<std::uint8_t>(std::clamp(encoded * 255.0f, 0.0f, 255.0f));
        }

        std::size_t FloatPixelOffset(const FloatImage& image, const std::uint32_t x, const std::uint32_t y)
        {
            return (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 3ULL;
        }

        FloatImage DecodeFloatImage(const TextureCreateDesc& textureDesc)
        {
            FloatImage image;
            image.width = textureDesc.width;
            image.height = textureDesc.height;
            image.pixels.resize(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 3ULL, 0.0f);

            const bool srgb = textureDesc.srgb;
            for (std::uint32_t y = 0; y < image.height; ++y)
            {
                for (std::uint32_t x = 0; x < image.width; ++x)
                {
                    const std::size_t srcOffset =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4ULL;
                    const std::size_t dstOffset = FloatPixelOffset(image, x, y);
                    image.pixels[dstOffset + 0] =
                        srgb ? SRGB8ToLinear(textureDesc.pixelData[srcOffset + 0]) : static_cast<float>(textureDesc.pixelData[srcOffset + 0]) / 255.0f;
                    image.pixels[dstOffset + 1] =
                        srgb ? SRGB8ToLinear(textureDesc.pixelData[srcOffset + 1]) : static_cast<float>(textureDesc.pixelData[srcOffset + 1]) / 255.0f;
                    image.pixels[dstOffset + 2] =
                        srgb ? SRGB8ToLinear(textureDesc.pixelData[srcOffset + 2]) : static_cast<float>(textureDesc.pixelData[srcOffset + 2]) / 255.0f;
                }
            }

            return image;
        }

        std::array<float, 3> SampleFloatImage(const FloatImage& image, float u, float v)
        {
            if (image.width == 0 || image.height == 0 || image.pixels.empty())
            {
                return { 0.0f, 0.0f, 0.0f };
            }

            u -= std::floor(u);
            v = std::clamp(v, 0.0f, 1.0f);

            const float x = u * static_cast<float>(image.width - 1);
            const float y = v * static_cast<float>(image.height - 1);
            const std::uint32_t x0 = static_cast<std::uint32_t>(x);
            const std::uint32_t y0 = static_cast<std::uint32_t>(y);
            const std::uint32_t x1 = (x0 + 1) % image.width;
            const std::uint32_t y1 = std::min(y0 + 1, image.height - 1);
            const float tx = x - static_cast<float>(x0);
            const float ty = y - static_cast<float>(y0);

            const std::size_t o00 = FloatPixelOffset(image, x0, y0);
            const std::size_t o10 = FloatPixelOffset(image, x1, y0);
            const std::size_t o01 = FloatPixelOffset(image, x0, y1);
            const std::size_t o11 = FloatPixelOffset(image, x1, y1);

            std::array<float, 3> sample {};
            for (std::size_t c = 0; c < 3; ++c)
            {
                const float a = std::lerp(image.pixels[o00 + c], image.pixels[o10 + c], tx);
                const float b = std::lerp(image.pixels[o01 + c], image.pixels[o11 + c], tx);
                sample[c] = std::lerp(a, b, ty);
            }

            return sample;
        }

        FloatImage ResizeEquirect(const FloatImage& source, const std::uint32_t width, const std::uint32_t height)
        {
            FloatImage resized;
            resized.width = width;
            resized.height = height;
            resized.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3ULL, 0.0f);

            for (std::uint32_t y = 0; y < height; ++y)
            {
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height);
                for (std::uint32_t x = 0; x < width; ++x)
                {
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width);
                    const std::array<float, 3> sample = SampleFloatImage(source, u, v);
                    const std::size_t dstOffset = FloatPixelOffset(resized, x, y);
                    resized.pixels[dstOffset + 0] = sample[0];
                    resized.pixels[dstOffset + 1] = sample[1];
                    resized.pixels[dstOffset + 2] = sample[2];
                }
            }

            return resized;
        }

        void BoxBlurEquirect(FloatImage& image, const std::uint32_t radiusX, const std::uint32_t radiusY, const std::uint32_t passes)
        {
            if (image.width == 0 || image.height == 0 || image.pixels.empty() || (radiusX == 0 && radiusY == 0) || passes == 0)
            {
                return;
            }

            std::vector<float> scratch(image.pixels.size(), 0.0f);
            for (std::uint32_t pass = 0; pass < passes; ++pass)
            {
                std::fill(scratch.begin(), scratch.end(), 0.0f);
                for (std::uint32_t y = 0; y < image.height; ++y)
                {
                    const std::int32_t minY = static_cast<std::int32_t>(y) - static_cast<std::int32_t>(radiusY);
                    const std::int32_t maxY = static_cast<std::int32_t>(y) + static_cast<std::int32_t>(radiusY);
                    for (std::uint32_t x = 0; x < image.width; ++x)
                    {
                        std::array<float, 3> accum { 0.0f, 0.0f, 0.0f };
                        float weightSum = 0.0f;
                        for (std::int32_t sampleY = minY; sampleY <= maxY; ++sampleY)
                        {
                            const std::uint32_t clampedY = static_cast<std::uint32_t>(
                                std::clamp(sampleY, 0, static_cast<std::int32_t>(image.height) - 1));
                            for (std::int32_t dx = -static_cast<std::int32_t>(radiusX); dx <= static_cast<std::int32_t>(radiusX); ++dx)
                            {
                                const std::int32_t wrappedX =
                                    (static_cast<std::int32_t>(x) + dx + static_cast<std::int32_t>(image.width)) %
                                    static_cast<std::int32_t>(image.width);
                                const float weight =
                                    1.0f /
                                    (1.0f + static_cast<float>(std::abs(dx)) + static_cast<float>(std::abs(sampleY - static_cast<std::int32_t>(y))));
                                const std::size_t srcOffset = FloatPixelOffset(
                                    image,
                                    static_cast<std::uint32_t>(wrappedX),
                                    clampedY);
                                accum[0] += image.pixels[srcOffset + 0] * weight;
                                accum[1] += image.pixels[srcOffset + 1] * weight;
                                accum[2] += image.pixels[srcOffset + 2] * weight;
                                weightSum += weight;
                            }
                        }

                        const std::size_t dstOffset = FloatPixelOffset(image, x, y);
                        scratch[dstOffset + 0] = accum[0] / std::max(weightSum, 1.0e-6f);
                        scratch[dstOffset + 1] = accum[1] / std::max(weightSum, 1.0e-6f);
                        scratch[dstOffset + 2] = accum[2] / std::max(weightSum, 1.0e-6f);
                    }
                }
                image.pixels.swap(scratch);
            }
        }

        std::vector<std::uint8_t> EncodeRGBA8(const FloatImage& image)
        {
            std::vector<std::uint8_t> bytes;
            bytes.resize(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4ULL, 255);
            for (std::uint32_t y = 0; y < image.height; ++y)
            {
                for (std::uint32_t x = 0; x < image.width; ++x)
                {
                    const std::size_t srcOffset = FloatPixelOffset(image, x, y);
                    const std::size_t dstOffset =
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) + static_cast<std::size_t>(x)) * 4ULL;
                    bytes[dstOffset + 0] = LinearToSRGB8(image.pixels[srcOffset + 0]);
                    bytes[dstOffset + 1] = LinearToSRGB8(image.pixels[srcOffset + 1]);
                    bytes[dstOffset + 2] = LinearToSRGB8(image.pixels[srcOffset + 2]);
                    bytes[dstOffset + 3] = 255;
                }
            }
            return bytes;
        }

        FloatImage BuildPrefilteredAtlas(const FloatImage& source)
        {
            FloatImage atlas;
            atlas.width = kPrefilterWidth;
            atlas.height = kPrefilterHeight * kPrefilterLevelCount;
            atlas.pixels.resize(static_cast<std::size_t>(atlas.width) * static_cast<std::size_t>(atlas.height) * 3ULL, 0.0f);

            FloatImage levelImage = ResizeEquirect(source, kPrefilterWidth, kPrefilterHeight);
            const std::array<std::uint32_t, kPrefilterLevelCount> blurRadii { 0u, 2u, 4u, 7u, 11u };

            for (std::uint32_t level = 0; level < kPrefilterLevelCount; ++level)
            {
                if (blurRadii[level] > 0)
                {
                    BoxBlurEquirect(levelImage, blurRadii[level], std::max(1u, blurRadii[level] / 2u), 1);
                }

                for (std::uint32_t y = 0; y < kPrefilterHeight; ++y)
                {
                    for (std::uint32_t x = 0; x < kPrefilterWidth; ++x)
                    {
                        const std::size_t srcOffset = FloatPixelOffset(levelImage, x, y);
                        const std::size_t dstOffset = FloatPixelOffset(atlas, x, y + level * kPrefilterHeight);
                        atlas.pixels[dstOffset + 0] = levelImage.pixels[srcOffset + 0];
                        atlas.pixels[dstOffset + 1] = levelImage.pixels[srcOffset + 1];
                        atlas.pixels[dstOffset + 2] = levelImage.pixels[srcOffset + 2];
                    }
                }
            }

            return atlas;
        }

        FloatImage BuildIrradianceImage(const FloatImage& source)
        {
            FloatImage irradiance = ResizeEquirect(source, kIrradianceWidth, kIrradianceHeight);
            BoxBlurEquirect(irradiance, 10, 5, 2);
            return irradiance;
        }

        PipelineStateDesc BuildPipelineDesc(
            const char* debugName,
            const RenderPassHandle renderPass,
            const ShaderProgramDesc& shaderProgram,
            const SceneBlendMode blendMode = SceneBlendMode::Opaque,
            const bool materialTwoSided = false)
        {
            PipelineStateDesc pipelineDesc;
            pipelineDesc.debugName = std::string(debugName) + ".MainPipeline";
            pipelineDesc.renderPass = renderPass;
            pipelineDesc.topology = PrimitiveTopology::TriangleList;
            pipelineDesc.cullMode = materialTwoSided ? CullMode::None : CullMode::Back;
            pipelineDesc.frontFace = FrontFace::CounterClockwise;
            pipelineDesc.depthStencilState.depthTestEnabled = true;
            pipelineDesc.depthStencilState.depthWriteEnabled =
                blendMode == SceneBlendMode::Opaque || blendMode == SceneBlendMode::Masked;
            switch (blendMode)
            {
            case SceneBlendMode::Translucent:
                pipelineDesc.blendState.enabled = true;
                pipelineDesc.blendState.srcColorFactor = BlendFactor::SrcAlpha;
                pipelineDesc.blendState.dstColorFactor = BlendFactor::OneMinusSrcAlpha;
                pipelineDesc.blendState.srcAlphaFactor = BlendFactor::One;
                pipelineDesc.blendState.dstAlphaFactor = BlendFactor::OneMinusSrcAlpha;
                break;
            case SceneBlendMode::Additive:
                pipelineDesc.blendState.enabled = true;
                pipelineDesc.blendState.srcColorFactor = BlendFactor::One;
                pipelineDesc.blendState.dstColorFactor = BlendFactor::One;
                pipelineDesc.blendState.srcAlphaFactor = BlendFactor::One;
                pipelineDesc.blendState.dstAlphaFactor = BlendFactor::One;
                break;
            case SceneBlendMode::Modulate:
                pipelineDesc.blendState.enabled = true;
                pipelineDesc.blendState.srcColorFactor = BlendFactor::DstColor;
                pipelineDesc.blendState.dstColorFactor = BlendFactor::Zero;
                pipelineDesc.blendState.srcAlphaFactor = BlendFactor::Zero;
                pipelineDesc.blendState.dstAlphaFactor = BlendFactor::One;
                break;
            case SceneBlendMode::Opaque:
            case SceneBlendMode::Masked:
            default:
                break;
            }
            pipelineDesc.vertexLayout.stride = sizeof(TriangleVertex);
            pipelineDesc.vertexLayout.attributes = {
                VertexAttributeDesc { 0, VertexFormat::Float3, offsetof(TriangleVertex, position) },
                VertexAttributeDesc { 1, VertexFormat::Float3, offsetof(TriangleVertex, color) },
                VertexAttributeDesc { 2, VertexFormat::Float2, offsetof(TriangleVertex, uv) }
            };
            pipelineDesc.descriptorBindings = {
                DescriptorBindingDesc { 0, DescriptorType::UniformBuffer, static_cast<std::uint32_t>(sizeof(PerDrawData)) },
                DescriptorBindingDesc { 1, DescriptorType::CombinedImageSampler, 0 },
                DescriptorBindingDesc { 2, DescriptorType::CombinedImageSampler, 0 },
                DescriptorBindingDesc { 3, DescriptorType::CombinedImageSampler, 0 },
                DescriptorBindingDesc { LightingSystem::kDefaultBinding, DescriptorType::UniformBuffer, LightingSystem::UniformBufferSize() },
                DescriptorBindingDesc { 5, DescriptorType::CombinedImageSampler, 0 },
                DescriptorBindingDesc { 6, DescriptorType::CombinedImageSampler, 0 },
                DescriptorBindingDesc { 7, DescriptorType::CombinedImageSampler, 0 },
                DescriptorBindingDesc { 8, DescriptorType::CombinedImageSampler, 0 },
                DescriptorBindingDesc { 9, DescriptorType::CombinedImageSampler, 0 },
                DescriptorBindingDesc { 10, DescriptorType::CombinedImageSampler, 0 },
                DescriptorBindingDesc { 11, DescriptorType::CombinedImageSampler, 0 },
                DescriptorBindingDesc { 16, DescriptorType::CombinedImageSampler, 0 },
                DescriptorBindingDesc { 15, DescriptorType::CombinedImageSampler, 0 }
            };
            pipelineDesc.shaders = shaderProgram;

            return pipelineDesc;
        }

        bool IsBlendSortedMaterial(const std::uint32_t blendMode)
        {
            switch (static_cast<SceneBlendMode>(blendMode))
            {
            case SceneBlendMode::Translucent:
            case SceneBlendMode::Additive:
            case SceneBlendMode::Modulate:
                return true;
            case SceneBlendMode::Opaque:
            case SceneBlendMode::Masked:
            default:
                return false;
            }
        }

        PipelineStateDesc BuildShadowPipelineDesc(
            const char* debugName,
            const RenderPassHandle renderPass,
            const ShaderProgramDesc& shaderProgram,
            const bool materialTwoSided = false)
        {
            PipelineStateDesc pipelineDesc;
            pipelineDesc.debugName = std::string(debugName) + ".ShadowPipeline";
            pipelineDesc.renderPass = renderPass;
            pipelineDesc.topology = PrimitiveTopology::TriangleList;
            pipelineDesc.cullMode = materialTwoSided ? CullMode::None : CullMode::Back;
            pipelineDesc.frontFace = FrontFace::CounterClockwise;
            pipelineDesc.vertexLayout.stride = sizeof(TriangleVertex);
            pipelineDesc.vertexLayout.attributes = {
                VertexAttributeDesc { 0, VertexFormat::Float3, offsetof(TriangleVertex, position) },
                VertexAttributeDesc { 1, VertexFormat::Float3, offsetof(TriangleVertex, color) },
                VertexAttributeDesc { 2, VertexFormat::Float2, offsetof(TriangleVertex, uv) }
            };
            pipelineDesc.descriptorBindings = {
                DescriptorBindingDesc { 0, DescriptorType::UniformBuffer, static_cast<std::uint32_t>(sizeof(ShadowPerDrawData)) },
                DescriptorBindingDesc { 1, DescriptorType::CombinedImageSampler, 0 },
                DescriptorBindingDesc { 2, DescriptorType::CombinedImageSampler, 0 }
            };
            pipelineDesc.shaders = shaderProgram;
            return pipelineDesc;
        }

        PipelineStateDesc BuildSkyPipelineDesc(
            const char* debugName,
            const RenderPassHandle renderPass,
            const ShaderProgramDesc& shaderProgram)
        {
            PipelineStateDesc pipelineDesc;
            pipelineDesc.debugName = std::string(debugName) + ".SkyPipeline";
            pipelineDesc.renderPass = renderPass;
            pipelineDesc.topology = PrimitiveTopology::TriangleList;
            pipelineDesc.cullMode = CullMode::None;
            pipelineDesc.frontFace = FrontFace::CounterClockwise;
            pipelineDesc.vertexLayout.stride = sizeof(TriangleVertex);
            pipelineDesc.vertexLayout.attributes = {
                VertexAttributeDesc { 0, VertexFormat::Float3, offsetof(TriangleVertex, position) },
                VertexAttributeDesc { 1, VertexFormat::Float3, offsetof(TriangleVertex, color) },
                VertexAttributeDesc { 2, VertexFormat::Float2, offsetof(TriangleVertex, uv) }
            };
            pipelineDesc.descriptorBindings = {
                DescriptorBindingDesc { 0, DescriptorType::UniformBuffer, static_cast<std::uint32_t>(sizeof(PerDrawData)) }
            };
            pipelineDesc.shaders = shaderProgram;
            return pipelineDesc;
        }

        PipelineStateDesc BuildGridPipelineDesc(
            const char* debugName,
            const RenderPassHandle renderPass,
            const ShaderProgramDesc& shaderProgram)
        {
            PipelineStateDesc pipelineDesc;
            pipelineDesc.debugName = std::string(debugName) + ".GridPipeline";
            pipelineDesc.renderPass = renderPass;
            pipelineDesc.topology = PrimitiveTopology::TriangleList;
            pipelineDesc.cullMode = CullMode::None;
            pipelineDesc.frontFace = FrontFace::CounterClockwise;
            pipelineDesc.vertexLayout.stride = sizeof(TriangleVertex);
            pipelineDesc.vertexLayout.attributes = {
                VertexAttributeDesc { 0, VertexFormat::Float3, offsetof(TriangleVertex, position) },
                VertexAttributeDesc { 1, VertexFormat::Float3, offsetof(TriangleVertex, color) },
                VertexAttributeDesc { 2, VertexFormat::Float2, offsetof(TriangleVertex, uv) }
            };
            pipelineDesc.descriptorBindings = {
                DescriptorBindingDesc { 0, DescriptorType::UniformBuffer, static_cast<std::uint32_t>(sizeof(PerDrawData)) }
            };
            pipelineDesc.shaders = shaderProgram;
            return pipelineDesc;
        }

        PipelineStateDesc BuildPostProcessPipelineDesc(
            const char* debugName,
            const RenderPassHandle renderPass,
            const ShaderProgramDesc& shaderProgram)
        {
            PipelineStateDesc pipelineDesc;
            pipelineDesc.debugName = std::string(debugName) + ".PostProcessPipeline";
            pipelineDesc.renderPass = renderPass;
            pipelineDesc.topology = PrimitiveTopology::TriangleList;
            pipelineDesc.cullMode = CullMode::None;
            pipelineDesc.frontFace = FrontFace::CounterClockwise;
            pipelineDesc.vertexLayout.stride = sizeof(TriangleVertex);
            pipelineDesc.vertexLayout.attributes = {
                VertexAttributeDesc { 0, VertexFormat::Float3, offsetof(TriangleVertex, position) },
                VertexAttributeDesc { 1, VertexFormat::Float3, offsetof(TriangleVertex, color) },
                VertexAttributeDesc { 2, VertexFormat::Float2, offsetof(TriangleVertex, uv) }
            };
            pipelineDesc.descriptorBindings = {
                DescriptorBindingDesc { 1, DescriptorType::CombinedImageSampler, 0 },
                DescriptorBindingDesc { LightingSystem::kDefaultBinding, DescriptorType::UniformBuffer, LightingSystem::UniformBufferSize() }
            };
            pipelineDesc.shaders = shaderProgram;
            return pipelineDesc;
        }

        MeshDesc BuildFullscreenTriangleMeshDesc()
        {
            const std::array<TriangleVertex, 3> vertices = {
                TriangleVertex { { -1.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
                TriangleVertex { { 3.0f, -1.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 2.0f, 0.0f } },
                TriangleVertex { { -1.0f, 3.0f, 0.0f }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 2.0f } }
            };
            const std::array<std::uint32_t, 3> indices = { 0u, 1u, 2u };

            MeshDesc mesh;
            mesh.vertexData.resize(sizeof(vertices));
            std::memcpy(mesh.vertexData.data(), vertices.data(), sizeof(vertices));
            mesh.indexData.resize(sizeof(indices));
            std::memcpy(mesh.indexData.data(), indices.data(), sizeof(indices));
            mesh.indexFormat = IndexFormat::UInt32;
            return mesh;
        }

        class ScenePipelineBase : public IRenderPipeline
        {
        public:
            bool Init(IRenderBackend& renderer, GPUResourceManager& resourceManager) override
            {
                if (m_Initialized)
                {
                    return true;
                }

                m_RenderBackend = &renderer;
                m_ResourceManager = &resourceManager;
                m_PointShadowMapSize = kPointShadowMapSize;

                RenderPassDesc renderPassDesc;
                renderPassDesc.debugName = std::string(GetDebugName()) + ".MainPass";
                renderPassDesc.clearColor = GetClearColor();
                FramebufferDesc framebufferDesc;
                framebufferDesc.debugName = std::string(GetDebugName()) + ".MainFramebuffer";
                framebufferDesc.presentToSwapchain = false;
                m_Framebuffer = resourceManager.CreateFramebuffer(framebufferDesc);
                if (m_Framebuffer == InvalidResourceHandle)
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }
                renderPassDesc.framebuffer = m_Framebuffer;
                m_RenderPass = resourceManager.CreateRenderPass(renderPassDesc);
                if (m_RenderPass == InvalidResourceHandle)
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                if (!m_ShaderSystem.Initialize(renderer.GetAPI()))
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }
                if (!m_TextureSystem.Initialize(resourceManager))
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }
                if (!m_LightingSystem.Initialize(LightingSystem::kDefaultBinding))
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                const Color clearColor = GetClearColor();
                m_LightingSystem.SetAmbientLight(
                    { std::max(clearColor.r, 0.0f), std::max(clearColor.g, 0.0f), std::max(clearColor.b, 0.0f) },
                    GetAmbientLightIntensity());
                m_LightingSystem.SetCameraWorldPosition({ 0.0f, 0.0f, 5.0f });
                DirectionalLightDesc directionalLight;
                directionalLight.direction = { -0.35f, -0.85f, -0.40f };
                directionalLight.color = { 1.0f, 0.97f, 0.92f };
                directionalLight.intensity = GetDirectionalLightIntensity();
                m_LightingSystem.SetDirectionalLight(directionalLight);
                m_LightingSystem.SetImageBasedLight(ImageBasedLightDesc {});

                m_DefaultAlbedoTexture = m_TextureSystem.CreateExternalTextureReference(
                    std::filesystem::path("assets/default_materials/tool_textures/generic.png"),
                    true,
                    std::string(GetDebugName()) + ".DefaultAlbedo");
                if (m_DefaultAlbedoTexture == InvalidTextureHandle)
                {
                    m_DefaultAlbedoTexture = m_TextureSystem.CreateSolidColorTexture(
                        std::string(GetDebugName()) + ".DefaultAlbedoFallback",
                        { 255, 255, 255, 255 },
                        true);
                }
                m_DefaultNormalTexture = m_TextureSystem.CreateSolidColorTexture(
                    std::string(GetDebugName()) + ".DefaultNormal",
                    { 128, 128, 255, 255 },
                    false);
                m_DefaultOrmTexture = m_TextureSystem.CreateSolidColorTexture(
                    std::string(GetDebugName()) + ".DefaultORM",
                    { 255, 255, 0, 255 },
                    false);
                m_DefaultMetallicTexture = m_TextureSystem.CreateSolidColorTexture(
                    std::string(GetDebugName()) + ".DefaultMetallic",
                    { 255, 255, 255, 255 },
                    false);
                m_DefaultRoughnessTexture = m_TextureSystem.CreateSolidColorTexture(
                    std::string(GetDebugName()) + ".DefaultRoughness",
                    { 255, 255, 255, 255 },
                    false);
                m_DefaultAmbientOcclusionTexture = m_TextureSystem.CreateSolidColorTexture(
                    std::string(GetDebugName()) + ".DefaultAmbientOcclusion",
                    { 255, 255, 255, 255 },
                    false);
                m_DefaultEmissiveTexture = m_TextureSystem.CreateSolidColorTexture(
                    std::string(GetDebugName()) + ".DefaultEmissive",
                    { 0, 0, 0, 255 },
                    true);
                m_DefaultOpacityTexture = m_TextureSystem.CreateSolidColorTexture(
                    std::string(GetDebugName()) + ".DefaultOpacity",
                    { 255, 255, 255, 255 },
                    false);
                m_DefaultHeightTexture = m_TextureSystem.CreateSolidColorTexture(
                    std::string(GetDebugName()) + ".DefaultHeight",
                    { 128, 128, 128, 255 },
                    false);
                TextureCreateDesc defaultIrradianceDesc;
                defaultIrradianceDesc.debugName = std::string(GetDebugName()) + ".DefaultIrradiance";
                defaultIrradianceDesc.width = kIrradianceWidth;
                defaultIrradianceDesc.height = kIrradianceHeight;
                defaultIrradianceDesc.srgb = true;
                defaultIrradianceDesc.pixelData.resize(
                    static_cast<std::size_t>(kIrradianceWidth) * static_cast<std::size_t>(kIrradianceHeight) * 4ULL,
                    255);
                for (std::size_t i = 0; i < defaultIrradianceDesc.pixelData.size(); i += 4)
                {
                    defaultIrradianceDesc.pixelData[i + 0] = 186;
                    defaultIrradianceDesc.pixelData[i + 1] = 205;
                    defaultIrradianceDesc.pixelData[i + 2] = 255;
                }
                m_DefaultIrradianceTexture = m_TextureSystem.CreateTexture2D(defaultIrradianceDesc);

                TextureCreateDesc defaultPrefilterDesc;
                defaultPrefilterDesc.debugName = std::string(GetDebugName()) + ".DefaultPrefilter";
                defaultPrefilterDesc.width = kPrefilterWidth;
                defaultPrefilterDesc.height = kPrefilterHeight * kPrefilterLevelCount;
                defaultPrefilterDesc.srgb = true;
                defaultPrefilterDesc.pixelData.resize(
                    static_cast<std::size_t>(defaultPrefilterDesc.width) * static_cast<std::size_t>(defaultPrefilterDesc.height) * 4ULL,
                    255);
                for (std::size_t i = 0; i < defaultPrefilterDesc.pixelData.size(); i += 4)
                {
                    defaultPrefilterDesc.pixelData[i + 0] = 186;
                    defaultPrefilterDesc.pixelData[i + 1] = 205;
                    defaultPrefilterDesc.pixelData[i + 2] = 255;
                }
                m_DefaultPrefilterTexture = m_TextureSystem.CreateTexture2D(defaultPrefilterDesc);

                m_DefaultShadowTexture = m_TextureSystem.CreateSolidColorTexture(
                    std::string(GetDebugName()) + ".DefaultShadow",
                    { 255, 255, 255, 255 },
                    false);

                if (m_DefaultAlbedoTexture == InvalidTextureHandle ||
                    m_DefaultNormalTexture == InvalidTextureHandle ||
                    m_DefaultOrmTexture == InvalidTextureHandle ||
                    m_DefaultMetallicTexture == InvalidTextureHandle ||
                    m_DefaultRoughnessTexture == InvalidTextureHandle ||
                    m_DefaultAmbientOcclusionTexture == InvalidTextureHandle ||
                    m_DefaultEmissiveTexture == InvalidTextureHandle ||
                    m_DefaultOpacityTexture == InvalidTextureHandle ||
                    m_DefaultHeightTexture == InvalidTextureHandle ||
                    m_DefaultIrradianceTexture == InvalidTextureHandle ||
                    m_DefaultPrefilterTexture == InvalidTextureHandle ||
                    m_DefaultShadowTexture == InvalidTextureHandle)
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                m_CurrentIrradianceTexture = m_DefaultIrradianceTexture;
                m_CurrentPrefilterTexture = m_DefaultPrefilterTexture;

                const bool registered = m_ShaderSystem.RegisterProgram(
                    ShaderProgramRegistration {
                        "Builtin.Triangle",
                        "triangle",
                        "triangle"
                    });
                const bool shadowRegistered = m_ShaderSystem.RegisterProgram(
                    ShaderProgramRegistration {
                        "Builtin.Shadow",
                        "shadow",
                        "shadow"
                    });
                const bool skyRegistered = m_ShaderSystem.RegisterProgram(
                    ShaderProgramRegistration {
                        "Builtin.Sky",
                        "sky",
                        "sky"
                    });
                const bool gridRegistered = m_ShaderSystem.RegisterProgram(
                    ShaderProgramRegistration {
                        "Builtin.Grid",
                        "grid",
                        "grid"
                    });
                const bool postProcessRegistered = m_ShaderSystem.RegisterProgram(
                    ShaderProgramRegistration {
                        "Builtin.PostProcess",
                        "postprocess",
                        "postprocess"
                    });
                if (!registered || !shadowRegistered || !skyRegistered || !gridRegistered || !postProcessRegistered)
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                ShaderVariantOptions shaderOptions;
                shaderOptions.preferSpirv = true;
                shaderOptions.macros = {
                    ShaderMacro { "LUMA_EDITOR", "1" }
                };

                ShaderProgramVariant shadowShaderVariant;
                if (!m_ShaderSystem.TryResolveProgram("Builtin.Shadow", shaderOptions, shadowShaderVariant))
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                ShaderProgramVariant materialShaderVariant;
                if (!m_ShaderSystem.TryResolveProgram("Builtin.Triangle", shaderOptions, materialShaderVariant))
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                ShaderProgramVariant skyShaderVariant;
                if (!m_ShaderSystem.TryResolveProgram("Builtin.Sky", shaderOptions, skyShaderVariant))
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                ShaderProgramVariant gridShaderVariant;
                if (!m_ShaderSystem.TryResolveProgram("Builtin.Grid", shaderOptions, gridShaderVariant))
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                ShaderProgramVariant postProcessShaderVariant;
                if (!m_ShaderSystem.TryResolveProgram("Builtin.PostProcess", shaderOptions, postProcessShaderVariant))
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                m_DefaultMaterialShaderProgramDesc = materialShaderVariant.programDesc;
                m_ShadowShaderProgramDesc = shadowShaderVariant.programDesc;
                m_SkyShaderProgramDesc = skyShaderVariant.programDesc;
                m_GridShaderProgramDesc = gridShaderVariant.programDesc;
                m_PostProcessShaderProgramDesc = postProcessShaderVariant.programDesc;

                m_PostProcessPipelineState = resourceManager.CreatePipelineState(
                    BuildPostProcessPipelineDesc(GetDebugName(), m_RenderPass, postProcessShaderVariant.programDesc));
                if (m_PostProcessPipelineState == InvalidResourceHandle)
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                const MeshDesc postProcessMeshDesc = BuildFullscreenTriangleMeshDesc();
                m_PostProcessMesh = resourceManager.CreateMesh(
                    postProcessMeshDesc,
                    std::string(GetDebugName()) + ".PostProcessMesh");
                if (m_PostProcessMesh == InvalidResourceHandle)
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                if (!EnsureScenePassResources(1280, 720))
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                RenderTargetDesc directionalShadowTargetDesc;
                directionalShadowTargetDesc.debugName = std::string(GetDebugName()) + ".DirectionalShadowTarget";
                directionalShadowTargetDesc.width = kDirectionalShadowMapSize;
                directionalShadowTargetDesc.height = kDirectionalShadowMapSize;
                directionalShadowTargetDesc.srgb = false;
                directionalShadowTargetDesc.clearColor = { 1.0f, 1.0f, 1.0f, 1.0f };
                m_DirectionalShadowRenderTarget = resourceManager.CreateRenderTarget(directionalShadowTargetDesc);

                RenderTargetDesc spotShadowTargetDesc;
                spotShadowTargetDesc.debugName = std::string(GetDebugName()) + ".SpotShadowTarget";
                spotShadowTargetDesc.width = kSpotShadowMapSize;
                spotShadowTargetDesc.height = kSpotShadowMapSize;
                spotShadowTargetDesc.srgb = false;
                spotShadowTargetDesc.clearColor = { 1.0f, 1.0f, 1.0f, 1.0f };
                m_SpotShadowRenderTarget = resourceManager.CreateRenderTarget(spotShadowTargetDesc);

                RenderTargetDesc pointShadowTargetDesc;
                pointShadowTargetDesc.debugName = std::string(GetDebugName()) + ".PointShadowTarget";
                pointShadowTargetDesc.width = m_PointShadowMapSize * kPointShadowAtlasColumns;
                pointShadowTargetDesc.height = m_PointShadowMapSize * kPointShadowAtlasRows;
                pointShadowTargetDesc.srgb = false;
                pointShadowTargetDesc.clearColor = { 1.0f, 1.0f, 1.0f, 1.0f };
                m_PointShadowRenderTarget = resourceManager.CreateRenderTarget(pointShadowTargetDesc);

                if (m_DirectionalShadowRenderTarget == InvalidResourceHandle ||
                    m_SpotShadowRenderTarget == InvalidResourceHandle ||
                    m_PointShadowRenderTarget == InvalidResourceHandle)
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                FramebufferDesc directionalShadowFramebufferDesc;
                directionalShadowFramebufferDesc.debugName = std::string(GetDebugName()) + ".DirectionalShadowFramebuffer";
                directionalShadowFramebufferDesc.colorTarget = m_DirectionalShadowRenderTarget;
                m_DirectionalShadowFramebuffer = resourceManager.CreateFramebuffer(directionalShadowFramebufferDesc);

                FramebufferDesc spotShadowFramebufferDesc;
                spotShadowFramebufferDesc.debugName = std::string(GetDebugName()) + ".SpotShadowFramebuffer";
                spotShadowFramebufferDesc.colorTarget = m_SpotShadowRenderTarget;
                m_SpotShadowFramebuffer = resourceManager.CreateFramebuffer(spotShadowFramebufferDesc);

                FramebufferDesc pointShadowFramebufferDesc;
                pointShadowFramebufferDesc.debugName = std::string(GetDebugName()) + ".PointShadowFramebuffer";
                pointShadowFramebufferDesc.colorTarget = m_PointShadowRenderTarget;
                m_PointShadowFramebuffer = resourceManager.CreateFramebuffer(pointShadowFramebufferDesc);

                if (m_DirectionalShadowFramebuffer == InvalidResourceHandle ||
                    m_SpotShadowFramebuffer == InvalidResourceHandle ||
                    m_PointShadowFramebuffer == InvalidResourceHandle)
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                RenderPassDesc directionalShadowPassDesc;
                directionalShadowPassDesc.debugName = std::string(GetDebugName()) + ".DirectionalShadowPass";
                directionalShadowPassDesc.clearColor = { 1.0f, 1.0f, 1.0f, 1.0f };
                directionalShadowPassDesc.framebuffer = m_DirectionalShadowFramebuffer;
                m_DirectionalShadowRenderPass = resourceManager.CreateRenderPass(directionalShadowPassDesc);

                RenderPassDesc spotShadowPassDesc;
                spotShadowPassDesc.debugName = std::string(GetDebugName()) + ".SpotShadowPass";
                spotShadowPassDesc.clearColor = { 1.0f, 1.0f, 1.0f, 1.0f };
                spotShadowPassDesc.framebuffer = m_SpotShadowFramebuffer;
                m_SpotShadowRenderPass = resourceManager.CreateRenderPass(spotShadowPassDesc);

                RenderPassDesc pointShadowPassDesc;
                pointShadowPassDesc.debugName = std::string(GetDebugName()) + ".PointShadowPass";
                pointShadowPassDesc.clearColor = { 1.0f, 1.0f, 1.0f, 1.0f };
                pointShadowPassDesc.framebuffer = m_PointShadowFramebuffer;
                m_PointShadowRenderPass = resourceManager.CreateRenderPass(pointShadowPassDesc);

                if (m_DirectionalShadowRenderPass == InvalidResourceHandle ||
                    m_SpotShadowRenderPass == InvalidResourceHandle ||
                    m_PointShadowRenderPass == InvalidResourceHandle)
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                m_DirectionalShadowPipelineState = resourceManager.CreatePipelineState(
                    BuildShadowPipelineDesc(GetDebugName(), m_DirectionalShadowRenderPass, shadowShaderVariant.programDesc));
                m_PointShadowPipelineState = resourceManager.CreatePipelineState(
                    BuildShadowPipelineDesc(GetDebugName(), m_PointShadowRenderPass, shadowShaderVariant.programDesc));
                m_SpotShadowPipelineState = resourceManager.CreatePipelineState(
                    BuildShadowPipelineDesc(GetDebugName(), m_SpotShadowRenderPass, shadowShaderVariant.programDesc));
                if (m_DirectionalShadowPipelineState == InvalidResourceHandle ||
                    m_PointShadowPipelineState == InvalidResourceHandle ||
                    m_SpotShadowPipelineState == InvalidResourceHandle)
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                DescriptorSetDesc directionalShadowDescriptorDesc;
                directionalShadowDescriptorDesc.buffers.push_back(
                    DescriptorBufferWrite { 0, std::vector<std::uint8_t>(sizeof(ShadowPerDrawData), 0) });
                directionalShadowDescriptorDesc.images.push_back(DescriptorImageWrite { 1, m_DefaultOpacityTexture });
                directionalShadowDescriptorDesc.images.push_back(DescriptorImageWrite { 2, m_DefaultHeightTexture });
                m_DirectionalShadowDescriptorSet = resourceManager.CreateDescriptorSet(
                    m_DirectionalShadowPipelineState,
                    directionalShadowDescriptorDesc,
                    std::string(GetDebugName()) + ".DirectionalShadow");

                DescriptorSetDesc pointShadowDescriptorDesc;
                pointShadowDescriptorDesc.buffers.push_back(
                    DescriptorBufferWrite { 0, std::vector<std::uint8_t>(sizeof(ShadowPerDrawData), 0) });
                pointShadowDescriptorDesc.images.push_back(DescriptorImageWrite { 1, m_DefaultOpacityTexture });
                pointShadowDescriptorDesc.images.push_back(DescriptorImageWrite { 2, m_DefaultHeightTexture });
                m_PointShadowDescriptorSet = resourceManager.CreateDescriptorSet(
                    m_PointShadowPipelineState,
                    pointShadowDescriptorDesc,
                    std::string(GetDebugName()) + ".PointShadow");

                DescriptorSetDesc spotShadowDescriptorDesc;
                spotShadowDescriptorDesc.buffers.push_back(
                    DescriptorBufferWrite { 0, std::vector<std::uint8_t>(sizeof(ShadowPerDrawData), 0) });
                spotShadowDescriptorDesc.images.push_back(DescriptorImageWrite { 1, m_DefaultOpacityTexture });
                spotShadowDescriptorDesc.images.push_back(DescriptorImageWrite { 2, m_DefaultHeightTexture });
                m_SpotShadowDescriptorSet = resourceManager.CreateDescriptorSet(
                    m_SpotShadowPipelineState,
                    spotShadowDescriptorDesc,
                    std::string(GetDebugName()) + ".SpotShadow");

                if (m_DirectionalShadowDescriptorSet == InvalidResourceHandle ||
                    m_PointShadowDescriptorSet == InvalidResourceHandle ||
                    m_SpotShadowDescriptorSet == InvalidResourceHandle)
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                DescriptorSetDesc postProcessDescriptorDesc;
                postProcessDescriptorDesc.images.push_back(DescriptorImageWrite { 1, m_DefaultAlbedoTexture });
                if (!m_LightingSystem.BuildDescriptorSetDesc(postProcessDescriptorDesc))
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }
                m_PostProcessDescriptorSet = resourceManager.CreateDescriptorSet(
                    m_PostProcessPipelineState,
                    postProcessDescriptorDesc,
                    std::string(GetDebugName()) + ".PostProcess");
                if (m_PostProcessDescriptorSet == InvalidResourceHandle)
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                if (!BuildRenderGraph())
                {
                    Shutdown(renderer, resourceManager);
                    return false;
                }

                m_Initialized = true;
                return true;
            }

            void Shutdown(IRenderBackend& renderer, GPUResourceManager& resourceManager) override
            {
                (void)renderer;
                for (auto& [_, item] : m_RenderItems)
                {
                    if (item.mesh != InvalidResourceHandle)
                    {
                        resourceManager.DestroyMesh(item.mesh, GPUResourceManager::DestroyMode::Deferred);
                    }
                    if (item.bakedLightmapTexture != InvalidTextureHandle)
                    {
                        resourceManager.DestroyTexture(item.bakedLightmapTexture, GPUResourceManager::DestroyMode::Deferred);
                    }
                }
                m_RenderItems.clear();
                m_RenderItemOrder.clear();
                m_ExternalTextures.clear();
                m_LastMaterialTextureBindingHashes.clear();
                m_LastShadowTextureBindingHashes.clear();
                if (m_DirectionalShadowDescriptorSet != InvalidResourceHandle)
                {
                    resourceManager.DestroyDescriptorSet(m_DirectionalShadowDescriptorSet, GPUResourceManager::DestroyMode::Deferred);
                    m_DirectionalShadowDescriptorSet = InvalidResourceHandle;
                }
                if (m_PointShadowDescriptorSet != InvalidResourceHandle)
                {
                    resourceManager.DestroyDescriptorSet(m_PointShadowDescriptorSet, GPUResourceManager::DestroyMode::Deferred);
                    m_PointShadowDescriptorSet = InvalidResourceHandle;
                }
                if (m_SpotShadowDescriptorSet != InvalidResourceHandle)
                {
                    resourceManager.DestroyDescriptorSet(m_SpotShadowDescriptorSet, GPUResourceManager::DestroyMode::Deferred);
                    m_SpotShadowDescriptorSet = InvalidResourceHandle;
                }
                if (m_PostProcessDescriptorSet != InvalidResourceHandle)
                {
                    resourceManager.DestroyDescriptorSet(m_PostProcessDescriptorSet, GPUResourceManager::DestroyMode::Deferred);
                    m_PostProcessDescriptorSet = InvalidResourceHandle;
                }
                if (m_SkyMesh != InvalidResourceHandle)
                {
                    resourceManager.DestroyMesh(m_SkyMesh, GPUResourceManager::DestroyMode::Deferred);
                    m_SkyMesh = InvalidResourceHandle;
                }
                if (m_GridMesh != InvalidResourceHandle)
                {
                    resourceManager.DestroyMesh(m_GridMesh, GPUResourceManager::DestroyMode::Deferred);
                    m_GridMesh = InvalidResourceHandle;
                }
                if (m_Mesh != InvalidResourceHandle)
                {
                    resourceManager.DestroyMesh(m_Mesh, GPUResourceManager::DestroyMode::Deferred);
                    m_Mesh = InvalidResourceHandle;
                }
                if (m_PostProcessMesh != InvalidResourceHandle)
                {
                    resourceManager.DestroyMesh(m_PostProcessMesh, GPUResourceManager::DestroyMode::Deferred);
                    m_PostProcessMesh = InvalidResourceHandle;
                }
                if (m_PostProcessPipelineState != InvalidResourceHandle)
                {
                    resourceManager.DestroyPipelineState(m_PostProcessPipelineState, GPUResourceManager::DestroyMode::Deferred);
                    m_PostProcessPipelineState = InvalidResourceHandle;
                }
                if (m_DirectionalShadowPipelineState != InvalidResourceHandle)
                {
                    resourceManager.DestroyPipelineState(m_DirectionalShadowPipelineState, GPUResourceManager::DestroyMode::Deferred);
                    m_DirectionalShadowPipelineState = InvalidResourceHandle;
                }
                if (m_PointShadowPipelineState != InvalidResourceHandle)
                {
                    resourceManager.DestroyPipelineState(m_PointShadowPipelineState, GPUResourceManager::DestroyMode::Deferred);
                    m_PointShadowPipelineState = InvalidResourceHandle;
                }
                if (m_SpotShadowPipelineState != InvalidResourceHandle)
                {
                    resourceManager.DestroyPipelineState(m_SpotShadowPipelineState, GPUResourceManager::DestroyMode::Deferred);
                    m_SpotShadowPipelineState = InvalidResourceHandle;
                }
                if (m_RenderPass != InvalidResourceHandle)
                {
                    resourceManager.DestroyRenderPass(m_RenderPass, GPUResourceManager::DestroyMode::Deferred);
                    m_RenderPass = InvalidResourceHandle;
                }
                if (m_DirectionalShadowRenderPass != InvalidResourceHandle)
                {
                    resourceManager.DestroyRenderPass(m_DirectionalShadowRenderPass, GPUResourceManager::DestroyMode::Deferred);
                    m_DirectionalShadowRenderPass = InvalidResourceHandle;
                }
                if (m_PointShadowRenderPass != InvalidResourceHandle)
                {
                    resourceManager.DestroyRenderPass(m_PointShadowRenderPass, GPUResourceManager::DestroyMode::Deferred);
                    m_PointShadowRenderPass = InvalidResourceHandle;
                }
                if (m_SpotShadowRenderPass != InvalidResourceHandle)
                {
                    resourceManager.DestroyRenderPass(m_SpotShadowRenderPass, GPUResourceManager::DestroyMode::Deferred);
                    m_SpotShadowRenderPass = InvalidResourceHandle;
                }
                DestroyScenePassResources();
                if (m_Framebuffer != InvalidResourceHandle)
                {
                    resourceManager.DestroyFramebuffer(m_Framebuffer, GPUResourceManager::DestroyMode::Deferred);
                    m_Framebuffer = InvalidResourceHandle;
                }
                if (m_DirectionalShadowFramebuffer != InvalidResourceHandle)
                {
                    resourceManager.DestroyFramebuffer(m_DirectionalShadowFramebuffer, GPUResourceManager::DestroyMode::Deferred);
                    m_DirectionalShadowFramebuffer = InvalidResourceHandle;
                }
                if (m_PointShadowFramebuffer != InvalidResourceHandle)
                {
                    resourceManager.DestroyFramebuffer(m_PointShadowFramebuffer, GPUResourceManager::DestroyMode::Deferred);
                    m_PointShadowFramebuffer = InvalidResourceHandle;
                }
                if (m_SpotShadowFramebuffer != InvalidResourceHandle)
                {
                    resourceManager.DestroyFramebuffer(m_SpotShadowFramebuffer, GPUResourceManager::DestroyMode::Deferred);
                    m_SpotShadowFramebuffer = InvalidResourceHandle;
                }
                if (m_DirectionalShadowRenderTarget != InvalidResourceHandle)
                {
                    resourceManager.DestroyRenderTarget(m_DirectionalShadowRenderTarget, GPUResourceManager::DestroyMode::Deferred);
                    m_DirectionalShadowRenderTarget = InvalidResourceHandle;
                }
                if (m_PointShadowRenderTarget != InvalidResourceHandle)
                {
                    resourceManager.DestroyRenderTarget(m_PointShadowRenderTarget, GPUResourceManager::DestroyMode::Deferred);
                    m_PointShadowRenderTarget = InvalidResourceHandle;
                }
                if (m_SpotShadowRenderTarget != InvalidResourceHandle)
                {
                    resourceManager.DestroyRenderTarget(m_SpotShadowRenderTarget, GPUResourceManager::DestroyMode::Deferred);
                    m_SpotShadowRenderTarget = InvalidResourceHandle;
                }

                m_RenderGraph.Clear();
                m_RenderGraphFrameIndex = 0;
                m_RenderBackend = nullptr;
                m_ResourceManager = nullptr;
                m_LightingSystem.Shutdown();
                m_DefaultAlbedoTexture = InvalidTextureHandle;
                m_DefaultNormalTexture = InvalidTextureHandle;
                m_DefaultOrmTexture = InvalidTextureHandle;
                m_DefaultMetallicTexture = InvalidTextureHandle;
                m_DefaultRoughnessTexture = InvalidTextureHandle;
                m_DefaultAmbientOcclusionTexture = InvalidTextureHandle;
                m_DefaultEmissiveTexture = InvalidTextureHandle;
                m_DefaultOpacityTexture = InvalidTextureHandle;
                m_DefaultHeightTexture = InvalidTextureHandle;
                m_DefaultIrradianceTexture = InvalidTextureHandle;
                m_DefaultPrefilterTexture = InvalidTextureHandle;
                m_DefaultShadowTexture = InvalidTextureHandle;
                m_CurrentEnvironmentTexture = InvalidTextureHandle;
                m_CurrentIrradianceTexture = InvalidTextureHandle;
                m_CurrentPrefilterTexture = InvalidTextureHandle;
                m_DefaultMaterialPipelineKey.clear();
                m_DefaultShadowPipelineKey.clear();
                m_MaterialPipelines.clear();
                m_ShadowMaterialPipelines.clear();
                m_CurrentEnvironmentSourcePath.clear();
                m_CurrentEnvironmentSourceWidth = 0;
                m_CurrentEnvironmentSourceHeight = 0;
                m_HasPrefilteredEnvironment = false;
                m_HasDirectionalShadow = false;
                m_HasPointShadow = false;
                m_HasSpotShadow = false;
                m_ShadowedPointLightIndex = -1;
                m_ShadowedSpotLightIndex = -1;
                m_PointShadowMapSize = kPointShadowMapSize;
                m_TextureSystem.Shutdown();
                m_ShaderSystem.Shutdown();
                m_Initialized = false;
                m_CurrentAutoExposureEV = 0.0f;
                m_LastAutoExposureTimeSeconds = 0.0f;
                m_AutoExposureInitialized = false;
                m_GridMeshRevision = 0;
                m_SkyMeshRevision = 0;
                m_OverrideMeshRevision = 0;
                m_RenderItemsRevision = 0;
                m_HadGridMesh = false;
                m_HadSkyMesh = false;
                m_HadOverrideMesh = false;
                m_ShadowShaderProgramDesc = {};
                m_SkyShaderProgramDesc = {};
                m_GridShaderProgramDesc = {};
                m_PostProcessShaderProgramDesc = {};
            }

            void RenderFrame(IRenderBackend& renderer, const SceneView& sceneView) override
            {
                (void)renderer;
                if (!m_Initialized)
                {
                    return;
                }

                if (m_RenderBackend == nullptr)
                {
                    return;
                }

                m_TextureSystem.TickStreaming();
                m_ViewProjection = sceneView.viewProjection;
                m_CameraWorldPosition = sceneView.cameraWorldPosition;
                m_CurrentEnvironmentTexture = ResolveSourceEnvironmentTexture(sceneView.imageBasedLight);
                RefreshImageBasedLighting(sceneView.imageBasedLight);
                if (!EnsurePointShadowResources(ResolveDesiredPointShadowMapSize(sceneView)))
                {
                    return;
                }
                UpdateShadowState(sceneView);
                m_LightingSystem.SetCameraWorldPosition(sceneView.cameraWorldPosition);
                m_LightingSystem.SetAmbientLight(sceneView.ambientLightColor, sceneView.ambientLightIntensity);
                m_LightingSystem.SetDirectionalLight(sceneView.directionalLight);
                m_LightingSystem.SetPointLights(sceneView.pointLights, sceneView.pointLightCount);
                m_LightingSystem.SetSpotLights(sceneView.spotLights, sceneView.spotLightCount);
                m_LightingSystem.SetDirectionalShadow(
                    m_DirectionalShadowTextureMatrix,
                    m_HasDirectionalShadow,
                    0.0018f,
                    1.0f / static_cast<float>(kDirectionalShadowMapSize));
                m_LightingSystem.SetPointShadow(
                    m_PointShadowTextureMatrices,
                    m_PointShadowLightPositionRange,
                    m_HasPointShadow,
                    m_PointShadowBias,
                    m_PointShadowSoftShadows,
                    static_cast<float>(m_ShadowedPointLightIndex),
                    1.0f / static_cast<float>(std::max(m_PointShadowMapSize * kPointShadowAtlasColumns, 1u)),
                    1.0f / static_cast<float>(std::max(m_PointShadowMapSize * kPointShadowAtlasRows, 1u)));
                m_LightingSystem.SetSpotShadow(
                    m_SpotShadowTextureMatrix,
                    m_HasSpotShadow,
                    0.0012f,
                    1.0f / static_cast<float>(kSpotShadowMapSize),
                    static_cast<float>(m_ShadowedSpotLightIndex));
                ImageBasedLightDesc imageBasedLight;
                imageBasedLight.enabled = sceneView.imageBasedLight.enabled;
                imageBasedLight.diffuseColor = sceneView.imageBasedLight.diffuseColor;
                imageBasedLight.diffuseIntensity = sceneView.imageBasedLight.diffuseIntensity;
                imageBasedLight.specularColor = sceneView.imageBasedLight.specularColor;
                imageBasedLight.specularIntensity = sceneView.imageBasedLight.specularIntensity;
                imageBasedLight.ambientOcclusionStrength = sceneView.imageBasedLight.ambientOcclusionStrength;
                imageBasedLight.rotationDegrees = sceneView.imageBasedLight.environmentRotationDegrees;
                imageBasedLight.hasEnvironmentTexture = m_HasPrefilteredEnvironment;
                imageBasedLight.lowerHemisphereIsSolidColor = sceneView.imageBasedLight.lowerHemisphereIsSolidColor;
                imageBasedLight.lowerHemisphereColor = sceneView.imageBasedLight.lowerHemisphereColor;
                imageBasedLight.exposureMultiplier = sceneView.imageBasedLight.exposureMultiplier;
                imageBasedLight.skyboxExposureMultiplier = sceneView.imageBasedLight.skyboxExposureMultiplier;
                imageBasedLight.sunSpecularMultiplier = sceneView.imageBasedLight.sunSpecularMultiplier;
                imageBasedLight.autoExposureEnabled = sceneView.imageBasedLight.autoExposureEnabled;
                imageBasedLight.autoExposureMinEV = sceneView.imageBasedLight.autoExposureMinEV;
                imageBasedLight.autoExposureMaxEV = sceneView.imageBasedLight.autoExposureMaxEV;
                imageBasedLight.autoExposureSpeedUp = sceneView.imageBasedLight.autoExposureSpeedUp;
                imageBasedLight.autoExposureSpeedDown = sceneView.imageBasedLight.autoExposureSpeedDown;
                UpdateAutoExposure(sceneView, imageBasedLight);
                m_LightingSystem.SetImageBasedLight(imageBasedLight);
                PostProcessDesc postProcess;
                if (sceneView.postProcess.active)
                {
                    postProcess.active = true;
                    postProcess.toneMappingEnabled = sceneView.postProcess.toneMappingEnabled;
                    postProcess.toneMappingOperator = sceneView.postProcess.toneMappingOperator;
                    postProcess.exposureCompensationEV = sceneView.postProcess.exposureCompensationEV;
                    postProcess.eyeAdaptationCompensationEV = sceneView.postProcess.eyeAdaptationCompensationEV;
                    postProcess.whitePoint = sceneView.postProcess.whitePoint;
                    postProcess.colorFilter = sceneView.postProcess.colorFilter;
                    postProcess.colorBalance = sceneView.postProcess.colorBalance;
                    postProcess.saturation = sceneView.postProcess.saturation;
                    postProcess.contrast = sceneView.postProcess.contrast;
                    postProcess.gamma = sceneView.postProcess.gamma;
                    postProcess.filmCurveShoulder = sceneView.postProcess.filmCurveShoulder;
                    postProcess.filmCurveLinear = sceneView.postProcess.filmCurveLinear;
                    postProcess.filmCurveToe = sceneView.postProcess.filmCurveToe;
                    postProcess.bloomEnabled = sceneView.postProcess.bloomEnabled;
                    postProcess.bloomIntensity = sceneView.postProcess.bloomIntensity;
                    postProcess.bloomThreshold = sceneView.postProcess.bloomThreshold;
                    postProcess.bloomKnee = sceneView.postProcess.bloomKnee;
                }
                else
                {
                    // Give editor scenes a sane default look before an authored post-process volume exists.
                    postProcess.active = true;
                    postProcess.toneMappingEnabled = true;
                    postProcess.toneMappingOperator = ToneMappingOperator::ACES;
                    postProcess.exposureCompensationEV = 0.45f;
                    postProcess.eyeAdaptationCompensationEV = 0.15f;
                    postProcess.whitePoint = 1.35f;
                    postProcess.colorFilter = { 1.0f, 1.0f, 1.0f };
                    postProcess.colorBalance = { 1.02f, 1.01f, 0.99f };
                    postProcess.saturation = 1.03f;
                    postProcess.contrast = 1.08f;
                    postProcess.gamma = 1.0f;
                    postProcess.filmCurveShoulder = 1.08f;
                    postProcess.filmCurveLinear = 1.0f;
                    postProcess.filmCurveToe = 0.96f;
                    postProcess.bloomEnabled = false;
                    postProcess.bloomIntensity = 0.0f;
                    postProcess.bloomThreshold = 1.1f;
                    postProcess.bloomKnee = 0.6f;
                }
                m_LightingSystem.SetPostProcess(postProcess);

                const Color desiredSceneClearColor = ToColor(sceneView.clearColor);
                if (ColorsDiffer(desiredSceneClearColor, m_SceneClearColor))
                {
                    m_SceneClearColor = desiredSceneClearColor;
                    DestroyScenePassResources();
                }

                bool scenePassRebuilt = false;
                if (!EnsureScenePassResources(sceneView.outputWidth, sceneView.outputHeight, &scenePassRebuilt))
                {
                    return;
                }
                if (scenePassRebuilt && !BuildRenderGraph())
                {
                    return;
                }

                const bool meshStateChanged =
                    sceneView.hasOverrideMesh != m_HadOverrideMesh ||
                    sceneView.overrideMeshRevision != m_OverrideMeshRevision;
                if (meshStateChanged && m_ResourceManager != nullptr)
                {
                    if (m_Mesh != InvalidResourceHandle)
                    {
                        m_ResourceManager->DestroyMesh(m_Mesh, GPUResourceManager::DestroyMode::Deferred);
                        m_Mesh = InvalidResourceHandle;
                    }

                    if (sceneView.hasOverrideMesh &&
                        sceneView.overrideMesh != nullptr &&
                        !sceneView.overrideMesh->vertexData.empty() &&
                        !sceneView.overrideMesh->indexData.empty())
                    {
                        m_Mesh = m_ResourceManager->CreateMesh(
                            *sceneView.overrideMesh,
                            std::string(GetDebugName()) + ".SceneMesh");
                    }

                    m_HadOverrideMesh = sceneView.hasOverrideMesh;
                    m_OverrideMeshRevision = sceneView.overrideMeshRevision;
                }

                const bool skyMeshStateChanged =
                    sceneView.hasSkyMesh != m_HadSkyMesh ||
                    sceneView.skyMeshRevision != m_SkyMeshRevision;
                if (skyMeshStateChanged && m_ResourceManager != nullptr)
                {
                    if (m_SkyMesh != InvalidResourceHandle)
                    {
                        m_ResourceManager->DestroyMesh(m_SkyMesh, GPUResourceManager::DestroyMode::Deferred);
                        m_SkyMesh = InvalidResourceHandle;
                    }

                    if (sceneView.hasSkyMesh &&
                        sceneView.skyMesh != nullptr &&
                        !sceneView.skyMesh->vertexData.empty() &&
                        !sceneView.skyMesh->indexData.empty())
                    {
                        m_SkyMesh = m_ResourceManager->CreateMesh(
                            *sceneView.skyMesh,
                            std::string(GetDebugName()) + ".SkyMesh");
                    }

                    m_HadSkyMesh = sceneView.hasSkyMesh;
                    m_SkyMeshRevision = sceneView.skyMeshRevision;
                }

                const bool gridMeshStateChanged =
                    sceneView.hasGridMesh != m_HadGridMesh ||
                    sceneView.gridMeshRevision != m_GridMeshRevision;
                if (gridMeshStateChanged && m_ResourceManager != nullptr)
                {
                    if (m_GridMesh != InvalidResourceHandle)
                    {
                        m_ResourceManager->DestroyMesh(m_GridMesh, GPUResourceManager::DestroyMode::Deferred);
                        m_GridMesh = InvalidResourceHandle;
                    }

                    if (sceneView.hasGridMesh &&
                        sceneView.gridMesh != nullptr &&
                        !sceneView.gridMesh->vertexData.empty() &&
                        !sceneView.gridMesh->indexData.empty())
                    {
                        m_GridMesh = m_ResourceManager->CreateMesh(
                            *sceneView.gridMesh,
                            std::string(GetDebugName()) + ".GridMesh");
                    }

                    m_HadGridMesh = sceneView.hasGridMesh;
                    m_GridMeshRevision = sceneView.gridMeshRevision;
                }

                SyncRenderItems(sceneView);
                std::string materialPipelineError;
                if (!EnsureMaterialPipelinesForRenderItems(&materialPipelineError))
                {
                    return;
                }
                std::string shadowPipelineError;
                if (!EnsureShadowPipelinesForRenderItems(&shadowPipelineError))
                {
                    return;
                }
                RenderShadowPasses();

                const RenderGraphContext renderGraphContext {
                    *m_RenderBackend,
                    sceneView.timeSeconds,
                    m_RenderGraphFrameIndex++
                };
                if (!m_RenderGraph.Execute(renderGraphContext))
                {
                    return;
                }
            }

            void OnResize(IRenderBackend& renderer, std::uint32_t width, std::uint32_t height) override
            {
                (void)renderer;
                (void)width;
                (void)height;
            }

            void SetStreamingService(Assets::IResourceStreamingService* streamingService) override
            {
                m_TextureSystem.SetStreamingService(streamingService);
            }

        protected:
            virtual Color GetClearColor() const = 0;
            virtual const char* GetPipelineMacro() const = 0;
            virtual float GetAmbientLightIntensity() const = 0;
            virtual float GetDirectionalLightIntensity() const = 0;

        private:
            bool EnsureScenePassResources(
                const std::uint32_t width,
                const std::uint32_t height,
                bool* outRebuilt = nullptr)
            {
                const std::uint32_t targetWidth = std::max<std::uint32_t>(width, 1u);
                const std::uint32_t targetHeight = std::max<std::uint32_t>(height, 1u);
                const MaterialPipelineResources* const defaultMaterialPipelines = GetDefaultMaterialPipelineResources();
                const bool resourcesValid =
                    m_SceneColorRenderTarget != InvalidResourceHandle &&
                    m_SceneFramebuffer != InvalidResourceHandle &&
                    m_SceneRenderPass != InvalidResourceHandle &&
                    defaultMaterialPipelines != nullptr &&
                    defaultMaterialPipelines->opaquePipelineState != InvalidResourceHandle &&
                    defaultMaterialPipelines->translucentPipelineState != InvalidResourceHandle &&
                    defaultMaterialPipelines->additivePipelineState != InvalidResourceHandle &&
                    defaultMaterialPipelines->modulatePipelineState != InvalidResourceHandle &&
                    m_GridPipelineState != InvalidResourceHandle &&
                    m_SkyPipelineState != InvalidResourceHandle &&
                    m_GridDescriptorSet != InvalidResourceHandle &&
                    defaultMaterialPipelines->opaqueDescriptorSet != InvalidResourceHandle &&
                    defaultMaterialPipelines->translucentDescriptorSet != InvalidResourceHandle &&
                    defaultMaterialPipelines->additiveDescriptorSet != InvalidResourceHandle &&
                    defaultMaterialPipelines->modulateDescriptorSet != InvalidResourceHandle &&
                    m_SkyDescriptorSet != InvalidResourceHandle;
                const bool resizeRequired =
                    !resourcesValid ||
                    targetWidth != m_SceneColorTargetWidth ||
                    targetHeight != m_SceneColorTargetHeight;
                if (outRebuilt != nullptr)
                {
                    *outRebuilt = resizeRequired;
                }
                if (!resizeRequired)
                {
                    return true;
                }

                DestroyScenePassResources();
                if (m_ResourceManager == nullptr)
                {
                    return false;
                }

                RenderTargetDesc sceneColorTargetDesc;
                sceneColorTargetDesc.debugName = std::string(GetDebugName()) + ".SceneColorTarget";
                sceneColorTargetDesc.width = targetWidth;
                sceneColorTargetDesc.height = targetHeight;
                sceneColorTargetDesc.format = GpuTextureFormat::RGBA16F;
                sceneColorTargetDesc.srgb = false;
                sceneColorTargetDesc.clearColor = m_SceneClearColor;
                m_SceneColorRenderTarget = m_ResourceManager->CreateRenderTarget(sceneColorTargetDesc);
                if (m_SceneColorRenderTarget == InvalidResourceHandle)
                {
                    DestroyScenePassResources();
                    return false;
                }

                FramebufferDesc sceneFramebufferDesc;
                sceneFramebufferDesc.debugName = std::string(GetDebugName()) + ".SceneFramebuffer";
                sceneFramebufferDesc.colorTarget = m_SceneColorRenderTarget;
                m_SceneFramebuffer = m_ResourceManager->CreateFramebuffer(sceneFramebufferDesc);
                if (m_SceneFramebuffer == InvalidResourceHandle)
                {
                    DestroyScenePassResources();
                    return false;
                }

                RenderPassDesc sceneRenderPassDesc;
                sceneRenderPassDesc.debugName = std::string(GetDebugName()) + ".ScenePass";
                sceneRenderPassDesc.clearColor = m_SceneClearColor;
                sceneRenderPassDesc.framebuffer = m_SceneFramebuffer;
                m_SceneRenderPass = m_ResourceManager->CreateRenderPass(sceneRenderPassDesc);
                if (m_SceneRenderPass == InvalidResourceHandle)
                {
                    DestroyScenePassResources();
                    return false;
                }

                m_GridPipelineState = m_ResourceManager->CreatePipelineState(
                    BuildGridPipelineDesc(GetDebugName(), m_SceneRenderPass, m_GridShaderProgramDesc));
                m_SkyPipelineState = m_ResourceManager->CreatePipelineState(
                    BuildSkyPipelineDesc(GetDebugName(), m_SceneRenderPass, m_SkyShaderProgramDesc));
                if (m_GridPipelineState == InvalidResourceHandle ||
                    m_SkyPipelineState == InvalidResourceHandle)
                {
                    DestroyScenePassResources();
                    return false;
                }

                DescriptorSetDesc skyDescriptorDesc;
                skyDescriptorDesc.buffers.push_back(
                    DescriptorBufferWrite { 0, std::vector<std::uint8_t>(sizeof(PerDrawData), 0) });
                m_SkyDescriptorSet = m_ResourceManager->CreateDescriptorSet(
                    m_SkyPipelineState,
                    skyDescriptorDesc,
                    std::string(GetDebugName()) + ".Sky");
                if (m_SkyDescriptorSet == InvalidResourceHandle)
                {
                    DestroyScenePassResources();
                    return false;
                }

                if (!EnsureDefaultMaterialPipelineResources())
                {
                    DestroyScenePassResources();
                    return false;
                }

                DescriptorSetDesc gridDescriptorDesc;
                gridDescriptorDesc.buffers.push_back(
                    DescriptorBufferWrite { 0, std::vector<std::uint8_t>(sizeof(PerDrawData), 0) });
                m_GridDescriptorSet = m_ResourceManager->CreateDescriptorSet(
                    m_GridPipelineState,
                    gridDescriptorDesc,
                    std::string(GetDebugName()) + ".Grid");
                if (m_GridDescriptorSet == InvalidResourceHandle)
                {
                    DestroyScenePassResources();
                    return false;
                }

                m_SceneColorTargetWidth = targetWidth;
                m_SceneColorTargetHeight = targetHeight;
                return true;
            }

            void DestroyScenePassResources()
            {
                if (m_ResourceManager == nullptr)
                {
                    m_SceneColorRenderTarget = InvalidResourceHandle;
                    m_SceneFramebuffer = InvalidResourceHandle;
                    m_SceneRenderPass = InvalidResourceHandle;
                    m_SkyPipelineState = InvalidResourceHandle;
                    m_SkyDescriptorSet = InvalidResourceHandle;
                    m_MaterialPipelines.clear();
                    m_ShadowMaterialPipelines.clear();
                    m_DefaultMaterialPipelineKey.clear();
                    m_DefaultShadowPipelineKey.clear();
                    m_LastMaterialTextureBindingHashes.clear();
                    m_LastShadowTextureBindingHashes.clear();
                    m_SceneColorTargetWidth = 0;
                    m_SceneColorTargetHeight = 0;
                    return;
                }

                if (m_SkyDescriptorSet != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyDescriptorSet(m_SkyDescriptorSet, GPUResourceManager::DestroyMode::Deferred);
                    m_SkyDescriptorSet = InvalidResourceHandle;
                }
                if (m_GridDescriptorSet != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyDescriptorSet(m_GridDescriptorSet, GPUResourceManager::DestroyMode::Deferred);
                    m_GridDescriptorSet = InvalidResourceHandle;
                }
                if (m_SkyPipelineState != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyPipelineState(m_SkyPipelineState, GPUResourceManager::DestroyMode::Deferred);
                    m_SkyPipelineState = InvalidResourceHandle;
                }
                if (m_GridPipelineState != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyPipelineState(m_GridPipelineState, GPUResourceManager::DestroyMode::Deferred);
                    m_GridPipelineState = InvalidResourceHandle;
                }
                for (auto& [_, resources] : m_MaterialPipelines)
                {
                    DestroyMaterialPipelineResources(resources);
                }
                for (auto& [_, resources] : m_ShadowMaterialPipelines)
                {
                    DestroyShadowPipelineResources(resources);
                }
                m_MaterialPipelines.clear();
                m_ShadowMaterialPipelines.clear();
                m_DefaultMaterialPipelineKey.clear();
                m_DefaultShadowPipelineKey.clear();
                m_LastMaterialTextureBindingHashes.clear();
                m_LastShadowTextureBindingHashes.clear();
                if (m_SceneRenderPass != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyRenderPass(m_SceneRenderPass, GPUResourceManager::DestroyMode::Deferred);
                    m_SceneRenderPass = InvalidResourceHandle;
                }
                if (m_SceneFramebuffer != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyFramebuffer(m_SceneFramebuffer, GPUResourceManager::DestroyMode::Deferred);
                    m_SceneFramebuffer = InvalidResourceHandle;
                }
                if (m_SceneColorRenderTarget != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyRenderTarget(m_SceneColorRenderTarget, GPUResourceManager::DestroyMode::Deferred);
                    m_SceneColorRenderTarget = InvalidResourceHandle;
                }
                m_SceneColorTargetWidth = 0;
                m_SceneColorTargetHeight = 0;
            }

            bool ApplyPostProcessBindings()
            {
                if (m_PostProcessDescriptorSet == InvalidResourceHandle || m_ResourceManager == nullptr)
                {
                    return false;
                }

                TextureHandle sceneColorTexture = m_DefaultAlbedoTexture;
                if (m_RenderBackend != nullptr && m_SceneColorRenderTarget != InvalidResourceHandle)
                {
                    if (const TextureHandle resolved =
                            m_RenderBackend->GetRenderTargetTextureHandle(m_SceneColorRenderTarget);
                        resolved != InvalidTextureHandle)
                    {
                        sceneColorTexture = resolved;
                    }
                }

                DescriptorSetDesc updateDesc;
                updateDesc.images.push_back(DescriptorImageWrite { 1, sceneColorTexture });
                if (!m_LightingSystem.BuildDescriptorSetDesc(updateDesc))
                {
                    return false;
                }

                m_ResourceManager->UpdateDescriptorSet(m_PostProcessDescriptorSet, updateDesc);
                return true;
            }

            struct RuntimeRenderItem
            {
                std::uint64_t revision = 0;
                MeshHandle mesh = InvalidResourceHandle;
                TextureHandle bakedLightmapTexture = InvalidTextureHandle;
                std::uint64_t bakedLightmapRevision = 0;
                std::array<float, 3> worldPosition { 0.0f, 0.0f, 0.0f };
                std::array<float, 16> worldTransform = kIdentityMatrix;
                std::string materialPipelineKey;
                std::string shadowPipelineKey;
                MaterialRenderProxy material;
            };

            struct SharedMeshResource
            {
                std::uint64_t revision = 0;
                MeshHandle handle = InvalidResourceHandle;
            };

            struct MaterialPipelineResources
            {
                std::string key;
                ShaderProgramDesc shaderProgram {};
                PipelineStateHandle opaquePipelineState = InvalidResourceHandle;
                PipelineStateHandle translucentPipelineState = InvalidResourceHandle;
                PipelineStateHandle additivePipelineState = InvalidResourceHandle;
                PipelineStateHandle modulatePipelineState = InvalidResourceHandle;
                DescriptorSetHandle opaqueDescriptorSet = InvalidResourceHandle;
                DescriptorSetHandle translucentDescriptorSet = InvalidResourceHandle;
                DescriptorSetHandle additiveDescriptorSet = InvalidResourceHandle;
                DescriptorSetHandle modulateDescriptorSet = InvalidResourceHandle;
            };

            struct ShadowPipelineResources
            {
                std::string key;
                ShaderProgramDesc shaderProgram {};
                PipelineStateHandle directionalPipelineState = InvalidResourceHandle;
                PipelineStateHandle pointPipelineState = InvalidResourceHandle;
                PipelineStateHandle spotPipelineState = InvalidResourceHandle;
                DescriptorSetHandle directionalDescriptorSet = InvalidResourceHandle;
                DescriptorSetHandle pointDescriptorSet = InvalidResourceHandle;
                DescriptorSetHandle spotDescriptorSet = InvalidResourceHandle;
            };

            TextureHandle ResolveMaterialTexture(
                const std::filesystem::path& sourcePath,
                const bool srgb,
                const TextureHandle fallback)
            {
                if (sourcePath.empty())
                {
                    return fallback;
                }

                auto resolveTexturePath = [](const std::filesystem::path& inputPath) -> std::filesystem::path
                {
                    auto preferExistingTextureVariant = [](const std::filesystem::path& candidate) -> std::filesystem::path
                    {
                        auto lowercaseExtension = [](std::string value)
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
                        };

                        std::error_code ec;
                        if (std::filesystem::exists(candidate, ec))
                        {
                            const std::filesystem::path normalized = std::filesystem::weakly_canonical(candidate, ec);
                            return ec ? candidate.lexically_normal() : normalized.lexically_normal();
                        }

                        if (lowercaseExtension(candidate.extension().string()) != ".lumatex")
                        {
                            const std::filesystem::path lumatexCandidate = candidate.parent_path() / (candidate.stem().string() + ".lumatex");
                            if (std::filesystem::exists(lumatexCandidate, ec))
                            {
                                const std::filesystem::path normalized = std::filesystem::weakly_canonical(lumatexCandidate, ec);
                                return ec ? lumatexCandidate.lexically_normal() : normalized.lexically_normal();
                            }
                        }

                        return {};
                    };

                    if (inputPath.empty())
                    {
                        return {};
                    }

                    if (inputPath.is_absolute())
                    {
                        return preferExistingTextureVariant(inputPath);
                    }

                    if (Project::IsLoaded())
                    {
                        if (const std::filesystem::path resolvedFromAssets =
                                preferExistingTextureVariant(Project::GetAssetsPath() / inputPath);
                            !resolvedFromAssets.empty())
                        {
                            return resolvedFromAssets;
                        }

                        if (const std::filesystem::path resolvedFromProject =
                                preferExistingTextureVariant(Project::GetProjectRoot() / inputPath);
                            !resolvedFromProject.empty())
                        {
                            return resolvedFromProject;
                        }
                    }

                    return preferExistingTextureVariant(inputPath);
                };

                const std::filesystem::path resolvedSourcePath = resolveTexturePath(sourcePath);
                if (resolvedSourcePath.empty())
                {
                    return fallback;
                }

                const std::string key =
                    std::string(srgb ? "srgb:" : "linear:") + resolvedSourcePath.generic_string();
                const auto cachedIt = m_ExternalTextures.find(key);
                if (cachedIt != m_ExternalTextures.end())
                {
                    return cachedIt->second;
                }

                const TextureHandle texture = m_TextureSystem.CreateExternalTextureReference(
                    resolvedSourcePath,
                    srgb,
                    resolvedSourcePath.filename().string());
                if (texture == InvalidTextureHandle)
                {
                    return fallback;
                }

                m_ExternalTextures.emplace(key, texture);
                return texture;
            }

            TextureHandle ResolveSourceEnvironmentTexture(const SceneImageBasedLightView& imageBasedLight)
            {
                if (!imageBasedLight.enabled)
                {
                    return InvalidTextureHandle;
                }

                return ResolveMaterialTexture(
                    imageBasedLight.environmentTexture,
                    true,
                    InvalidTextureHandle);
            }

            TextureHandle ResolveExplicitIrradianceTexture(const SceneImageBasedLightView& imageBasedLight)
            {
                if (!imageBasedLight.enabled)
                {
                    return InvalidTextureHandle;
                }

                return ResolveMaterialTexture(
                    imageBasedLight.irradianceTexture,
                    true,
                    InvalidTextureHandle);
            }

            TextureHandle ResolveExplicitPrefilterTexture(const SceneImageBasedLightView& imageBasedLight)
            {
                if (!imageBasedLight.enabled)
                {
                    return InvalidTextureHandle;
                }

                return ResolveMaterialTexture(
                    imageBasedLight.prefilteredReflectionTexture,
                    true,
                    InvalidTextureHandle);
            }

            void ResetEnvironmentMaps()
            {
                m_CurrentIrradianceTexture = m_DefaultIrradianceTexture;
                m_CurrentPrefilterTexture = m_DefaultPrefilterTexture;
                m_CurrentEnvironmentSourcePath.clear();
                m_CurrentEnvironmentSourceWidth = 0;
                m_CurrentEnvironmentSourceHeight = 0;
                m_HasPrefilteredEnvironment = false;
            }

            void RefreshImageBasedLighting(const SceneImageBasedLightView& imageBasedLight)
            {
                if (!imageBasedLight.enabled)
                {
                    ResetEnvironmentMaps();
                    return;
                }

                const TextureHandle explicitIrradianceTexture = ResolveExplicitIrradianceTexture(imageBasedLight);
                const TextureHandle explicitPrefilterTexture = ResolveExplicitPrefilterTexture(imageBasedLight);
                if (explicitIrradianceTexture != InvalidTextureHandle || explicitPrefilterTexture != InvalidTextureHandle)
                {
                    m_CurrentIrradianceTexture =
                        explicitIrradianceTexture != InvalidTextureHandle
                            ? explicitIrradianceTexture
                            : m_DefaultIrradianceTexture;
                    m_CurrentPrefilterTexture =
                        explicitPrefilterTexture != InvalidTextureHandle
                            ? explicitPrefilterTexture
                            : m_DefaultPrefilterTexture;
                    m_CurrentEnvironmentSourcePath.clear();
                    m_CurrentEnvironmentSourceWidth = 0;
                    m_CurrentEnvironmentSourceHeight = 0;
                    m_HasPrefilteredEnvironment = true;
                    return;
                }

                if (m_CurrentEnvironmentTexture == InvalidTextureHandle)
                {
                    ResetEnvironmentMaps();
                    return;
                }

                TextureMetadata sourceMetadata;
                if (!m_TextureSystem.TryGetMetadata(m_CurrentEnvironmentTexture, sourceMetadata) ||
                    sourceMetadata.width < 2 || sourceMetadata.height < 2 ||
                    sourceMetadata.pixelDataSize !=
                        static_cast<std::size_t>(sourceMetadata.width) * static_cast<std::size_t>(sourceMetadata.height) * 4ULL)
                {
                    ResetEnvironmentMaps();
                    return;
                }

                const std::filesystem::path sourcePath =
                    sourceMetadata.sourcePath.empty() ? imageBasedLight.environmentTexture : sourceMetadata.sourcePath;
                if (!imageBasedLight.forceRebuild &&
                    m_HasPrefilteredEnvironment &&
                    sourcePath == m_CurrentEnvironmentSourcePath &&
                    sourceMetadata.width == m_CurrentEnvironmentSourceWidth &&
                    sourceMetadata.height == m_CurrentEnvironmentSourceHeight)
                {
                    return;
                }

                TextureCreateDesc sourceDesc;
                if (!m_TextureSystem.TryGetDesc(m_CurrentEnvironmentTexture, sourceDesc))
                {
                    ResetEnvironmentMaps();
                    return;
                }

                const FloatImage sourceImage = DecodeFloatImage(sourceDesc);
                const FloatImage irradianceImage = BuildIrradianceImage(sourceImage);
                const FloatImage prefilteredAtlas = BuildPrefilteredAtlas(sourceImage);

                TextureCreateDesc irradianceDesc;
                irradianceDesc.debugName = std::string(GetDebugName()) + ".Irradiance";
                irradianceDesc.width = irradianceImage.width;
                irradianceDesc.height = irradianceImage.height;
                irradianceDesc.srgb = true;
                irradianceDesc.pixelData = EncodeRGBA8(irradianceImage);

                TextureCreateDesc prefilterDesc;
                prefilterDesc.debugName = std::string(GetDebugName()) + ".PrefilteredEnvironment";
                prefilterDesc.width = prefilteredAtlas.width;
                prefilterDesc.height = prefilteredAtlas.height;
                prefilterDesc.srgb = true;
                prefilterDesc.pixelData = EncodeRGBA8(prefilteredAtlas);

                if (m_CurrentIrradianceTexture == InvalidTextureHandle || m_CurrentIrradianceTexture == m_DefaultIrradianceTexture)
                {
                    m_CurrentIrradianceTexture = m_TextureSystem.CreateTexture2D(irradianceDesc);
                }
                else
                {
                    m_TextureSystem.UpdateTexture2D(m_CurrentIrradianceTexture, irradianceDesc);
                }

                if (m_CurrentPrefilterTexture == InvalidTextureHandle || m_CurrentPrefilterTexture == m_DefaultPrefilterTexture)
                {
                    m_CurrentPrefilterTexture = m_TextureSystem.CreateTexture2D(prefilterDesc);
                }
                else
                {
                    m_TextureSystem.UpdateTexture2D(m_CurrentPrefilterTexture, prefilterDesc);
                }

                if (m_CurrentIrradianceTexture == InvalidTextureHandle || m_CurrentPrefilterTexture == InvalidTextureHandle)
                {
                    ResetEnvironmentMaps();
                    return;
                }

                m_CurrentEnvironmentSourcePath = sourcePath;
                m_CurrentEnvironmentSourceWidth = sourceMetadata.width;
                m_CurrentEnvironmentSourceHeight = sourceMetadata.height;
                m_HasPrefilteredEnvironment = true;
            }

            float EstimateSceneLuminance(const SceneView& sceneView) const
            {
                float luminance = Luminance(sceneView.ambientLightColor) * std::max(sceneView.ambientLightIntensity, 0.0f);

                if (sceneView.imageBasedLight.enabled)
                {
                    luminance +=
                        Luminance(sceneView.imageBasedLight.diffuseColor) *
                        std::max(sceneView.imageBasedLight.diffuseIntensity, 0.0f) *
                        0.75f;
                    luminance +=
                        Luminance(sceneView.imageBasedLight.specularColor) *
                        std::max(sceneView.imageBasedLight.specularIntensity, 0.0f) *
                        0.25f;
                }

                if (sceneView.directionalLight.enabled)
                {
                    luminance +=
                        Luminance(sceneView.directionalLight.color) *
                        std::max(sceneView.directionalLight.intensity, 0.0f) *
                        0.65f;
                }

                float localLuminance = 0.0f;
                for (std::size_t index = 0; index < sceneView.spotLightCount; ++index)
                {
                    const SpotLightDesc& light = sceneView.spotLights[index];
                    if (!light.enabled)
                    {
                        continue;
                    }

                    const float coneSpan = std::max(light.outerConeAngleDegrees - light.innerConeAngleDegrees, 1.0f);
                    const float coneWeight = std::clamp(coneSpan / 45.0f, 0.2f, 1.5f);
                    const float rangeWeight = std::clamp(light.range * 0.06f, 0.15f, 2.0f);
                    localLuminance +=
                        Luminance(light.color) *
                        std::max(light.intensity, 0.0f) *
                        coneWeight *
                        rangeWeight;
                }

                luminance += localLuminance * 0.18f;
                return std::max(luminance, 1.0e-4f);
            }

            void UpdateAutoExposure(const SceneView& sceneView, ImageBasedLightDesc& imageBasedLight)
            {
                const float manualExposure = std::max(sceneView.imageBasedLight.exposureMultiplier, 1.0e-4f);
                if (!sceneView.imageBasedLight.autoExposureEnabled)
                {
                    m_AutoExposureInitialized = false;
                    m_LastAutoExposureTimeSeconds = sceneView.timeSeconds;
                    imageBasedLight.exposureMultiplier = manualExposure;
                    return;
                }

                const float estimatedLuminance = EstimateSceneLuminance(sceneView);
                const float keyValue = 0.18f * manualExposure;
                const float unclampedTargetEV = std::log2(std::max(keyValue / estimatedLuminance, 1.0e-4f));
                const float targetEV =
                    std::clamp(
                        unclampedTargetEV,
                        sceneView.imageBasedLight.autoExposureMinEV,
                        sceneView.imageBasedLight.autoExposureMaxEV);

                float deltaTime = 1.0f / 60.0f;
                if (m_AutoExposureInitialized)
                {
                    deltaTime = std::clamp(sceneView.timeSeconds - m_LastAutoExposureTimeSeconds, 1.0f / 240.0f, 0.25f);
                }

                if (!m_AutoExposureInitialized)
                {
                    m_CurrentAutoExposureEV = targetEV;
                    m_AutoExposureInitialized = true;
                }
                else
                {
                    const float speed =
                        targetEV > m_CurrentAutoExposureEV
                            ? std::max(sceneView.imageBasedLight.autoExposureSpeedUp, 0.05f)
                            : std::max(sceneView.imageBasedLight.autoExposureSpeedDown, 0.05f);
                    const float response = 1.0f - std::exp(-speed * deltaTime);
                    m_CurrentAutoExposureEV += (targetEV - m_CurrentAutoExposureEV) * response;
                }

                m_LastAutoExposureTimeSeconds = sceneView.timeSeconds;
                imageBasedLight.exposureMultiplier = std::exp2(m_CurrentAutoExposureEV);
            }

            std::uint32_t ResolveDesiredPointShadowMapSize(const SceneView& sceneView) const
            {
                for (std::size_t index = 0; index < sceneView.pointLightCount; ++index)
                {
                    const PointLightDesc& pointLight = sceneView.pointLights[index];
                    if (!pointLight.enabled || !pointLight.castsShadows || pointLight.range <= 0.05f)
                    {
                        continue;
                    }

                    return NormalizePointShadowResolution(pointLight.shadowResolution);
                }

                return kPointShadowMapSize;
            }

            void InvalidateShadowPipelineCaches()
            {
                for (auto& [_, resources] : m_ShadowMaterialPipelines)
                {
                    DestroyShadowPipelineResources(resources);
                }
                m_ShadowMaterialPipelines.clear();
                m_DefaultShadowPipelineKey.clear();
                m_LastShadowTextureBindingHashes.clear();
            }

            void DestroyPointShadowResources()
            {
                if (m_ResourceManager == nullptr)
                {
                    m_PointShadowDescriptorSet = InvalidResourceHandle;
                    m_PointShadowPipelineState = InvalidResourceHandle;
                    m_PointShadowRenderPass = InvalidResourceHandle;
                    m_PointShadowFramebuffer = InvalidResourceHandle;
                    m_PointShadowRenderTarget = InvalidResourceHandle;
                    return;
                }

                if (m_PointShadowDescriptorSet != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyDescriptorSet(m_PointShadowDescriptorSet, GPUResourceManager::DestroyMode::Deferred);
                    m_PointShadowDescriptorSet = InvalidResourceHandle;
                }
                if (m_PointShadowPipelineState != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyPipelineState(m_PointShadowPipelineState, GPUResourceManager::DestroyMode::Deferred);
                    m_PointShadowPipelineState = InvalidResourceHandle;
                }
                if (m_PointShadowRenderPass != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyRenderPass(m_PointShadowRenderPass, GPUResourceManager::DestroyMode::Deferred);
                    m_PointShadowRenderPass = InvalidResourceHandle;
                }
                if (m_PointShadowFramebuffer != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyFramebuffer(m_PointShadowFramebuffer, GPUResourceManager::DestroyMode::Deferred);
                    m_PointShadowFramebuffer = InvalidResourceHandle;
                }
                if (m_PointShadowRenderTarget != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyRenderTarget(m_PointShadowRenderTarget, GPUResourceManager::DestroyMode::Deferred);
                    m_PointShadowRenderTarget = InvalidResourceHandle;
                }
            }

            bool EnsurePointShadowResources(const std::uint32_t desiredMapSize)
            {
                if (m_ResourceManager == nullptr)
                {
                    return false;
                }

                const std::uint32_t normalizedMapSize = NormalizePointShadowResolution(desiredMapSize);
                const bool resourcesMissing =
                    m_PointShadowRenderTarget == InvalidResourceHandle ||
                    m_PointShadowFramebuffer == InvalidResourceHandle ||
                    m_PointShadowRenderPass == InvalidResourceHandle ||
                    m_PointShadowPipelineState == InvalidResourceHandle ||
                    m_PointShadowDescriptorSet == InvalidResourceHandle;
                if (!resourcesMissing && m_PointShadowMapSize == normalizedMapSize)
                {
                    return true;
                }

                InvalidateShadowPipelineCaches();
                DestroyPointShadowResources();

                RenderTargetDesc pointShadowTargetDesc;
                pointShadowTargetDesc.debugName = std::string(GetDebugName()) + ".PointShadowTarget";
                pointShadowTargetDesc.width = normalizedMapSize * kPointShadowAtlasColumns;
                pointShadowTargetDesc.height = normalizedMapSize * kPointShadowAtlasRows;
                pointShadowTargetDesc.srgb = false;
                pointShadowTargetDesc.clearColor = { 1.0f, 1.0f, 1.0f, 1.0f };
                m_PointShadowRenderTarget = m_ResourceManager->CreateRenderTarget(pointShadowTargetDesc);
                if (m_PointShadowRenderTarget == InvalidResourceHandle)
                {
                    DestroyPointShadowResources();
                    return false;
                }

                FramebufferDesc pointShadowFramebufferDesc;
                pointShadowFramebufferDesc.debugName = std::string(GetDebugName()) + ".PointShadowFramebuffer";
                pointShadowFramebufferDesc.colorTarget = m_PointShadowRenderTarget;
                m_PointShadowFramebuffer = m_ResourceManager->CreateFramebuffer(pointShadowFramebufferDesc);
                if (m_PointShadowFramebuffer == InvalidResourceHandle)
                {
                    DestroyPointShadowResources();
                    return false;
                }

                RenderPassDesc pointShadowPassDesc;
                pointShadowPassDesc.debugName = std::string(GetDebugName()) + ".PointShadowPass";
                pointShadowPassDesc.clearColor = { 1.0f, 1.0f, 1.0f, 1.0f };
                pointShadowPassDesc.framebuffer = m_PointShadowFramebuffer;
                m_PointShadowRenderPass = m_ResourceManager->CreateRenderPass(pointShadowPassDesc);
                if (m_PointShadowRenderPass == InvalidResourceHandle)
                {
                    DestroyPointShadowResources();
                    return false;
                }

                m_PointShadowPipelineState = m_ResourceManager->CreatePipelineState(
                    BuildShadowPipelineDesc(GetDebugName(), m_PointShadowRenderPass, m_ShadowShaderProgramDesc));
                if (m_PointShadowPipelineState == InvalidResourceHandle)
                {
                    DestroyPointShadowResources();
                    return false;
                }

                DescriptorSetDesc pointShadowDescriptorDesc;
                if (!BuildShadowDescriptorSetDesc(pointShadowDescriptorDesc))
                {
                    DestroyPointShadowResources();
                    return false;
                }

                m_PointShadowDescriptorSet = m_ResourceManager->CreateDescriptorSet(
                    m_PointShadowPipelineState,
                    pointShadowDescriptorDesc,
                    std::string(GetDebugName()) + ".PointShadow");
                if (m_PointShadowDescriptorSet == InvalidResourceHandle)
                {
                    DestroyPointShadowResources();
                    return false;
                }

                m_PointShadowMapSize = normalizedMapSize;
                return true;
            }

            void UpdateShadowState(const SceneView& sceneView)
            {
                m_HasDirectionalShadow = false;
                m_HasPointShadow = false;
                m_HasSpotShadow = false;
                m_ShadowedPointLightIndex = -1;
                m_ShadowedSpotLightIndex = -1;
                m_PointShadowBias = 0.0025f;
                m_PointShadowSoftShadows = true;
                m_DirectionalShadowTextureMatrix = Mat4 {}.elements;
                for (auto& matrix : m_PointShadowViewProjections)
                {
                    matrix = Mat4 {}.elements;
                }
                for (auto& matrix : m_PointShadowTextureMatrices)
                {
                    matrix = Mat4 {}.elements;
                }
                m_PointShadowLightPositionRange = { 0.0f, 0.0f, 0.0f, 1.0f };
                m_SpotShadowTextureMatrix = Mat4 {}.elements;

                if (m_RenderBackend == nullptr)
                {
                    return;
                }

                const RendererAPI api = m_RenderBackend->GetAPI();
                const Mat4 shadowBias = BuildShadowTextureMatrix(api);
                const Vec3 cameraPosition {
                    sceneView.cameraWorldPosition[0],
                    sceneView.cameraWorldPosition[1],
                    sceneView.cameraWorldPosition[2]
                };

                if (sceneView.directionalLight.enabled && sceneView.directionalLight.castsShadows)
                {
                    const Vec3 lightDirection = Normalize({
                        sceneView.directionalLight.direction[0],
                        sceneView.directionalLight.direction[1],
                        sceneView.directionalLight.direction[2]
                    });
                    const Vec3 up =
                        std::abs(lightDirection.y) > 0.95f
                            ? Vec3 { 0.0f, 0.0f, 1.0f }
                            : Vec3 { 0.0f, 1.0f, 0.0f };
                    constexpr float kDirectionalShadowRadius = 32.0f;
                    constexpr float kDirectionalShadowDistance = 54.0f;
                    const Vec3 lightPosition = cameraPosition - lightDirection * kDirectionalShadowDistance;
                    const Mat4 lightView = BuildLookAt(lightPosition, cameraPosition, up);
                    const Mat4 lightProjection = BuildOrthographic(
                        -kDirectionalShadowRadius,
                        kDirectionalShadowRadius,
                        -kDirectionalShadowRadius,
                        kDirectionalShadowRadius,
                        1.0f,
                        140.0f);
                    const Mat4 lightViewProjection = Multiply(lightProjection, lightView);
                    const Mat4 textureMatrix = Multiply(shadowBias, lightViewProjection);
                    m_DirectionalShadowViewProjection = lightViewProjection.elements;
                      m_DirectionalShadowTextureMatrix = textureMatrix.elements;
                      m_HasDirectionalShadow = true;
                  }

                constexpr std::array<Vec3, LightingSystem::kPointShadowFaceCount> kPointShadowDirections {{
                    { 1.0f, 0.0f, 0.0f },
                    { -1.0f, 0.0f, 0.0f },
                    { 0.0f, 1.0f, 0.0f },
                    { 0.0f, -1.0f, 0.0f },
                    { 0.0f, 0.0f, 1.0f },
                    { 0.0f, 0.0f, -1.0f }
                }};
                constexpr std::array<Vec3, LightingSystem::kPointShadowFaceCount> kPointShadowUps {{
                    { 0.0f, -1.0f, 0.0f },
                    { 0.0f, -1.0f, 0.0f },
                    { 0.0f, 0.0f, 1.0f },
                    { 0.0f, 0.0f, -1.0f },
                    { 0.0f, -1.0f, 0.0f },
                    { 0.0f, -1.0f, 0.0f }
                }};
                for (std::size_t index = 0; index < sceneView.pointLightCount; ++index)
                {
                    const PointLightDesc& pointLight = sceneView.pointLights[index];
                    if (!pointLight.enabled || !pointLight.castsShadows || pointLight.range <= 0.05f)
                    {
                        continue;
                    }

                    const Vec3 lightPosition { pointLight.position[0], pointLight.position[1], pointLight.position[2] };
                    const float lightRange = std::max(pointLight.range, 0.1f);
                    const Mat4 lightProjection =
                        BuildPerspective(3.14159265359f * 0.5f, 1.0f, 0.05f, lightRange);

                    for (std::size_t faceIndex = 0; faceIndex < LightingSystem::kPointShadowFaceCount; ++faceIndex)
                    {
                        const Mat4 lightView = BuildLookAt(
                            lightPosition,
                            lightPosition + kPointShadowDirections[faceIndex],
                            kPointShadowUps[faceIndex]);
                        const Mat4 lightViewProjection = Multiply(lightProjection, lightView);
                        const float atlasScaleX = 1.0f / static_cast<float>(kPointShadowAtlasColumns);
                        const float atlasScaleY = 1.0f / static_cast<float>(kPointShadowAtlasRows);
                        const float atlasOffsetX =
                            static_cast<float>(faceIndex % kPointShadowAtlasColumns) * atlasScaleX;
                        const float atlasOffsetY =
                            static_cast<float>(faceIndex / kPointShadowAtlasColumns) * atlasScaleY;
                        const Mat4 atlasTransform =
                            BuildAtlasTransform(atlasOffsetX, atlasOffsetY, atlasScaleX, atlasScaleY);
                        const Mat4 textureMatrix = Multiply(atlasTransform, Multiply(shadowBias, lightViewProjection));
                        m_PointShadowViewProjections[faceIndex] = lightViewProjection.elements;
                        m_PointShadowTextureMatrices[faceIndex] = textureMatrix.elements;
                    }

                    m_PointShadowLightPositionRange = {
                        pointLight.position[0],
                        pointLight.position[1],
                        pointLight.position[2],
                        lightRange
                    };
                    m_PointShadowBias = std::max(pointLight.shadowBias, 0.0f);
                    m_PointShadowSoftShadows = pointLight.softShadows;
                    m_HasPointShadow = true;
                    m_ShadowedPointLightIndex = static_cast<int>(index);
                    break;
                }

                for (std::size_t index = 0; index < sceneView.spotLightCount; ++index)
                {
                    const SpotLightDesc& spotLight = sceneView.spotLights[index];
                    if (!spotLight.enabled || !spotLight.castsShadows)
                    {
                        continue;
                    }

                    const Vec3 lightPosition { spotLight.position[0], spotLight.position[1], spotLight.position[2] };
                    const Vec3 lightDirection = Normalize({ spotLight.direction[0], spotLight.direction[1], spotLight.direction[2] });
                    const Vec3 up =
                        std::abs(lightDirection.y) > 0.95f
                            ? Vec3 { 0.0f, 0.0f, 1.0f }
                            : Vec3 { 0.0f, 1.0f, 0.0f };
                    const Mat4 lightView = BuildLookAt(lightPosition, lightPosition + lightDirection, up);
                    const float fovRadians =
                        std::clamp(spotLight.outerConeAngleDegrees * 2.1f, 5.0f, 170.0f) *
                        (3.14159265359f / 180.0f);
                    const Mat4 lightProjection =
                        BuildPerspective(fovRadians, 1.0f, 0.1f, std::max(spotLight.range, 1.0f));
                    const Mat4 lightViewProjection = Multiply(lightProjection, lightView);
                    const Mat4 textureMatrix = Multiply(shadowBias, lightViewProjection);
                    m_SpotShadowViewProjection = lightViewProjection.elements;
                    m_SpotShadowTextureMatrix = textureMatrix.elements;
                    m_HasSpotShadow = true;
                    m_ShadowedSpotLightIndex = static_cast<int>(index);
                    break;
                }
            }

            void RenderShadowPasses()
            {
                if (m_RenderBackend == nullptr || m_ResourceManager == nullptr)
                {
                    return;
                }

                auto renderSingleShadowPass =
                    [this](const bool enabled,
                           const int passKind,
                           const RenderPassHandle renderPass,
                           const std::array<float, 16>& viewProjection)
                {
                    const ShadowPipelineResources* const defaultShadowResources = GetDefaultShadowPipelineResources();
                    if (!enabled ||
                        renderPass == InvalidResourceHandle ||
                        defaultShadowResources == nullptr)
                    {
                        return;
                    }

                    m_RenderBackend->BeginRenderPass(renderPass);
                    if (m_Mesh != InvalidResourceHandle)
                    {
                        PipelineStateHandle pipeline = InvalidResourceHandle;
                        DescriptorSetHandle descriptorSet = InvalidResourceHandle;
                        switch (passKind)
                        {
                        case 0:
                            pipeline = defaultShadowResources->directionalPipelineState;
                            descriptorSet = defaultShadowResources->directionalDescriptorSet;
                            break;
                        case 1:
                            pipeline = defaultShadowResources->pointPipelineState;
                            descriptorSet = defaultShadowResources->pointDescriptorSet;
                            break;
                        case 2:
                        default:
                            pipeline = defaultShadowResources->spotPipelineState;
                            descriptorSet = defaultShadowResources->spotDescriptorSet;
                            break;
                        }
                        if (pipeline != InvalidResourceHandle &&
                            descriptorSet != InvalidResourceHandle &&
                            ApplyShadowMaterialBindings(MaterialRenderProxy {}, viewProjection, kIdentityMatrix, descriptorSet))
                        {
                            m_RenderBackend->BindPipeline(pipeline);
                            m_RenderBackend->BindDescriptorSet(descriptorSet);
                            m_RenderBackend->DrawMesh(m_Mesh);
                        }
                    }
                    for (const std::string& key : m_RenderItemOrder)
                    {
                        const auto itemIt = m_RenderItems.find(key);
                        if (itemIt == m_RenderItems.end() || itemIt->second.mesh == InvalidResourceHandle)
                        {
                            continue;
                        }

                        const ShadowPipelineResources* shadowResources = GetShadowPipelineResources(itemIt->second.shadowPipelineKey);
                        if (shadowResources == nullptr)
                        {
                            shadowResources = defaultShadowResources;
                        }

                        PipelineStateHandle pipeline = InvalidResourceHandle;
                        DescriptorSetHandle descriptorSet = InvalidResourceHandle;
                        switch (passKind)
                        {
                        case 0:
                            pipeline = shadowResources->directionalPipelineState;
                            descriptorSet = shadowResources->directionalDescriptorSet;
                            break;
                        case 1:
                            pipeline = shadowResources->pointPipelineState;
                            descriptorSet = shadowResources->pointDescriptorSet;
                            break;
                        case 2:
                        default:
                            pipeline = shadowResources->spotPipelineState;
                            descriptorSet = shadowResources->spotDescriptorSet;
                            break;
                        }
                        if (pipeline == InvalidResourceHandle ||
                            descriptorSet == InvalidResourceHandle ||
                            !ApplyShadowMaterialBindings(
                                itemIt->second.material,
                                viewProjection,
                                itemIt->second.worldTransform,
                                descriptorSet))
                        {
                            continue;
                        }

                        m_RenderBackend->BindPipeline(pipeline);
                        m_RenderBackend->BindDescriptorSet(descriptorSet);
                        m_RenderBackend->DrawMesh(itemIt->second.mesh);
                    }
                    m_RenderBackend->EndRenderPass();
                };

                renderSingleShadowPass(
                    m_HasDirectionalShadow,
                    0,
                    m_DirectionalShadowRenderPass,
                    m_DirectionalShadowViewProjection);
                if (m_HasPointShadow)
                {
                    for (std::size_t faceIndex = 0; faceIndex < LightingSystem::kPointShadowFaceCount; ++faceIndex)
                    {
                        const std::uint32_t viewportX =
                            static_cast<std::uint32_t>(faceIndex % kPointShadowAtlasColumns) * m_PointShadowMapSize;
                        const std::uint32_t viewportY =
                            static_cast<std::uint32_t>(faceIndex / kPointShadowAtlasColumns) * m_PointShadowMapSize;
                        m_RenderBackend->SetSceneViewportRegion(
                            viewportX,
                            viewportY,
                            m_PointShadowMapSize,
                            m_PointShadowMapSize);
                        renderSingleShadowPass(
                            true,
                            1,
                            m_PointShadowRenderPass,
                            m_PointShadowViewProjections[faceIndex]);
                    }
                    m_RenderBackend->ClearSceneViewportRegion();
                }
                renderSingleShadowPass(
                    m_HasSpotShadow,
                    2,
                    m_SpotShadowRenderPass,
                    m_SpotShadowViewProjection);
            }

            void AppendShadowTextureBindings(DescriptorSetDesc& descriptorSetDesc) const
            {
                TextureHandle directionalShadowTexture = m_DefaultShadowTexture;
                TextureHandle pointShadowTexture = m_DefaultShadowTexture;
                TextureHandle spotShadowTexture = m_DefaultShadowTexture;
                if (m_RenderBackend != nullptr)
                {
                    if (m_HasDirectionalShadow)
                    {
                        if (const TextureHandle resolved =
                                m_RenderBackend->GetRenderTargetTextureHandle(m_DirectionalShadowRenderTarget);
                            resolved != InvalidTextureHandle)
                        {
                            directionalShadowTexture = resolved;
                        }
                    }
                    if (m_HasPointShadow)
                    {
                        if (const TextureHandle resolved =
                                m_RenderBackend->GetRenderTargetTextureHandle(m_PointShadowRenderTarget);
                            resolved != InvalidTextureHandle)
                        {
                            pointShadowTexture = resolved;
                        }
                    }
                    if (m_HasSpotShadow)
                    {
                        if (const TextureHandle resolved =
                                m_RenderBackend->GetRenderTargetTextureHandle(m_SpotShadowRenderTarget);
                            resolved != InvalidTextureHandle)
                        {
                            spotShadowTexture = resolved;
                        }
                    }
                }

                descriptorSetDesc.images.push_back(DescriptorImageWrite { 7, directionalShadowTexture });
                descriptorSetDesc.images.push_back(DescriptorImageWrite { 8, spotShadowTexture });
                descriptorSetDesc.images.push_back(DescriptorImageWrite { 15, pointShadowTexture });
            }

            bool ApplyMaterialBindings(
                const MaterialRenderProxy& material,
                const TextureHandle bakedLightmapTexture,
                const std::array<float, 16>& worldTransform,
                const DescriptorSetHandle descriptorSet)
            {
                if (descriptorSet == InvalidResourceHandle || m_ResourceManager == nullptr)
                {
                    return false;
                }

                const float uvRotationRadians = material.uvRotation * (3.14159265359f / 180.0f);
                PerDrawData perDrawData {};
                std::memcpy(perDrawData.viewProjection, m_ViewProjection.data(), sizeof(perDrawData.viewProjection));
                std::memcpy(perDrawData.worldTransform, worldTransform.data(), sizeof(perDrawData.worldTransform));
                std::memcpy(perDrawData.tint, material.baseColor.data(), sizeof(perDrawData.tint));
                perDrawData.emissiveColorIntensity[0] = material.emissiveColor[0];
                perDrawData.emissiveColorIntensity[1] = material.emissiveColor[1];
                perDrawData.emissiveColorIntensity[2] = material.emissiveColor[2];
                perDrawData.emissiveColorIntensity[3] = material.emissiveIntensity;
                perDrawData.surfaceParameters[0] = material.metallic;
                perDrawData.surfaceParameters[1] = material.roughness;
                perDrawData.surfaceParameters[2] = material.specular;
                perDrawData.surfaceParameters[3] = material.ambientOcclusion;
                perDrawData.opacityAndNormal[0] = material.opacity;
                perDrawData.opacityAndNormal[1] = material.opacityMaskClipValue;
                perDrawData.opacityAndNormal[2] = material.normalStrength;
                perDrawData.opacityAndNormal[3] = static_cast<float>(material.featureFlags);
                perDrawData.uvTransform0[0] = material.uvTiling[0];
                perDrawData.uvTransform0[1] = material.uvTiling[1];
                perDrawData.uvTransform0[2] = material.uvOffset[0];
                perDrawData.uvTransform0[3] = material.uvOffset[1];
                perDrawData.uvTransform1[0] = uvRotationRadians;
                perDrawData.uvTransform1[1] = static_cast<float>(material.blendMode);
                perDrawData.uvTransform1[2] = static_cast<float>(material.shadingModel);
                perDrawData.uvTransform1[3] = static_cast<float>(material.domain);
                perDrawData.materialParameters2[0] = material.refraction;
                perDrawData.materialParameters2[1] = material.displacementScale;
                perDrawData.materialParameters2[2] = material.opacity;
                std::uint32_t materialTextureMask = 0u;
                if (!material.ormTexture.empty())
                {
                    materialTextureMask |= 1u << 0u;
                }
                if (!material.metallicTexture.empty())
                {
                    materialTextureMask |= 1u << 1u;
                }
                if (!material.roughnessTexture.empty())
                {
                    materialTextureMask |= 1u << 2u;
                }
                if (!material.ambientOcclusionTexture.empty())
                {
                    materialTextureMask |= 1u << 3u;
                }
                perDrawData.materialParameters2[3] = static_cast<float>(materialTextureMask);
                perDrawData.subsurfaceAndCoat[0] = material.subsurfaceColor[0];
                perDrawData.subsurfaceAndCoat[1] = material.subsurfaceColor[1];
                perDrawData.subsurfaceAndCoat[2] = material.subsurfaceColor[2];
                perDrawData.subsurfaceAndCoat[3] = material.clearCoat;
                perDrawData.materialParameters3[0] = material.clearCoatRoughness;
                perDrawData.lightmapParams[0] = bakedLightmapTexture != InvalidTextureHandle ? 1.0f : 0.0f;

                const TextureHandle albedoTexture =
                    ResolveMaterialTexture(material.albedoTexture, true, m_DefaultAlbedoTexture);
                const TextureHandle normalTexture =
                    ResolveMaterialTexture(material.normalTexture, false, m_DefaultNormalTexture);
                const TextureHandle ormTexture =
                    ResolveMaterialTexture(material.ormTexture, false, m_DefaultOrmTexture);
                const TextureHandle metallicTexture =
                    ResolveMaterialTexture(material.metallicTexture, false, m_DefaultMetallicTexture);
                const TextureHandle roughnessTexture =
                    ResolveMaterialTexture(material.roughnessTexture, false, m_DefaultRoughnessTexture);
                const TextureHandle ambientOcclusionTexture =
                    ResolveMaterialTexture(material.ambientOcclusionTexture, false, m_DefaultAmbientOcclusionTexture);
                const TextureHandle irradianceTexture =
                    m_CurrentIrradianceTexture == InvalidTextureHandle ? m_DefaultIrradianceTexture : m_CurrentIrradianceTexture;
                const TextureHandle prefilterTexture =
                    m_CurrentPrefilterTexture == InvalidTextureHandle ? m_DefaultPrefilterTexture : m_CurrentPrefilterTexture;
                const TextureHandle emissiveTexture =
                    ResolveMaterialTexture(material.emissiveTexture, true, m_DefaultEmissiveTexture);
                const TextureHandle opacityTexture =
                    ResolveMaterialTexture(material.opacityTexture, false, m_DefaultOpacityTexture);
                const TextureHandle heightTexture =
                    ResolveMaterialTexture(material.heightTexture, false, m_DefaultHeightTexture);
                const TextureHandle lightmapTexture =
                    bakedLightmapTexture == InvalidTextureHandle ? m_DefaultEmissiveTexture : bakedLightmapTexture;

                DescriptorSetDesc updateDesc;
                updateDesc.buffers.push_back(
                    DescriptorBufferWrite {
                        0,
                        std::vector<std::uint8_t>(
                            reinterpret_cast<const std::uint8_t*>(&perDrawData),
                            reinterpret_cast<const std::uint8_t*>(&perDrawData) + sizeof(PerDrawData))
                    });
                if (!m_LightingSystem.BuildDescriptorSetDesc(updateDesc))
                {
                    return false;
                }
                std::size_t textureBindingsHash = 0u;
                HashCombine(textureBindingsHash, static_cast<std::size_t>(albedoTexture));
                HashCombine(textureBindingsHash, static_cast<std::size_t>(normalTexture));
                HashCombine(textureBindingsHash, static_cast<std::size_t>(ormTexture));
                HashCombine(textureBindingsHash, static_cast<std::size_t>(metallicTexture));
                HashCombine(textureBindingsHash, static_cast<std::size_t>(roughnessTexture));
                HashCombine(textureBindingsHash, static_cast<std::size_t>(ambientOcclusionTexture));
                HashCombine(textureBindingsHash, static_cast<std::size_t>(irradianceTexture));
                HashCombine(textureBindingsHash, static_cast<std::size_t>(prefilterTexture));
                HashCombine(textureBindingsHash, static_cast<std::size_t>(emissiveTexture));
                HashCombine(textureBindingsHash, static_cast<std::size_t>(opacityTexture));
                HashCombine(textureBindingsHash, static_cast<std::size_t>(heightTexture));
                HashCombine(textureBindingsHash, static_cast<std::size_t>(lightmapTexture));
                HashCombine(
                    textureBindingsHash,
                    static_cast<std::size_t>(
                        m_DirectionalShadowRenderTarget == InvalidTextureHandle ? m_DefaultShadowTexture : m_DirectionalShadowRenderTarget));
                HashCombine(
                    textureBindingsHash,
                    static_cast<std::size_t>(
                        m_PointShadowRenderTarget == InvalidTextureHandle ? m_DefaultShadowTexture : m_PointShadowRenderTarget));
                HashCombine(
                    textureBindingsHash,
                    static_cast<std::size_t>(
                        m_SpotShadowRenderTarget == InvalidTextureHandle ? m_DefaultShadowTexture : m_SpotShadowRenderTarget));

                const auto lastTextureBindingIt = m_LastMaterialTextureBindingHashes.find(descriptorSet);
                const bool textureBindingsChanged =
                    lastTextureBindingIt == m_LastMaterialTextureBindingHashes.end() ||
                    lastTextureBindingIt->second != textureBindingsHash;
                if (textureBindingsChanged)
                {
                    updateDesc.images.push_back(DescriptorImageWrite { 1, albedoTexture });
                    updateDesc.images.push_back(DescriptorImageWrite { 2, normalTexture });
                    updateDesc.images.push_back(DescriptorImageWrite { 3, ormTexture });
                    updateDesc.images.push_back(DescriptorImageWrite { 12, metallicTexture });
                    updateDesc.images.push_back(DescriptorImageWrite { 13, roughnessTexture });
                    updateDesc.images.push_back(DescriptorImageWrite { 14, ambientOcclusionTexture });
                    updateDesc.images.push_back(DescriptorImageWrite { 5, irradianceTexture });
                    updateDesc.images.push_back(DescriptorImageWrite { 6, prefilterTexture });
                    updateDesc.images.push_back(DescriptorImageWrite { 9, emissiveTexture });
                    updateDesc.images.push_back(DescriptorImageWrite { 10, opacityTexture });
                    updateDesc.images.push_back(DescriptorImageWrite { 11, heightTexture });
                    updateDesc.images.push_back(DescriptorImageWrite { 16, lightmapTexture });

                    AppendShadowTextureBindings(updateDesc);
                    m_LastMaterialTextureBindingHashes[descriptorSet] = textureBindingsHash;
                }

                m_ResourceManager->UpdateDescriptorSet(descriptorSet, updateDesc);
                return true;
            }

            bool ApplyShadowMaterialBindings(
                const MaterialRenderProxy& material,
                const std::array<float, 16>& viewProjection,
                const std::array<float, 16>& worldTransform,
                const DescriptorSetHandle descriptorSet)
            {
                if (descriptorSet == InvalidResourceHandle || m_ResourceManager == nullptr)
                {
                    return false;
                }

                const float uvRotationRadians = material.uvRotation * (3.14159265359f / 180.0f);
                ShadowPerDrawData perDrawData {};
                std::memcpy(perDrawData.viewProjection, viewProjection.data(), sizeof(perDrawData.viewProjection));
                std::memcpy(perDrawData.worldTransform, worldTransform.data(), sizeof(perDrawData.worldTransform));
                perDrawData.uvTransform0[0] = material.uvTiling[0];
                perDrawData.uvTransform0[1] = material.uvTiling[1];
                perDrawData.uvTransform0[2] = material.uvOffset[0];
                perDrawData.uvTransform0[3] = material.uvOffset[1];
                perDrawData.shadowMaterialParams[0] = uvRotationRadians;
                perDrawData.shadowMaterialParams[1] = material.opacity;
                perDrawData.shadowMaterialParams[2] = material.opacityMaskClipValue;
                perDrawData.shadowMaterialParams[3] = material.displacementScale;

                const TextureHandle opacityTexture =
                    ResolveMaterialTexture(material.opacityTexture, false, m_DefaultOpacityTexture);
                const TextureHandle heightTexture =
                    ResolveMaterialTexture(material.heightTexture, false, m_DefaultHeightTexture);
                DescriptorSetDesc updateDesc;
                updateDesc.buffers.push_back(
                    DescriptorBufferWrite {
                        0,
                        std::vector<std::uint8_t>(
                            reinterpret_cast<const std::uint8_t*>(&perDrawData),
                            reinterpret_cast<const std::uint8_t*>(&perDrawData) + sizeof(ShadowPerDrawData))
                    });
                std::size_t textureBindingsHash = 0u;
                HashCombine(textureBindingsHash, static_cast<std::size_t>(opacityTexture));
                HashCombine(textureBindingsHash, static_cast<std::size_t>(heightTexture));
                const auto lastTextureBindingIt = m_LastShadowTextureBindingHashes.find(descriptorSet);
                const bool textureBindingsChanged =
                    lastTextureBindingIt == m_LastShadowTextureBindingHashes.end() ||
                    lastTextureBindingIt->second != textureBindingsHash;
                if (textureBindingsChanged)
                {
                    updateDesc.images.push_back(DescriptorImageWrite { 1, opacityTexture });
                    updateDesc.images.push_back(DescriptorImageWrite { 2, heightTexture });
                    m_LastShadowTextureBindingHashes[descriptorSet] = textureBindingsHash;
                }
                m_ResourceManager->UpdateDescriptorSet(descriptorSet, updateDesc);
                return true;
            }

            bool ApplySkyBindings()
            {
                if (m_SkyDescriptorSet == InvalidResourceHandle || m_ResourceManager == nullptr)
                {
                    return false;
                }

                PerDrawData perDrawData {};
                std::memcpy(perDrawData.viewProjection, m_ViewProjection.data(), sizeof(perDrawData.viewProjection));
                std::array<float, 16> skyWorldTransform = kIdentityMatrix;
                skyWorldTransform[12] = m_CameraWorldPosition[0];
                skyWorldTransform[13] = m_CameraWorldPosition[1];
                skyWorldTransform[14] = m_CameraWorldPosition[2];
                std::memcpy(perDrawData.worldTransform, skyWorldTransform.data(), sizeof(perDrawData.worldTransform));
                const ImageBasedLightDesc& imageBasedLight = m_LightingSystem.GetImageBasedLight();
                const float skyExposure =
                    std::max(1.0e-4f, imageBasedLight.skyboxExposureMultiplier);
                perDrawData.tint[0] = skyExposure;
                perDrawData.tint[1] = skyExposure;
                perDrawData.tint[2] = skyExposure;
                perDrawData.tint[3] = 1.0f;

                DescriptorSetDesc updateDesc;
                DescriptorBufferWrite write;
                write.binding = 0;
                write.data.resize(sizeof(PerDrawData));
                std::memcpy(write.data.data(), &perDrawData, sizeof(PerDrawData));
                updateDesc.buffers.push_back(std::move(write));
                m_ResourceManager->UpdateDescriptorSet(m_SkyDescriptorSet, updateDesc);
                return true;
            }

            bool ApplyGridBindings()
            {
                if (m_GridDescriptorSet == InvalidResourceHandle || m_ResourceManager == nullptr)
                {
                    return false;
                }

                PerDrawData perDrawData {};
                std::memcpy(perDrawData.viewProjection, m_ViewProjection.data(), sizeof(perDrawData.viewProjection));
                std::memcpy(perDrawData.worldTransform, kIdentityMatrix.data(), sizeof(perDrawData.worldTransform));
                perDrawData.tint[0] = 1.0f;
                perDrawData.tint[1] = 1.0f;
                perDrawData.tint[2] = 1.0f;
                perDrawData.tint[3] = 1.0f;

                DescriptorSetDesc updateDesc;
                DescriptorBufferWrite write;
                write.binding = 0;
                write.data.resize(sizeof(PerDrawData));
                std::memcpy(write.data.data(), &perDrawData, sizeof(PerDrawData));
                updateDesc.buffers.push_back(std::move(write));
                m_ResourceManager->UpdateDescriptorSet(m_GridDescriptorSet, updateDesc);
                return true;
            }

            bool BuildMaterialDescriptorSetDesc(DescriptorSetDesc& outDesc) const
            {
                outDesc.buffers.push_back(
                    DescriptorBufferWrite { 0, std::vector<std::uint8_t>(sizeof(PerDrawData), 0) });
                outDesc.images.push_back(DescriptorImageWrite { 1, m_DefaultAlbedoTexture });
                outDesc.images.push_back(DescriptorImageWrite { 2, m_DefaultNormalTexture });
                outDesc.images.push_back(DescriptorImageWrite { 3, m_DefaultOrmTexture });
                outDesc.images.push_back(DescriptorImageWrite { 12, m_DefaultMetallicTexture });
                outDesc.images.push_back(DescriptorImageWrite { 13, m_DefaultRoughnessTexture });
                outDesc.images.push_back(DescriptorImageWrite { 14, m_DefaultAmbientOcclusionTexture });
                outDesc.images.push_back(DescriptorImageWrite { 5, m_DefaultIrradianceTexture });
                outDesc.images.push_back(DescriptorImageWrite { 6, m_DefaultPrefilterTexture });
                outDesc.images.push_back(DescriptorImageWrite { 9, m_DefaultEmissiveTexture });
                outDesc.images.push_back(DescriptorImageWrite { 10, m_DefaultOpacityTexture });
                outDesc.images.push_back(DescriptorImageWrite { 11, m_DefaultHeightTexture });
                outDesc.images.push_back(DescriptorImageWrite { 16, m_DefaultEmissiveTexture });
                if (!m_LightingSystem.BuildDescriptorSetDesc(outDesc))
                {
                    return false;
                }

                AppendShadowTextureBindings(outDesc);
                return true;
            }

            bool BuildShadowDescriptorSetDesc(DescriptorSetDesc& outDesc) const
            {
                outDesc.buffers.push_back(
                    DescriptorBufferWrite { 0, std::vector<std::uint8_t>(sizeof(ShadowPerDrawData), 0) });
                outDesc.images.push_back(DescriptorImageWrite { 1, m_DefaultOpacityTexture });
                outDesc.images.push_back(DescriptorImageWrite { 2, m_DefaultHeightTexture });
                return true;
            }

            void DestroyMaterialPipelineResources(MaterialPipelineResources& resources)
            {
                if (m_ResourceManager == nullptr)
                {
                    resources = {};
                    return;
                }

                if (resources.modulateDescriptorSet != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyDescriptorSet(resources.modulateDescriptorSet, GPUResourceManager::DestroyMode::Deferred);
                    resources.modulateDescriptorSet = InvalidResourceHandle;
                }
                if (resources.translucentDescriptorSet != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyDescriptorSet(resources.translucentDescriptorSet, GPUResourceManager::DestroyMode::Deferred);
                    resources.translucentDescriptorSet = InvalidResourceHandle;
                }
                if (resources.additiveDescriptorSet != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyDescriptorSet(resources.additiveDescriptorSet, GPUResourceManager::DestroyMode::Deferred);
                    resources.additiveDescriptorSet = InvalidResourceHandle;
                }
                if (resources.opaqueDescriptorSet != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyDescriptorSet(resources.opaqueDescriptorSet, GPUResourceManager::DestroyMode::Deferred);
                    resources.opaqueDescriptorSet = InvalidResourceHandle;
                }
                if (resources.modulatePipelineState != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyPipelineState(resources.modulatePipelineState, GPUResourceManager::DestroyMode::Deferred);
                    resources.modulatePipelineState = InvalidResourceHandle;
                }
                if (resources.translucentPipelineState != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyPipelineState(resources.translucentPipelineState, GPUResourceManager::DestroyMode::Deferred);
                    resources.translucentPipelineState = InvalidResourceHandle;
                }
                if (resources.additivePipelineState != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyPipelineState(resources.additivePipelineState, GPUResourceManager::DestroyMode::Deferred);
                    resources.additivePipelineState = InvalidResourceHandle;
                }
                if (resources.opaquePipelineState != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyPipelineState(resources.opaquePipelineState, GPUResourceManager::DestroyMode::Deferred);
                    resources.opaquePipelineState = InvalidResourceHandle;
                }
            }

            void DestroyShadowPipelineResources(ShadowPipelineResources& resources)
            {
                if (m_ResourceManager == nullptr)
                {
                    resources = {};
                    return;
                }

                if (resources.spotDescriptorSet != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyDescriptorSet(resources.spotDescriptorSet, GPUResourceManager::DestroyMode::Deferred);
                    resources.spotDescriptorSet = InvalidResourceHandle;
                }
                if (resources.pointDescriptorSet != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyDescriptorSet(resources.pointDescriptorSet, GPUResourceManager::DestroyMode::Deferred);
                    resources.pointDescriptorSet = InvalidResourceHandle;
                }
                if (resources.directionalDescriptorSet != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyDescriptorSet(resources.directionalDescriptorSet, GPUResourceManager::DestroyMode::Deferred);
                    resources.directionalDescriptorSet = InvalidResourceHandle;
                }
                if (resources.spotPipelineState != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyPipelineState(resources.spotPipelineState, GPUResourceManager::DestroyMode::Deferred);
                    resources.spotPipelineState = InvalidResourceHandle;
                }
                if (resources.pointPipelineState != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyPipelineState(resources.pointPipelineState, GPUResourceManager::DestroyMode::Deferred);
                    resources.pointPipelineState = InvalidResourceHandle;
                }
                if (resources.directionalPipelineState != InvalidResourceHandle)
                {
                    m_ResourceManager->DestroyPipelineState(resources.directionalPipelineState, GPUResourceManager::DestroyMode::Deferred);
                    resources.directionalPipelineState = InvalidResourceHandle;
                }
            }

            bool CreateMaterialPipelineResources(
                const std::string& key,
                const ShaderProgramDesc& shaderProgram,
                const std::uint32_t featureFlags,
                MaterialPipelineResources& outResources)
            {
                if (m_ResourceManager == nullptr || m_SceneRenderPass == InvalidResourceHandle)
                {
                    return false;
                }

                outResources.key = key;
                outResources.shaderProgram = shaderProgram;
                const bool materialTwoSided = (featureFlags & MaterialFeature_TwoSided) != 0u;
                outResources.opaquePipelineState = m_ResourceManager->CreatePipelineState(
                    BuildPipelineDesc(GetDebugName(), m_SceneRenderPass, shaderProgram, SceneBlendMode::Opaque, materialTwoSided));
                outResources.translucentPipelineState = m_ResourceManager->CreatePipelineState(
                    BuildPipelineDesc(GetDebugName(), m_SceneRenderPass, shaderProgram, SceneBlendMode::Translucent, materialTwoSided));
                outResources.additivePipelineState = m_ResourceManager->CreatePipelineState(
                    BuildPipelineDesc(GetDebugName(), m_SceneRenderPass, shaderProgram, SceneBlendMode::Additive, materialTwoSided));
                outResources.modulatePipelineState = m_ResourceManager->CreatePipelineState(
                    BuildPipelineDesc(GetDebugName(), m_SceneRenderPass, shaderProgram, SceneBlendMode::Modulate, materialTwoSided));
                if (outResources.opaquePipelineState == InvalidResourceHandle ||
                    outResources.translucentPipelineState == InvalidResourceHandle ||
                    outResources.additivePipelineState == InvalidResourceHandle ||
                    outResources.modulatePipelineState == InvalidResourceHandle)
                {
                    DestroyMaterialPipelineResources(outResources);
                    return false;
                }

                DescriptorSetDesc descriptorSetDesc;
                if (!BuildMaterialDescriptorSetDesc(descriptorSetDesc))
                {
                    DestroyMaterialPipelineResources(outResources);
                    return false;
                }

                outResources.opaqueDescriptorSet = m_ResourceManager->CreateDescriptorSet(
                    outResources.opaquePipelineState,
                    descriptorSetDesc,
                    std::string(GetDebugName()) + ".Opaque." + key);
                outResources.translucentDescriptorSet = m_ResourceManager->CreateDescriptorSet(
                    outResources.translucentPipelineState,
                    descriptorSetDesc,
                    std::string(GetDebugName()) + ".Translucent." + key);
                outResources.additiveDescriptorSet = m_ResourceManager->CreateDescriptorSet(
                    outResources.additivePipelineState,
                    descriptorSetDesc,
                    std::string(GetDebugName()) + ".Additive." + key);
                outResources.modulateDescriptorSet = m_ResourceManager->CreateDescriptorSet(
                    outResources.modulatePipelineState,
                    descriptorSetDesc,
                    std::string(GetDebugName()) + ".Modulate." + key);
                if (outResources.opaqueDescriptorSet == InvalidResourceHandle ||
                    outResources.translucentDescriptorSet == InvalidResourceHandle ||
                    outResources.additiveDescriptorSet == InvalidResourceHandle ||
                    outResources.modulateDescriptorSet == InvalidResourceHandle)
                {
                    DestroyMaterialPipelineResources(outResources);
                    return false;
                }

                return true;
            }

            bool CreateShadowPipelineResources(
                const std::string& key,
                const ShaderProgramDesc& shaderProgram,
                const std::uint32_t featureFlags,
                ShadowPipelineResources& outResources)
            {
                if (m_ResourceManager == nullptr ||
                    m_DirectionalShadowRenderPass == InvalidResourceHandle ||
                    m_PointShadowRenderPass == InvalidResourceHandle ||
                    m_SpotShadowRenderPass == InvalidResourceHandle)
                {
                    return false;
                }

                outResources.key = key;
                outResources.shaderProgram = shaderProgram;
                const bool materialTwoSided = (featureFlags & MaterialFeature_TwoSided) != 0u;
                outResources.directionalPipelineState = m_ResourceManager->CreatePipelineState(
                    BuildShadowPipelineDesc(GetDebugName(), m_DirectionalShadowRenderPass, shaderProgram, materialTwoSided));
                outResources.pointPipelineState = m_ResourceManager->CreatePipelineState(
                    BuildShadowPipelineDesc(GetDebugName(), m_PointShadowRenderPass, shaderProgram, materialTwoSided));
                outResources.spotPipelineState = m_ResourceManager->CreatePipelineState(
                    BuildShadowPipelineDesc(GetDebugName(), m_SpotShadowRenderPass, shaderProgram, materialTwoSided));
                if (outResources.directionalPipelineState == InvalidResourceHandle ||
                    outResources.pointPipelineState == InvalidResourceHandle ||
                    outResources.spotPipelineState == InvalidResourceHandle)
                {
                    DestroyShadowPipelineResources(outResources);
                    return false;
                }

                DescriptorSetDesc descriptorSetDesc;
                if (!BuildShadowDescriptorSetDesc(descriptorSetDesc))
                {
                    DestroyShadowPipelineResources(outResources);
                    return false;
                }

                outResources.directionalDescriptorSet = m_ResourceManager->CreateDescriptorSet(
                    outResources.directionalPipelineState,
                    descriptorSetDesc,
                    std::string(GetDebugName()) + ".ShadowDirectional." + key);
                outResources.pointDescriptorSet = m_ResourceManager->CreateDescriptorSet(
                    outResources.pointPipelineState,
                    descriptorSetDesc,
                    std::string(GetDebugName()) + ".ShadowPoint." + key);
                outResources.spotDescriptorSet = m_ResourceManager->CreateDescriptorSet(
                    outResources.spotPipelineState,
                    descriptorSetDesc,
                    std::string(GetDebugName()) + ".ShadowSpot." + key);
                if (outResources.directionalDescriptorSet == InvalidResourceHandle ||
                    outResources.pointDescriptorSet == InvalidResourceHandle ||
                    outResources.spotDescriptorSet == InvalidResourceHandle)
                {
                    DestroyShadowPipelineResources(outResources);
                    return false;
                }

                return true;
            }

            const MaterialPipelineResources* GetMaterialPipelineResources(const std::string_view key) const
            {
                const auto it = m_MaterialPipelines.find(std::string(key));
                return it != m_MaterialPipelines.end() ? &it->second : nullptr;
            }

            const ShadowPipelineResources* GetShadowPipelineResources(const std::string_view key) const
            {
                const auto it = m_ShadowMaterialPipelines.find(std::string(key));
                return it != m_ShadowMaterialPipelines.end() ? &it->second : nullptr;
            }

            const MaterialPipelineResources* GetDefaultMaterialPipelineResources() const
            {
                if (m_DefaultMaterialPipelineKey.empty())
                {
                    return nullptr;
                }

                return GetMaterialPipelineResources(m_DefaultMaterialPipelineKey);
            }

            const ShadowPipelineResources* GetDefaultShadowPipelineResources() const
            {
                if (m_DefaultShadowPipelineKey.empty())
                {
                    return nullptr;
                }

                return GetShadowPipelineResources(m_DefaultShadowPipelineKey);
            }

            bool EnsureMaterialPipelineResourcesFor(
                const MaterialRenderProxy& material,
                std::string* outKey = nullptr,
                std::string* outError = nullptr)
            {
                const std::string lookupKey =
                    std::string("Builtin.Triangle|pipeline=") + GetPipelineMacro() +
                    "|features=" + std::to_string(material.featureFlags);
                const auto existing = m_MaterialPipelines.find(lookupKey);
                if (existing != m_MaterialPipelines.end())
                {
                    if (outKey != nullptr)
                    {
                        *outKey = existing->first;
                    }
                    return true;
                }

                MaterialPipelineResources resources;
                if (!CreateMaterialPipelineResources(
                        lookupKey,
                        m_DefaultMaterialShaderProgramDesc,
                        material.featureFlags,
                        resources))
                {
                    if (outError != nullptr)
                    {
                        *outError = "Failed to create default material pipeline resources.";
                    }
                    return false;
                }

                auto [it, inserted] = m_MaterialPipelines.emplace(lookupKey, std::move(resources));
                if (!inserted)
                {
                    DestroyMaterialPipelineResources(resources);
                }

                if (outKey != nullptr)
                {
                    *outKey = it->first;
                }
                return true;
            }

            bool EnsureDefaultMaterialPipelineResources()
            {
                MaterialRenderProxy defaultMaterial;
                std::string key;
                if (!EnsureMaterialPipelineResourcesFor(defaultMaterial, &key, nullptr))
                {
                    return false;
                }

                m_DefaultMaterialPipelineKey = std::move(key);
                return true;
            }

            bool EnsureShadowPipelineResourcesFor(
                const MaterialRenderProxy& material,
                std::string* outKey = nullptr,
                std::string* outError = nullptr)
            {
                const std::string lookupKey =
                    std::string("Builtin.Shadow|pipeline=") + GetPipelineMacro() +
                    "|features=" + std::to_string(material.featureFlags);
                const auto existing = m_ShadowMaterialPipelines.find(lookupKey);
                if (existing != m_ShadowMaterialPipelines.end())
                {
                    if (outKey != nullptr)
                    {
                        *outKey = existing->first;
                    }
                    return true;
                }

                ShadowPipelineResources resources;
                if (!CreateShadowPipelineResources(
                        lookupKey,
                        m_ShadowShaderProgramDesc,
                        material.featureFlags,
                        resources))
                {
                    if (outError != nullptr)
                    {
                        *outError = "Failed to create default shadow pipeline resources.";
                    }
                    return false;
                }

                auto [it, inserted] = m_ShadowMaterialPipelines.emplace(lookupKey, std::move(resources));
                if (!inserted)
                {
                    DestroyShadowPipelineResources(resources);
                }

                if (outKey != nullptr)
                {
                    *outKey = it->first;
                }
                return true;
            }

            bool EnsureDefaultShadowPipelineResources()
            {
                MaterialRenderProxy defaultMaterial;
                std::string key;
                if (!EnsureShadowPipelineResourcesFor(defaultMaterial, &key, nullptr))
                {
                    return false;
                }

                m_DefaultShadowPipelineKey = std::move(key);
                return true;
            }

            bool EnsureMaterialPipelinesForRenderItems(std::string* outFirstError = nullptr)
            {
                if (!EnsureDefaultMaterialPipelineResources())
                {
                    if (outFirstError != nullptr && outFirstError->empty())
                    {
                        *outFirstError = "Failed to resolve default material pipeline.";
                    }
                    return false;
                }

                std::string firstError;
                for (auto& [_, item] : m_RenderItems)
                {
                    std::string key;
                    std::string error;
                    if (!EnsureMaterialPipelineResourcesFor(item.material, &key, &error))
                    {
                        if (firstError.empty())
                        {
                            firstError = std::move(error);
                        }
                        item.materialPipelineKey = m_DefaultMaterialPipelineKey;
                        continue;
                    }

                    item.materialPipelineKey = std::move(key);
                }

                if (outFirstError != nullptr)
                {
                    *outFirstError = std::move(firstError);
                }

                return true;
            }

            bool EnsureShadowPipelinesForRenderItems(std::string* outFirstError = nullptr)
            {
                if (!EnsureDefaultShadowPipelineResources())
                {
                    if (outFirstError != nullptr && outFirstError->empty())
                    {
                        *outFirstError = "Failed to resolve default shadow pipeline.";
                    }
                    return false;
                }

                std::string firstError;
                for (auto& [_, item] : m_RenderItems)
                {
                    std::string key;
                    std::string error;
                    if (!EnsureShadowPipelineResourcesFor(item.material, &key, &error))
                    {
                        if (firstError.empty())
                        {
                            firstError = std::move(error);
                        }
                        item.shadowPipelineKey = m_DefaultShadowPipelineKey;
                        continue;
                    }

                    item.shadowPipelineKey = std::move(key);
                }

                if (outFirstError != nullptr)
                {
                    *outFirstError = std::move(firstError);
                }

                return true;
            }

            PipelineStateHandle SelectMaterialPipelineState(
                const MaterialPipelineResources& resources,
                const std::uint32_t blendMode) const
            {
                switch (static_cast<SceneBlendMode>(blendMode))
                {
                case SceneBlendMode::Translucent:
                    return resources.translucentPipelineState;
                case SceneBlendMode::Additive:
                    return resources.additivePipelineState;
                case SceneBlendMode::Modulate:
                    return resources.modulatePipelineState;
                case SceneBlendMode::Opaque:
                case SceneBlendMode::Masked:
                default:
                    return resources.opaquePipelineState;
                }
            }

            DescriptorSetHandle SelectMaterialDescriptorSet(
                const MaterialPipelineResources& resources,
                const std::uint32_t blendMode) const
            {
                switch (static_cast<SceneBlendMode>(blendMode))
                {
                case SceneBlendMode::Translucent:
                    return resources.translucentDescriptorSet;
                case SceneBlendMode::Additive:
                    return resources.additiveDescriptorSet;
                case SceneBlendMode::Modulate:
                    return resources.modulateDescriptorSet;
                case SceneBlendMode::Opaque:
                case SceneBlendMode::Masked:
                default:
                    return resources.opaqueDescriptorSet;
                }
            }

            void SyncRenderItems(const SceneView& sceneView)
            {
                if (m_ResourceManager == nullptr)
                {
                    return;
                }

                if (sceneView.renderItems == nullptr)
                {
                    if (!m_RenderItems.empty() || !m_SharedMeshes.empty())
                    {
                        for (auto& [_, item] : m_RenderItems)
                        {
                            if (item.bakedLightmapTexture != InvalidTextureHandle)
                            {
                                m_ResourceManager->DestroyTexture(
                                    item.bakedLightmapTexture,
                                    GPUResourceManager::DestroyMode::Deferred);
                            }
                        }
                        for (auto& [_, mesh] : m_SharedMeshes)
                        {
                            if (mesh.handle != InvalidResourceHandle)
                            {
                                m_ResourceManager->DestroyMesh(mesh.handle, GPUResourceManager::DestroyMode::Deferred);
                            }
                        }
                        m_RenderItems.clear();
                        m_SharedMeshes.clear();
                        m_RenderItemOrder.clear();
                        m_RenderItemsRevision = 0;
                    }
                    return;
                }

                if (m_RenderItemsRevision == sceneView.renderItemsRevision)
                {
                    return;
                }

                std::unordered_set<std::string> liveKeys;
                std::unordered_set<std::string> liveMeshKeys;
                liveKeys.reserve(sceneView.renderItems->size());
                liveMeshKeys.reserve(sceneView.renderItems->size());
                std::vector<std::string> orderedKeys;
                orderedKeys.reserve(sceneView.renderItems->size());

                for (const SceneRenderItem& sourceItem : *sceneView.renderItems)
                {
                    if (sourceItem.key.empty())
                    {
                        continue;
                    }

                    liveKeys.insert(sourceItem.key);
                    orderedKeys.push_back(sourceItem.key);

                    const std::string resolvedMeshKey =
                        sourceItem.meshKey.empty() ? sourceItem.key : sourceItem.meshKey;
                    liveMeshKeys.insert(resolvedMeshKey);
                    SharedMeshResource& sharedMesh = m_SharedMeshes[resolvedMeshKey];
                    if (sharedMesh.revision != sourceItem.meshRevision)
                    {
                        if (sharedMesh.handle != InvalidResourceHandle)
                        {
                            m_ResourceManager->DestroyMesh(sharedMesh.handle, GPUResourceManager::DestroyMode::Deferred);
                            sharedMesh.handle = InvalidResourceHandle;
                        }

                        if (!sourceItem.mesh.vertexData.empty() && !sourceItem.mesh.indexData.empty())
                        {
                            sharedMesh.handle = m_ResourceManager->CreateMesh(
                                sourceItem.mesh,
                                std::string(GetDebugName()) + ".Shared." + resolvedMeshKey);
                        }

                        sharedMesh.revision = sourceItem.meshRevision;
                    }

                    RuntimeRenderItem& runtimeItem = m_RenderItems[sourceItem.key];
                    runtimeItem.revision = sourceItem.revision;
                    runtimeItem.mesh = sharedMesh.handle;
                    runtimeItem.materialPipelineKey.clear();
                    runtimeItem.shadowPipelineKey.clear();
                    runtimeItem.material = sourceItem.material;
                    runtimeItem.worldPosition = sourceItem.worldPosition;
                    runtimeItem.worldTransform = sourceItem.worldTransform;
                    if (sourceItem.bakedLightmap.width > 0 &&
                        sourceItem.bakedLightmap.height > 0 &&
                        !sourceItem.bakedLightmap.pixels.empty())
                    {
                        TextureDesc lightmapDesc;
                        lightmapDesc.debugName = std::string(GetDebugName()) + ".Lightmap." + sourceItem.key;
                        lightmapDesc.width = sourceItem.bakedLightmap.width;
                        lightmapDesc.height = sourceItem.bakedLightmap.height;
                        lightmapDesc.format = GpuTextureFormat::RGBA8;
                        lightmapDesc.srgb = false;
                        lightmapDesc.pixelData = sourceItem.bakedLightmap.pixels;

                        if (runtimeItem.bakedLightmapTexture == InvalidTextureHandle)
                        {
                            runtimeItem.bakedLightmapTexture =
                                m_ResourceManager->CreateTexture(lightmapDesc, lightmapDesc.debugName);
                        }
                        else if (runtimeItem.bakedLightmapRevision != sourceItem.bakedLightmap.revision)
                        {
                            m_ResourceManager->UpdateTexture(runtimeItem.bakedLightmapTexture, lightmapDesc);
                        }
                        runtimeItem.bakedLightmapRevision = sourceItem.bakedLightmap.revision;
                    }
                    else if (runtimeItem.bakedLightmapTexture != InvalidTextureHandle)
                    {
                        m_ResourceManager->DestroyTexture(
                            runtimeItem.bakedLightmapTexture,
                            GPUResourceManager::DestroyMode::Deferred);
                        runtimeItem.bakedLightmapTexture = InvalidTextureHandle;
                        runtimeItem.bakedLightmapRevision = 0;
                    }
                }

                for (auto it = m_RenderItems.begin(); it != m_RenderItems.end();)
                {
                    if (liveKeys.find(it->first) == liveKeys.end())
                    {
                        if (it->second.bakedLightmapTexture != InvalidTextureHandle)
                        {
                            m_ResourceManager->DestroyTexture(
                                it->second.bakedLightmapTexture,
                                GPUResourceManager::DestroyMode::Deferred);
                        }
                        it = m_RenderItems.erase(it);
                        continue;
                    }
                    ++it;
                }

                for (auto it = m_SharedMeshes.begin(); it != m_SharedMeshes.end();)
                {
                    if (liveMeshKeys.find(it->first) == liveMeshKeys.end())
                    {
                        if (it->second.handle != InvalidResourceHandle)
                        {
                            m_ResourceManager->DestroyMesh(it->second.handle, GPUResourceManager::DestroyMode::Deferred);
                        }
                        it = m_SharedMeshes.erase(it);
                        continue;
                    }
                    ++it;
                }

                m_RenderItemOrder = std::move(orderedKeys);
                m_RenderItemsRevision = sceneView.renderItemsRevision;
            }

            bool BuildRenderGraph()
            {
                m_RenderGraph.Clear();
                m_RenderGraphFrameIndex = 0;
                if (m_RenderPass == InvalidResourceHandle ||
                    GetDefaultMaterialPipelineResources() == nullptr ||
                    m_ResourceManager == nullptr ||
                    m_RenderBackend == nullptr)
                {
                    return false;
                }

                const std::string setupPassName = std::string(GetDebugName()) + ".UpdatePerDraw";
                const RenderGraph::PassHandle setupPass = m_RenderGraph.AddPass(
                    RenderGraphPassDesc { setupPassName, true },
                    [this](const RenderGraphContext& context)
                    {
                        (void)context;
                    });
                if (setupPass == RenderGraph::InvalidPassHandle)
                {
                    return false;
                }

                const std::string scenePassName = std::string(GetDebugName()) + ".ScenePass";
                const RenderGraph::PassHandle scenePass = m_RenderGraph.AddPass(
                    RenderGraphPassDesc { scenePassName, true },
                    [this](const RenderGraphContext& context)
                    {
                        const MaterialPipelineResources* const defaultMaterialPipelines = GetDefaultMaterialPipelineResources();
                        if (m_SceneRenderPass == InvalidResourceHandle ||
                            defaultMaterialPipelines == nullptr)
                        {
                            return;
                        }

                        context.renderer.BeginRenderPass(m_SceneRenderPass);
                        if (m_SkyMesh != InvalidResourceHandle &&
                            m_SkyPipelineState != InvalidResourceHandle &&
                            m_SkyDescriptorSet != InvalidResourceHandle &&
                            ApplySkyBindings())
                        {
                            context.renderer.BindPipeline(m_SkyPipelineState);
                            context.renderer.BindDescriptorSet(m_SkyDescriptorSet);
                            context.renderer.DrawMesh(m_SkyMesh);
                        }

                        if (m_GridMesh != InvalidResourceHandle &&
                            m_GridPipelineState != InvalidResourceHandle &&
                            m_GridDescriptorSet != InvalidResourceHandle &&
                            ApplyGridBindings())
                        {
                            context.renderer.BindPipeline(m_GridPipelineState);
                            context.renderer.BindDescriptorSet(m_GridDescriptorSet);
                            context.renderer.DrawMesh(m_GridMesh);
                        }
                        std::vector<const RuntimeRenderItem*> opaqueItems;
                        std::vector<const RuntimeRenderItem*> blendedItems;
                        opaqueItems.reserve(m_RenderItemOrder.size());
                        blendedItems.reserve(m_RenderItemOrder.size());
                        for (const std::string& key : m_RenderItemOrder)
                        {
                            const auto itemIt = m_RenderItems.find(key);
                            if (itemIt == m_RenderItems.end() || itemIt->second.mesh == InvalidResourceHandle)
                            {
                                continue;
                            }

                            if (IsBlendSortedMaterial(itemIt->second.material.blendMode))
                            {
                                blendedItems.push_back(&itemIt->second);
                            }
                            else
                            {
                                opaqueItems.push_back(&itemIt->second);
                            }
                        }
                        std::sort(
                            blendedItems.begin(),
                            blendedItems.end(),
                            [cameraPosition = m_CameraWorldPosition](const RuntimeRenderItem* lhs, const RuntimeRenderItem* rhs)
                            {
                                const float lhsDx = lhs->worldPosition[0] - cameraPosition[0];
                                const float lhsDy = lhs->worldPosition[1] - cameraPosition[1];
                                const float lhsDz = lhs->worldPosition[2] - cameraPosition[2];
                                const float rhsDx = rhs->worldPosition[0] - cameraPosition[0];
                                const float rhsDy = rhs->worldPosition[1] - cameraPosition[1];
                                const float rhsDz = rhs->worldPosition[2] - cameraPosition[2];
                                const float lhsDistanceSq = lhsDx * lhsDx + lhsDy * lhsDy + lhsDz * lhsDz;
                                const float rhsDistanceSq = rhsDx * rhsDx + rhsDy * rhsDy + rhsDz * rhsDz;
                                return lhsDistanceSq > rhsDistanceSq;
                            });

                        if (m_Mesh != InvalidResourceHandle)
                        {
                            const PipelineStateHandle pipelineHandle =
                                SelectMaterialPipelineState(*defaultMaterialPipelines, static_cast<std::uint32_t>(SceneBlendMode::Opaque));
                            const DescriptorSetHandle descriptorSetHandle =
                                SelectMaterialDescriptorSet(*defaultMaterialPipelines, static_cast<std::uint32_t>(SceneBlendMode::Opaque));
                            ApplyMaterialBindings(MaterialRenderProxy {}, InvalidTextureHandle, kIdentityMatrix, descriptorSetHandle);
                            context.renderer.BindPipeline(pipelineHandle);
                            context.renderer.BindDescriptorSet(descriptorSetHandle);
                            context.renderer.DrawMesh(m_Mesh);
                        }
                        for (const RuntimeRenderItem* item : opaqueItems)
                        {
                            const MaterialPipelineResources* pipelineResources = GetMaterialPipelineResources(item->materialPipelineKey);
                            if (pipelineResources == nullptr)
                            {
                                pipelineResources = defaultMaterialPipelines;
                            }

                            const PipelineStateHandle pipelineHandle =
                                SelectMaterialPipelineState(*pipelineResources, item->material.blendMode);
                            const DescriptorSetHandle descriptorSetHandle =
                                SelectMaterialDescriptorSet(*pipelineResources, item->material.blendMode);
                            if (pipelineHandle == InvalidResourceHandle ||
                                descriptorSetHandle == InvalidResourceHandle ||
                                !ApplyMaterialBindings(
                                    item->material,
                                    item->bakedLightmapTexture,
                                    item->worldTransform,
                                    descriptorSetHandle))
                            {
                                continue;
                            }

                            context.renderer.BindPipeline(pipelineHandle);
                            context.renderer.BindDescriptorSet(descriptorSetHandle);
                            context.renderer.DrawMesh(item->mesh);
                        }
                        for (const RuntimeRenderItem* item : blendedItems)
                        {
                            const MaterialPipelineResources* pipelineResources = GetMaterialPipelineResources(item->materialPipelineKey);
                            if (pipelineResources == nullptr)
                            {
                                pipelineResources = defaultMaterialPipelines;
                            }

                            const PipelineStateHandle pipelineHandle =
                                SelectMaterialPipelineState(*pipelineResources, item->material.blendMode);
                            const DescriptorSetHandle descriptorSetHandle =
                                SelectMaterialDescriptorSet(*pipelineResources, item->material.blendMode);
                            if (pipelineHandle == InvalidResourceHandle ||
                                descriptorSetHandle == InvalidResourceHandle)
                            {
                                continue;
                            }
                            if (!ApplyMaterialBindings(
                                    item->material,
                                    item->bakedLightmapTexture,
                                    item->worldTransform,
                                    descriptorSetHandle))
                            {
                                continue;
                            }

                            context.renderer.BindPipeline(pipelineHandle);
                            context.renderer.BindDescriptorSet(descriptorSetHandle);
                            context.renderer.DrawMesh(item->mesh);
                        }
                        context.renderer.EndRenderPass();
                    });
                if (scenePass == RenderGraph::InvalidPassHandle)
                {
                    return false;
                }

                const std::string outputPassName = std::string(GetDebugName()) + ".OutputPass";
                const RenderGraph::PassHandle outputPass = m_RenderGraph.AddPass(
                    RenderGraphPassDesc { outputPassName, true },
                    [this](const RenderGraphContext& context)
                    {
                        if (m_RenderPass == InvalidResourceHandle)
                        {
                            return;
                        }

                        context.renderer.BeginRenderPass(m_RenderPass);
                        if (m_PostProcessMesh != InvalidResourceHandle &&
                            m_PostProcessPipelineState != InvalidResourceHandle &&
                            m_PostProcessDescriptorSet != InvalidResourceHandle &&
                            ApplyPostProcessBindings())
                        {
                            context.renderer.BindPipeline(m_PostProcessPipelineState);
                            context.renderer.BindDescriptorSet(m_PostProcessDescriptorSet);
                            context.renderer.DrawMesh(m_PostProcessMesh);
                        }
                        context.renderer.EndRenderPass();
                    });
                if (outputPass == RenderGraph::InvalidPassHandle)
                {
                    return false;
                }

                if (!m_RenderGraph.AddDependency(scenePass, setupPass))
                {
                    return false;
                }

                if (!m_RenderGraph.AddDependency(outputPass, scenePass))
                {
                    return false;
                }

                return m_RenderGraph.Compile();
            }

            bool m_Initialized = false;
            RenderPassHandle m_RenderPass = InvalidResourceHandle;
            FramebufferHandle m_Framebuffer = InvalidResourceHandle;
            PipelineStateHandle m_GridPipelineState = InvalidResourceHandle;
            PipelineStateHandle m_SkyPipelineState = InvalidResourceHandle;
            PipelineStateHandle m_PostProcessPipelineState = InvalidResourceHandle;
            RenderTargetHandle m_SceneColorRenderTarget = InvalidResourceHandle;
            FramebufferHandle m_SceneFramebuffer = InvalidResourceHandle;
            RenderPassHandle m_SceneRenderPass = InvalidResourceHandle;
            RenderTargetHandle m_DirectionalShadowRenderTarget = InvalidResourceHandle;
            RenderTargetHandle m_PointShadowRenderTarget = InvalidResourceHandle;
            RenderTargetHandle m_SpotShadowRenderTarget = InvalidResourceHandle;
            FramebufferHandle m_DirectionalShadowFramebuffer = InvalidResourceHandle;
            FramebufferHandle m_PointShadowFramebuffer = InvalidResourceHandle;
            FramebufferHandle m_SpotShadowFramebuffer = InvalidResourceHandle;
            RenderPassHandle m_DirectionalShadowRenderPass = InvalidResourceHandle;
            RenderPassHandle m_PointShadowRenderPass = InvalidResourceHandle;
            RenderPassHandle m_SpotShadowRenderPass = InvalidResourceHandle;
            PipelineStateHandle m_DirectionalShadowPipelineState = InvalidResourceHandle;
            PipelineStateHandle m_PointShadowPipelineState = InvalidResourceHandle;
            PipelineStateHandle m_SpotShadowPipelineState = InvalidResourceHandle;
            MeshHandle m_PostProcessMesh = InvalidResourceHandle;
            MeshHandle m_GridMesh = InvalidResourceHandle;
            MeshHandle m_SkyMesh = InvalidResourceHandle;
            MeshHandle m_Mesh = InvalidResourceHandle;
            DescriptorSetHandle m_PostProcessDescriptorSet = InvalidResourceHandle;
            DescriptorSetHandle m_GridDescriptorSet = InvalidResourceHandle;
            DescriptorSetHandle m_SkyDescriptorSet = InvalidResourceHandle;
            DescriptorSetHandle m_DirectionalShadowDescriptorSet = InvalidResourceHandle;
            DescriptorSetHandle m_PointShadowDescriptorSet = InvalidResourceHandle;
            DescriptorSetHandle m_SpotShadowDescriptorSet = InvalidResourceHandle;
            IRenderBackend* m_RenderBackend = nullptr;
            GPUResourceManager* m_ResourceManager = nullptr;
            ShaderSystem m_ShaderSystem;
            LightingSystem m_LightingSystem;
            TextureSystem m_TextureSystem;
            ShaderProgramDesc m_DefaultMaterialShaderProgramDesc {};
            ShaderProgramDesc m_ShadowShaderProgramDesc {};
            ShaderProgramDesc m_SkyShaderProgramDesc {};
            ShaderProgramDesc m_GridShaderProgramDesc {};
            ShaderProgramDesc m_PostProcessShaderProgramDesc {};
            TextureHandle m_DefaultAlbedoTexture = InvalidTextureHandle;
            TextureHandle m_DefaultNormalTexture = InvalidTextureHandle;
            TextureHandle m_DefaultOrmTexture = InvalidTextureHandle;
            TextureHandle m_DefaultMetallicTexture = InvalidTextureHandle;
            TextureHandle m_DefaultRoughnessTexture = InvalidTextureHandle;
            TextureHandle m_DefaultAmbientOcclusionTexture = InvalidTextureHandle;
            TextureHandle m_DefaultEmissiveTexture = InvalidTextureHandle;
            TextureHandle m_DefaultOpacityTexture = InvalidTextureHandle;
            TextureHandle m_DefaultHeightTexture = InvalidTextureHandle;
            TextureHandle m_DefaultIrradianceTexture = InvalidTextureHandle;
            TextureHandle m_DefaultPrefilterTexture = InvalidTextureHandle;
            TextureHandle m_DefaultShadowTexture = InvalidTextureHandle;
            TextureHandle m_CurrentEnvironmentTexture = InvalidTextureHandle;
            TextureHandle m_CurrentIrradianceTexture = InvalidTextureHandle;
            TextureHandle m_CurrentPrefilterTexture = InvalidTextureHandle;
            RenderGraph m_RenderGraph;
            std::uint64_t m_RenderGraphFrameIndex = 0;
            std::uint64_t m_GridMeshRevision = 0;
            std::uint64_t m_SkyMeshRevision = 0;
            std::uint64_t m_OverrideMeshRevision = 0;
            std::uint64_t m_RenderItemsRevision = 0;
            bool m_HadGridMesh = false;
            bool m_HadSkyMesh = false;
            bool m_HadOverrideMesh = false;
            bool m_HasPrefilteredEnvironment = false;
            bool m_HasDirectionalShadow = false;
            bool m_HasPointShadow = false;
            bool m_HasSpotShadow = false;
            int m_ShadowedPointLightIndex = -1;
            int m_ShadowedSpotLightIndex = -1;
            float m_PointShadowBias = 0.0025f;
            bool m_PointShadowSoftShadows = true;
            std::uint32_t m_PointShadowMapSize = kPointShadowMapSize;
            std::uint32_t m_SceneColorTargetWidth = 0;
            std::uint32_t m_SceneColorTargetHeight = 0;
            std::uint32_t m_CurrentEnvironmentSourceWidth = 0;
            std::uint32_t m_CurrentEnvironmentSourceHeight = 0;
            std::filesystem::path m_CurrentEnvironmentSourcePath;
            std::array<float, 16> m_DirectionalShadowViewProjection {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f };
            std::array<std::array<float, 16>, LightingSystem::kPointShadowFaceCount> m_PointShadowViewProjections {};
            std::array<float, 16> m_SpotShadowViewProjection {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f };
            std::array<float, 16> m_DirectionalShadowTextureMatrix {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f };
            std::array<std::array<float, 16>, LightingSystem::kPointShadowFaceCount> m_PointShadowTextureMatrices {};
            std::array<float, 4> m_PointShadowLightPositionRange { 0.0f, 0.0f, 0.0f, 1.0f };
            std::array<float, 16> m_SpotShadowTextureMatrix {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f };
            std::unordered_map<std::string, RuntimeRenderItem> m_RenderItems;
            std::unordered_map<std::string, SharedMeshResource> m_SharedMeshes;
            std::vector<std::string> m_RenderItemOrder;
            std::unordered_map<std::string, MaterialPipelineResources> m_MaterialPipelines;
            std::unordered_map<std::string, ShadowPipelineResources> m_ShadowMaterialPipelines;
            std::string m_DefaultMaterialPipelineKey;
            std::string m_DefaultShadowPipelineKey;
            std::unordered_map<std::string, TextureHandle> m_ExternalTextures;
            std::unordered_map<DescriptorSetHandle, std::size_t> m_LastMaterialTextureBindingHashes;
            std::unordered_map<DescriptorSetHandle, std::size_t> m_LastShadowTextureBindingHashes;
            std::array<float, 16> m_ViewProjection = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
            std::array<float, 3> m_CameraWorldPosition { 0.0f, 0.0f, 5.0f };
            Color m_SceneClearColor = { 0.05f, 0.07f, 0.12f, 1.0f };
            float m_CurrentAutoExposureEV = 0.0f;
            float m_LastAutoExposureTimeSeconds = 0.0f;
            bool m_AutoExposureInitialized = false;
        };

        class CoreLitePipeline final : public ScenePipelineBase
        {
        public:
            const char* GetDebugName() const override
            {
                return "CoreLitePipeline";
            }

        protected:
            Color GetClearColor() const override
            {
                return { 0.07f, 0.08f, 0.10f, 0.0f };
            }

            const char* GetPipelineMacro() const override
            {
                return "CORELITE";
            }

            float GetAmbientLightIntensity() const override
            {
                return 1.15f;
            }

            float GetDirectionalLightIntensity() const override
            {
                return 0.85f;
            }
        };

        class CoreXPipeline final : public ScenePipelineBase
        {
        public:
            const char* GetDebugName() const override
            {
                return "CoreXPipeline";
            }

        protected:
            Color GetClearColor() const override
            {
                return { 0.03f, 0.04f, 0.07f, 0.0f };
            }

            const char* GetPipelineMacro() const override
            {
                return "COREX";
            }

            float GetAmbientLightIntensity() const override
            {
                return 1.35f;
            }

            float GetDirectionalLightIntensity() const override
            {
                return 1.10f;
            }
        };
    }

    std::unique_ptr<IRenderPipeline> CreateRenderPipeline(const RenderPipelineProfile profile)
    {
        switch (profile)
        {
        case RenderPipelineProfile::CoreX:
            return std::make_unique<CoreXPipeline>();
        case RenderPipelineProfile::CoreLite:
        default:
            return std::make_unique<CoreLitePipeline>();
        }
    }
}
