#include "Luma/Editor/Assets/ThumbnailService.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <stb_image.h>

#ifndef TINYEXR_USE_MINIZ
#define TINYEXR_USE_MINIZ (0)
#endif
#ifndef TINYEXR_USE_STB_ZLIB
#define TINYEXR_USE_STB_ZLIB (1)
#endif
#define TINYEXR_IMPLEMENTATION
#include <tinyexr.h>

#include "Luma/Asset/Core/MeshAssetIO.h"
#include "Luma/RHI/IRenderBackend.h"

namespace Luma::Editor
{
    namespace
    {
        constexpr int kDefaultThumbnailSize = 128;

        struct Vec3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
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

        Vec3 Normalize(const Vec3& value)
        {
            const float lengthSq = Dot(value, value);
            if (lengthSq <= 1.0e-8f)
            {
                return {};
            }

            const float invLength = 1.0f / std::sqrt(lengthSq);
            return value * invLength;
        }

        Vec3 RotateAroundY(const Vec3& value, const float radians)
        {
            const float c = std::cos(radians);
            const float s = std::sin(radians);
            return {
                value.x * c + value.z * s,
                value.y,
                -value.x * s + value.z * c
            };
        }

        Vec3 RotateAroundX(const Vec3& value, const float radians)
        {
            const float c = std::cos(radians);
            const float s = std::sin(radians);
            return {
                value.x,
                value.y * c - value.z * s,
                value.y * s + value.z * c
            };
        }

        float Length(const Vec3& value)
        {
            return std::sqrt(Dot(value, value));
        }

        std::string ToLowerString(std::string value)
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

        std::uint64_t CombineContentStamp(const std::uint64_t seed, const std::uint64_t value)
        {
            std::uint64_t combined = seed;
            combined ^= value + 0x9e3779b97f4a7c15ull + (combined << 6) + (combined >> 2);
            return combined;
        }

        bool IsImageThumbnailSource(const std::filesystem::path& imagePath)
        {
            const std::string extension = ToLowerString(imagePath.extension().string());
            return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" ||
                extension == ".bmp" || extension == ".dds" || extension == ".hdr" || extension == ".exr" ||
                extension == ".lumatex" || extension == ".lumasky";
        }

        bool IsHdrThumbnailSource(const std::filesystem::path& imagePath)
        {
            const std::string extension = ToLowerString(imagePath.extension().string());
            return extension == ".hdr" || extension == ".exr" || extension == ".lumasky";
        }

        bool IsMeshThumbnailSource(const std::filesystem::path& meshPath)
        {
            const std::string extension = ToLowerString(meshPath.extension().string());
            return extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb" || extension == ".lumamesh";
        }

        std::uint8_t LinearToSRGB8(const float linearValue)
        {
            const float clamped = std::clamp(linearValue, 0.0f, 1.0f);
            const float srgb = std::pow(clamped, 1.0f / 2.2f);
            return static_cast<std::uint8_t>(std::clamp(srgb * 255.0f, 0.0f, 255.0f));
        }

        bool ReadFileBytes(const std::filesystem::path& filePath, std::vector<std::uint8_t>& outBytes)
        {
            std::ifstream file(filePath, std::ios::binary);
            if (!file.is_open())
            {
                return false;
            }

            file.seekg(0, std::ios::end);
            const std::streamsize fileSize = file.tellg();
            if (fileSize <= 0)
            {
                return false;
            }
            file.seekg(0, std::ios::beg);

            outBytes.resize(static_cast<std::size_t>(fileSize));
            file.read(reinterpret_cast<char*>(outBytes.data()), fileSize);
            return file.good() || file.eof();
        }

        struct IntermediateSourceData
        {
            std::vector<std::uint8_t> payload;
            const std::uint8_t* sourceBytes = nullptr;
            std::size_t sourceSize = 0;
            std::string sourceName;
        };

        bool LoadIntermediateSourceData(const std::filesystem::path& filePath, IntermediateSourceData& outData)
        {
            outData = {};
            if (!ReadFileBytes(filePath, outData.payload))
            {
                return false;
            }

            const std::string_view payloadView(
                reinterpret_cast<const char*>(outData.payload.data()),
                outData.payload.size());

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

            std::istringstream headerStream(std::string(payloadView.substr(0, headerEnd)));
            std::string line;
            while (std::getline(headerStream, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (line.rfind("Source=", 0) == 0)
                {
                    outData.sourceName = line.substr(std::strlen("Source="));
                    break;
                }
            }

            const std::size_t sourceOffset = headerEnd + separatorLength;
            if (sourceOffset >= outData.payload.size())
            {
                return false;
            }

            outData.sourceBytes = outData.payload.data() + sourceOffset;
            outData.sourceSize = outData.payload.size() - sourceOffset;
            return outData.sourceSize > 0;
        }
        void ToneMapToRGBA8(
            const float* linearPixels,
            const int width,
            const int height,
            const int componentStride,
            std::vector<std::uint8_t>& outPixels)
        {
            outPixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4ULL);
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
                4);
            if (pixels == nullptr || outWidth <= 0 || outHeight <= 0)
            {
                return false;
            }

            outPixels.assign(
                pixels,
                pixels + static_cast<std::size_t>(outWidth) * static_cast<std::size_t>(outHeight) * 4ULL);
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

        void* CreateThumbnailTexture(
            IRenderBackend& renderer,
            const std::vector<std::uint8_t>& rgbaPixels,
            const int width,
            const int height)
        {
            if (rgbaPixels.empty() || width <= 0 || height <= 0)
            {
                return nullptr;
            }

            return renderer.CreateImGuiTextureRGBA8(
                static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height),
                rgbaPixels.data());
        }

        struct MeshPreviewVertex
        {
            Vec3 position {};
            Vec3 normal { 0.0f, 1.0f, 0.0f };
        };

        struct MeshPreviewGeometry
        {
            std::vector<MeshPreviewVertex> vertices;
            std::vector<std::uint32_t> indices;
            bool hasIndices = false;
            bool hasBounds = false;
            Vec3 boundsMin {};
            Vec3 boundsMax {};
        };

        void FillMeshNormalsIfNeeded(std::vector<MeshPreviewGeometry>& geometry)
        {
            for (MeshPreviewGeometry& mesh : geometry)
            {
                if (mesh.vertices.empty())
                {
                    continue;
                }

                bool hasAnyNormal = false;
                for (const MeshPreviewVertex& vertex : mesh.vertices)
                {
                    if (Length(vertex.normal) > 1.0e-4f)
                    {
                        hasAnyNormal = true;
                        break;
                    }
                }
                if (hasAnyNormal)
                {
                    continue;
                }

                for (MeshPreviewVertex& vertex : mesh.vertices)
                {
                    vertex.normal = { 0.0f, 1.0f, 0.0f };
                }

                if (mesh.hasIndices && mesh.indices.size() >= 3)
                {
                    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
                    {
                        const std::uint32_t i0 = mesh.indices[i + 0];
                        const std::uint32_t i1 = mesh.indices[i + 1];
                        const std::uint32_t i2 = mesh.indices[i + 2];
                        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
                        {
                            continue;
                        }

                        const Vec3 e0 = mesh.vertices[i1].position - mesh.vertices[i0].position;
                        const Vec3 e1 = mesh.vertices[i2].position - mesh.vertices[i0].position;
                        const Vec3 faceNormal = Normalize({
                            e0.y * e1.z - e0.z * e1.y,
                            e0.z * e1.x - e0.x * e1.z,
                            e0.x * e1.y - e0.y * e1.x
                        });
                        mesh.vertices[i0].normal = mesh.vertices[i0].normal + faceNormal;
                        mesh.vertices[i1].normal = mesh.vertices[i1].normal + faceNormal;
                        mesh.vertices[i2].normal = mesh.vertices[i2].normal + faceNormal;
                    }

                    for (MeshPreviewVertex& vertex : mesh.vertices)
                    {
                        vertex.normal = Normalize(vertex.normal);
                    }
                }
            }
        }
        void GatherAssimpNodeGeometry(
            const aiScene& scene,
            const aiNode& node,
            std::vector<MeshPreviewGeometry>& outGeometry)
        {
            for (unsigned int i = 0; i < node.mNumMeshes; ++i)
            {
                const unsigned int meshIndex = node.mMeshes[i];
                if (meshIndex >= scene.mNumMeshes || scene.mMeshes[meshIndex] == nullptr)
                {
                    continue;
                }

                const aiMesh& sourceMesh = *scene.mMeshes[meshIndex];
                if (!sourceMesh.HasPositions() || sourceMesh.mNumVertices == 0)
                {
                    continue;
                }

                MeshPreviewGeometry geometry {};
                geometry.vertices.reserve(sourceMesh.mNumVertices);
                geometry.indices.reserve(sourceMesh.mNumFaces * 3ULL);
                geometry.boundsMin = {
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max()
                };
                geometry.boundsMax = {
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest()
                };

                for (unsigned int vertexIndex = 0; vertexIndex < sourceMesh.mNumVertices; ++vertexIndex)
                {
                    MeshPreviewVertex vertex {};
                    vertex.position = {
                        sourceMesh.mVertices[vertexIndex].x,
                        sourceMesh.mVertices[vertexIndex].y,
                        sourceMesh.mVertices[vertexIndex].z
                    };
                    if (sourceMesh.HasNormals())
                    {
                        vertex.normal = {
                            sourceMesh.mNormals[vertexIndex].x,
                            sourceMesh.mNormals[vertexIndex].y,
                            sourceMesh.mNormals[vertexIndex].z
                        };
                    }
                    geometry.boundsMin = {
                        std::min(geometry.boundsMin.x, vertex.position.x),
                        std::min(geometry.boundsMin.y, vertex.position.y),
                        std::min(geometry.boundsMin.z, vertex.position.z)
                    };
                    geometry.boundsMax = {
                        std::max(geometry.boundsMax.x, vertex.position.x),
                        std::max(geometry.boundsMax.y, vertex.position.y),
                        std::max(geometry.boundsMax.z, vertex.position.z)
                    };
                    geometry.vertices.push_back(vertex);
                }

                geometry.hasBounds = !geometry.vertices.empty();
                for (unsigned int faceIndex = 0; faceIndex < sourceMesh.mNumFaces; ++faceIndex)
                {
                    const aiFace& face = sourceMesh.mFaces[faceIndex];
                    if (face.mNumIndices < 3)
                    {
                        continue;
                    }

                    for (unsigned int i1 = 1; i1 + 1 < face.mNumIndices; ++i1)
                    {
                        geometry.indices.push_back(static_cast<std::uint32_t>(face.mIndices[0]));
                        geometry.indices.push_back(static_cast<std::uint32_t>(face.mIndices[i1]));
                        geometry.indices.push_back(static_cast<std::uint32_t>(face.mIndices[i1 + 1]));
                    }
                }

                geometry.hasIndices = !geometry.indices.empty();
                outGeometry.push_back(std::move(geometry));
            }

            for (unsigned int i = 0; i < node.mNumChildren; ++i)
            {
                if (node.mChildren[i] != nullptr)
                {
                    GatherAssimpNodeGeometry(scene, *node.mChildren[i], outGeometry);
                }
            }
        }

        bool LoadMeshGeometryForThumbnail(const std::filesystem::path& meshPath, std::vector<MeshPreviewGeometry>& outGeometry)
        {
            outGeometry.clear();

            Assets::MeshAssetData meshAsset;
            std::string error;
            if (!Assets::LoadMeshAssetData(meshPath, meshAsset, error))
            {
                return false;
            }

            MeshPreviewGeometry geometry;
            geometry.vertices.reserve(meshAsset.mesh.vertices.size());
            geometry.indices = meshAsset.mesh.indices;
            geometry.hasIndices = !geometry.indices.empty();
            geometry.hasBounds = true;
            geometry.boundsMin = { meshAsset.boundsMin[0], meshAsset.boundsMin[1], meshAsset.boundsMin[2] };
            geometry.boundsMax = { meshAsset.boundsMax[0], meshAsset.boundsMax[1], meshAsset.boundsMax[2] };

            for (const PrimitiveVertex& vertex : meshAsset.mesh.vertices)
            {
                MeshPreviewVertex previewVertex;
                previewVertex.position = { vertex.position[0], vertex.position[1], vertex.position[2] };
                geometry.vertices.push_back(previewVertex);
            }

            outGeometry.push_back(std::move(geometry));
            FillMeshNormalsIfNeeded(outGeometry);
            return !outGeometry.empty();
        }

        struct PreparedThumbnailPixels
        {
            std::vector<std::uint8_t> rgbaPixels;
            int width = 0;
            int height = 0;
            bool succeeded = false;
        };

        PreparedThumbnailPixels PrepareMeshThumbnailPixels(
            const std::filesystem::path& meshPath,
            const ThumbnailSize& requestedSize)
        {
            std::vector<MeshPreviewGeometry> geometry;
            if (!LoadMeshGeometryForThumbnail(meshPath, geometry))
            {
                return {};
            }

            const int thumbnailSize = std::clamp(std::max(requestedSize.width, requestedSize.height), 48, 512);
            std::vector<std::uint8_t> rgba(
                static_cast<std::size_t>(thumbnailSize) * static_cast<std::size_t>(thumbnailSize) * 4ULL,
                0);
            std::vector<float> depth(
                static_cast<std::size_t>(thumbnailSize) * static_cast<std::size_t>(thumbnailSize),
                std::numeric_limits<float>::infinity());

            for (int y = 0; y < thumbnailSize; ++y)
            {
                for (int x = 0; x < thumbnailSize; ++x)
                {
                    const std::size_t pixelIndex =
                        static_cast<std::size_t>(y) * static_cast<std::size_t>(thumbnailSize) + static_cast<std::size_t>(x);
                    rgba[pixelIndex * 4ULL + 0] = 26;
                    rgba[pixelIndex * 4ULL + 1] = 29;
                    rgba[pixelIndex * 4ULL + 2] = 36;
                    rgba[pixelIndex * 4ULL + 3] = 255;
                }
            }

            Vec3 boundsMin {
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()
            };
            Vec3 boundsMax {
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()
            };

            for (const MeshPreviewGeometry& mesh : geometry)
            {
                if (!mesh.hasBounds)
                {
                    continue;
                }

                boundsMin = {
                    std::min(boundsMin.x, mesh.boundsMin.x),
                    std::min(boundsMin.y, mesh.boundsMin.y),
                    std::min(boundsMin.z, mesh.boundsMin.z)
                };
                boundsMax = {
                    std::max(boundsMax.x, mesh.boundsMax.x),
                    std::max(boundsMax.y, mesh.boundsMax.y),
                    std::max(boundsMax.z, mesh.boundsMax.z)
                };
            }

            if (boundsMin.x > boundsMax.x || boundsMin.y > boundsMax.y || boundsMin.z > boundsMax.z)
            {
                boundsMin = { -0.5f, -0.5f, -0.5f };
                boundsMax = { 0.5f, 0.5f, 0.5f };
            }

            const Vec3 center = (boundsMin + boundsMax) * 0.5f;
            const float radius = std::max(0.001f, Length(boundsMax - boundsMin) * 0.5f);
            constexpr float kYaw = 0.62f;
            constexpr float kPitch = -0.48f;
            constexpr float kCameraDistance = 2.8f;
            const float halfSize = static_cast<float>(thumbnailSize - 1) * 0.5f;
            const float focal = static_cast<float>(thumbnailSize) * 0.88f;
            const Vec3 lightDir = Normalize({ -0.35f, -1.0f, -0.25f });

            struct ProjectedVertex
            {
                float x = 0.0f;
                float y = 0.0f;
                float depth = 0.0f;
                Vec3 normal {};
                bool valid = false;
            };

            auto edge = [](const float ax, const float ay, const float bx, const float by, const float px, const float py) -> float
            {
                return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
            };

            auto projectVertex = [&](const MeshPreviewVertex& vertex) -> ProjectedVertex
            {
                const Vec3 normalizedPosition = (vertex.position - center) * (1.0f / radius);
                const Vec3 rotatedPosition = RotateAroundX(RotateAroundY(normalizedPosition, kYaw), kPitch);
                const float viewDepth = rotatedPosition.z + kCameraDistance;
                if (viewDepth <= 0.05f)
                {
                    return {};
                }

                ProjectedVertex projected {};
                projected.x = rotatedPosition.x * focal / viewDepth + halfSize;
                projected.y = -rotatedPosition.y * focal / viewDepth + halfSize;
                projected.depth = viewDepth;
                projected.normal = Normalize(RotateAroundX(RotateAroundY(vertex.normal, kYaw), kPitch));
                projected.valid = std::isfinite(projected.x) && std::isfinite(projected.y);
                return projected;
            };

            auto rasterTriangle = [&](const MeshPreviewVertex& va, const MeshPreviewVertex& vb, const MeshPreviewVertex& vc)
            {
                const ProjectedVertex a = projectVertex(va);
                const ProjectedVertex b = projectVertex(vb);
                const ProjectedVertex c = projectVertex(vc);
                if (!a.valid || !b.valid || !c.valid)
                {
                    return;
                }

                const float area = edge(a.x, a.y, b.x, b.y, c.x, c.y);
                if (std::abs(area) < 1.0e-5f)
                {
                    return;
                }

                const int minX = std::max(0, static_cast<int>(std::floor(std::min({ a.x, b.x, c.x }))));
                const int maxX = std::min(thumbnailSize - 1, static_cast<int>(std::ceil(std::max({ a.x, b.x, c.x }))));
                const int minY = std::max(0, static_cast<int>(std::floor(std::min({ a.y, b.y, c.y }))));
                const int maxY = std::min(thumbnailSize - 1, static_cast<int>(std::ceil(std::max({ a.y, b.y, c.y }))));

                for (int y = minY; y <= maxY; ++y)
                {
                    for (int x = minX; x <= maxX; ++x)
                    {
                        const float sampleX = static_cast<float>(x) + 0.5f;
                        const float sampleY = static_cast<float>(y) + 0.5f;
                        const float w0 = edge(b.x, b.y, c.x, c.y, sampleX, sampleY);
                        const float w1 = edge(c.x, c.y, a.x, a.y, sampleX, sampleY);
                        const float w2 = edge(a.x, a.y, b.x, b.y, sampleX, sampleY);
                        const bool insidePositive = (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f);
                        const bool insideNegative = (w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f);
                        if (!insidePositive && !insideNegative)
                        {
                            continue;
                        }

                        const float invArea = 1.0f / area;
                        const float bw0 = w0 * invArea;
                        const float bw1 = w1 * invArea;
                        const float bw2 = w2 * invArea;
                        const float z = bw0 * a.depth + bw1 * b.depth + bw2 * c.depth;
                        const std::size_t pixelIndex =
                            static_cast<std::size_t>(y) * static_cast<std::size_t>(thumbnailSize) + static_cast<std::size_t>(x);
                        if (z >= depth[pixelIndex])
                        {
                            continue;
                        }

                        depth[pixelIndex] = z;
                        const Vec3 normal = Normalize(a.normal * bw0 + b.normal * bw1 + c.normal * bw2);
                        const float ndl = std::max(Dot(normal, { -lightDir.x, -lightDir.y, -lightDir.z }), 0.0f);
                        const float lit = 0.22f + ndl * 0.78f;
                        rgba[pixelIndex * 4ULL + 0] = static_cast<std::uint8_t>(lit * 214.0f);
                        rgba[pixelIndex * 4ULL + 1] = static_cast<std::uint8_t>(lit * 219.0f);
                        rgba[pixelIndex * 4ULL + 2] = static_cast<std::uint8_t>(lit * 230.0f);
                        rgba[pixelIndex * 4ULL + 3] = 255;
                    }
                }
            };

            std::size_t triangleBudget = 12000;
            for (const MeshPreviewGeometry& mesh : geometry)
            {
                if (triangleBudget == 0 || mesh.vertices.size() < 3)
                {
                    break;
                }

                if (mesh.hasIndices && mesh.indices.size() >= 3)
                {
                    const std::size_t triangleCount = mesh.indices.size() / 3ULL;
                    for (std::size_t tri = 0; tri < triangleCount && triangleBudget > 0; ++tri, --triangleBudget)
                    {
                        const std::uint32_t i0 = mesh.indices[tri * 3ULL + 0ULL];
                        const std::uint32_t i1 = mesh.indices[tri * 3ULL + 1ULL];
                        const std::uint32_t i2 = mesh.indices[tri * 3ULL + 2ULL];
                        if (i0 >= mesh.vertices.size() || i1 >= mesh.vertices.size() || i2 >= mesh.vertices.size())
                        {
                            continue;
                        }

                        rasterTriangle(mesh.vertices[i0], mesh.vertices[i1], mesh.vertices[i2]);
                    }
                }
                else
                {
                    const std::size_t triVertexCount = (mesh.vertices.size() / 3ULL) * 3ULL;
                    for (std::size_t i = 0; i + 2 < triVertexCount && triangleBudget > 0; i += 3ULL, --triangleBudget)
                    {
                        rasterTriangle(mesh.vertices[i + 0], mesh.vertices[i + 1], mesh.vertices[i + 2]);
                    }
                }
            }

            PreparedThumbnailPixels output {};
            output.rgbaPixels = std::move(rgba);
            output.width = thumbnailSize;
            output.height = thumbnailSize;
            output.succeeded = true;
            return output;
        }

        PreparedThumbnailPixels PrepareImageThumbnailPixels(const std::filesystem::path& assetPath)
        {
            stbi_set_flip_vertically_on_load(0);

            PreparedThumbnailPixels output {};
            std::vector<std::uint8_t> rgbaPixels;
            int width = 0;
            int height = 0;
            const std::string extension = ToLowerString(assetPath.extension().string());

            if (extension == ".lumatex" || extension == ".lumasky")
            {
                IntermediateSourceData intermediateSource;
                if (!LoadIntermediateSourceData(assetPath, intermediateSource))
                {
                    return {};
                }

                const std::string sourceExtension =
                    ToLowerString(std::filesystem::path(intermediateSource.sourceName).extension().string());
                const bool sourceIsHdr = (sourceExtension == ".hdr" || sourceExtension == ".exr");
                if (sourceIsHdr)
                {
                    if (!DecodeHdrToRGBA8FromMemory(
                            intermediateSource.sourceBytes,
                            intermediateSource.sourceSize,
                            sourceExtension == ".exr",
                            rgbaPixels,
                            width,
                            height))
                    {
                        return {};
                    }
                }
                else
                {
                    if (!DecodeLdrToRGBA8FromMemory(
                            intermediateSource.sourceBytes,
                            intermediateSource.sourceSize,
                            rgbaPixels,
                            width,
                            height))
                    {
                        return {};
                    }
                }
            }
            else if (IsHdrThumbnailSource(assetPath))
            {
                std::vector<std::uint8_t> fileBytes;
                if (!ReadFileBytes(assetPath, fileBytes))
                {
                    return {};
                }

                if (!DecodeHdrToRGBA8FromMemory(
                        fileBytes.data(),
                        fileBytes.size(),
                        extension == ".exr",
                        rgbaPixels,
                        width,
                        height))
                {
                    return {};
                }
            }
            else
            {
                int channels = 0;
                stbi_uc* pixels = stbi_load(assetPath.string().c_str(), &width, &height, &channels, 4);
                if (pixels == nullptr || width <= 0 || height <= 0)
                {
                    return {};
                }

                rgbaPixels.assign(
                    pixels,
                    pixels + static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4ULL);
                stbi_image_free(pixels);
            }

            output.rgbaPixels = std::move(rgbaPixels);
            output.width = width;
            output.height = height;
            output.succeeded = true;
            return output;
        }

        ThumbnailRenderOutput RenderMeshThumbnail(
            IRenderBackend& renderer,
            const std::filesystem::path& meshPath,
            const ThumbnailSize& requestedSize)
        {
            const PreparedThumbnailPixels prepared = PrepareMeshThumbnailPixels(meshPath, requestedSize);
            if (!prepared.succeeded)
            {
                return {};
            }

            ThumbnailRenderOutput output {};
            output.texture = CreateThumbnailTexture(renderer, prepared.rgbaPixels, prepared.width, prepared.height);
            output.width = prepared.width;
            output.height = prepared.height;
            return output;
        }
        class ImageThumbnailRenderer final : public IThumbnailRenderer
        {
        public:
            int Priority() const override
            {
                return 100;
            }

            bool CanRender(const ThumbnailRenderContext& context) const override
            {
                return context.assetClass == ThumbnailAssetClass::Image && IsImageThumbnailSource(context.assetPath);
            }

            ThumbnailRenderOutput Render(IRenderBackend& renderer, const ThumbnailRenderContext& context) override
            {
                const PreparedThumbnailPixels prepared = PrepareImageThumbnailPixels(context.assetPath);
                if (!prepared.succeeded)
                {
                    return {};
                }

                ThumbnailRenderOutput output {};
                output.texture = CreateThumbnailTexture(renderer, prepared.rgbaPixels, prepared.width, prepared.height);
                output.width = prepared.width;
                output.height = prepared.height;
                return output;
            }
        };

        class MeshThumbnailRenderer final : public IThumbnailRenderer
        {
        public:
            int Priority() const override
            {
                return 90;
            }

            bool CanRender(const ThumbnailRenderContext& context) const override
            {
                return context.assetClass == ThumbnailAssetClass::Mesh && IsMeshThumbnailSource(context.assetPath);
            }

            ThumbnailRenderOutput Render(IRenderBackend& renderer, const ThumbnailRenderContext& context) override
            {
                return RenderMeshThumbnail(renderer, context.assetPath, context.size);
            }
        };
    }

    ThumbnailService::ThumbnailService()
    {
        RegisterBuiltInRenderers();
        StartMeshWorkerThreads();
    }

    ThumbnailService::~ThumbnailService()
    {
        StopMeshWorkerThreads();
        Shutdown(m_LastRenderer);
    }

    ThumbnailHandle ThumbnailService::RequestThumbnail(
        const std::filesystem::path& assetPath,
        ThumbnailSize size,
        const ThumbnailRequestOptions& options)
    {
        return EnsureRequest(0, assetPath, SanitizeSize(size), options);
    }

    ThumbnailHandle ThumbnailService::RequestThumbnail(
        const Assets::AssetID assetId,
        const std::filesystem::path& assetPath,
        ThumbnailSize size,
        const ThumbnailRequestOptions& options)
    {
        return EnsureRequest(assetId, assetPath, SanitizeSize(size), options);
    }

    ThumbnailQueryResult ThumbnailService::GetThumbnail(const ThumbnailHandle handle) const
    {
        const auto it = m_Entries.find(handle);
        if (it == m_Entries.end())
        {
            return {};
        }

        return {
            it->second.texture,
            it->second.state,
            it->second.width,
            it->second.height
        };
    }

    ThumbnailQueryResult ThumbnailService::GetThumbnail(
        const std::filesystem::path& assetPath,
        ThumbnailSize size) const
    {
        size = SanitizeSize(size);
        const std::string key = BuildPathSizeKey(assetPath, size);
        const auto handleIt = m_PathSizeToHandle.find(key);
        if (handleIt == m_PathSizeToHandle.end())
        {
            return {};
        }

        return GetThumbnail(handleIt->second);
    }

    ThumbnailQueryResult ThumbnailService::GetThumbnailByAssetID(const Assets::AssetID assetId, ThumbnailSize size) const
    {
        size = SanitizeSize(size);
        const auto it = m_AssetToHandles.find(assetId);
        if (it == m_AssetToHandles.end())
        {
            return {};
        }

        for (const ThumbnailHandle handle : it->second)
        {
            const auto entryIt = m_Entries.find(handle);
            if (entryIt == m_Entries.end())
            {
                continue;
            }

            if (entryIt->second.size.width == size.width && entryIt->second.size.height == size.height)
            {
                return {
                    entryIt->second.texture,
                    entryIt->second.state,
                    entryIt->second.width,
                    entryIt->second.height
                };
            }
        }

        return {};
    }

    void ThumbnailService::Invalidate(const std::filesystem::path& assetPath)
    {
        const std::string normalizedPath = NormalizePathKey(assetPath);
        if (normalizedPath.empty())
        {
            return;
        }

        std::vector<ThumbnailHandle> handlesToRemove;
        handlesToRemove.reserve(m_Entries.size());
        for (const auto& [handle, entry] : m_Entries)
        {
            if (entry.normalizedPathKey == normalizedPath)
            {
                handlesToRemove.push_back(handle);
            }
        }

        for (const ThumbnailHandle handle : handlesToRemove)
        {
            RemoveHandle(handle);
        }
    }

    void ThumbnailService::Invalidate(const Assets::AssetID assetId)
    {
        const auto it = m_AssetToHandles.find(assetId);
        if (it == m_AssetToHandles.end())
        {
            return;
        }

        const std::vector<ThumbnailHandle> handles = it->second;
        for (const ThumbnailHandle handle : handles)
        {
            RemoveHandle(handle);
        }
    }

    void ThumbnailService::InvalidateAll()
    {
        std::vector<ThumbnailHandle> handlesToRemove;
        handlesToRemove.reserve(m_Entries.size());
        for (const auto& [handle, entry] : m_Entries)
        {
            (void)entry;
            handlesToRemove.push_back(handle);
        }

        for (const ThumbnailHandle handle : handlesToRemove)
        {
            RemoveHandle(handle);
        }
    }
    void ThumbnailService::InvalidateAll(const ThumbnailAssetClass assetClass)
    {
        std::vector<ThumbnailHandle> handlesToRemove;
        handlesToRemove.reserve(m_Entries.size());
        for (const auto& [handle, entry] : m_Entries)
        {
            if (entry.assetClass == assetClass)
            {
                handlesToRemove.push_back(handle);
            }
        }

        for (const ThumbnailHandle handle : handlesToRemove)
        {
            RemoveHandle(handle);
        }
    }

    void ThumbnailService::Prewarm(const std::vector<std::filesystem::path>& assetPaths, ThumbnailSize size)
    {
        size = SanitizeSize(size);
        ThumbnailRequestOptions options {};
        options.highPriority = false;
        options.forceRegenerate = false;

        for (const std::filesystem::path& path : assetPaths)
        {
            EnsureRequest(0, path, size, options);
        }
    }

    void ThumbnailService::PrewarmFolder(const std::filesystem::path& folderPath, ThumbnailSize size)
    {
        size = SanitizeSize(size);

        std::error_code ec;
        if (!std::filesystem::exists(folderPath, ec) || !std::filesystem::is_directory(folderPath, ec))
        {
            return;
        }

        std::vector<std::filesystem::path> entries;
        for (const auto& entry : std::filesystem::directory_iterator(
                 folderPath,
                 std::filesystem::directory_options::skip_permission_denied,
                 ec))
        {
            if (ec)
            {
                break;
            }

            if (entry.is_regular_file(ec))
            {
                entries.push_back(entry.path());
            }
        }

        Prewarm(entries, size);
    }

    void ThumbnailService::PruneToPaths(const std::vector<std::filesystem::path>& keepPaths)
    {
        std::unordered_set<std::string> keepKeys;
        keepKeys.reserve(keepPaths.size());
        for (const std::filesystem::path& keepPath : keepPaths)
        {
            const std::string key = NormalizePathKey(keepPath);
            if (!key.empty())
            {
                keepKeys.insert(key);
            }
        }

        std::vector<ThumbnailHandle> handlesToRemove;
        handlesToRemove.reserve(m_Entries.size());
        for (const auto& [handle, entry] : m_Entries)
        {
            if (!keepKeys.contains(entry.normalizedPathKey))
            {
                handlesToRemove.push_back(handle);
            }
        }

        for (const ThumbnailHandle handle : handlesToRemove)
        {
            RemoveHandle(handle);
        }
    }

    void ThumbnailService::SetBudgetMsPerFrame(const double milliseconds)
    {
        m_BudgetMsPerFrame = std::max(milliseconds, 0.0);
    }

    void ThumbnailService::SetConcurrency(const std::size_t concurrency)
    {
        const std::size_t sanitizedConcurrency = std::max<std::size_t>(1, concurrency);
        if (m_Concurrency == sanitizedConcurrency)
        {
            return;
        }

        m_Concurrency = sanitizedConcurrency;
        RestartMeshWorkerThreads();
    }

    void ThumbnailService::RegisterRenderer(std::unique_ptr<IThumbnailRenderer> renderer)
    {
        if (!renderer)
        {
            return;
        }

        m_Renderers.push_back(std::move(renderer));
        std::sort(
            m_Renderers.begin(),
            m_Renderers.end(),
            [](const std::unique_ptr<IThumbnailRenderer>& lhs, const std::unique_ptr<IThumbnailRenderer>& rhs)
            {
                return lhs->Priority() > rhs->Priority();
            });
    }

    void ThumbnailService::Tick(IRenderBackend& renderer)
    {
        m_LastRenderer = &renderer;
        const auto tickStartTime = std::chrono::steady_clock::now();
        DrainBackgroundMeshResults(renderer);
        if (m_PendingQueue.empty())
        {
            m_LastProcessedCount = 0;
            m_LastTickMs = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - tickStartTime)
                               .count();
            return;
        }

        const auto startTime = std::chrono::steady_clock::now();
        std::size_t processedCount = 0;
        const std::size_t maxPerTick = std::max<std::size_t>(1, m_Concurrency);
        while (!m_PendingQueue.empty())
        {
            if (processedCount >= maxPerTick)
            {
                break;
            }

            if (processedCount > 0 && m_BudgetMsPerFrame > 0.0)
            {
                const auto now = std::chrono::steady_clock::now();
                const double elapsedMs = std::chrono::duration<double, std::milli>(now - startTime).count();
                if (elapsedMs >= m_BudgetMsPerFrame)
                {
                    break;
                }
            }

            const ThumbnailHandle handle = m_PendingQueue.front();
            m_PendingQueue.pop_front();
            m_PendingSet.erase(handle);

            auto entryIt = m_Entries.find(handle);
            if (entryIt == m_Entries.end())
            {
                continue;
            }

            CacheEntry& entry = entryIt->second;
            entry.queued = false;
            if (entry.state != ThumbnailState::Pending && entry.state != ThumbnailState::Missing)
            {
                continue;
            }

            std::error_code ec;
            if (!std::filesystem::exists(entry.assetPath, ec) || !std::filesystem::is_regular_file(entry.assetPath, ec))
            {
                DestroyEntryTexture(entry, m_LastRenderer);
                entry.width = 0;
                entry.height = 0;
                entry.state = ThumbnailState::Failed;
                ++processedCount;
                continue;
            }

            entry.sourceStamp = ComputeSourceStamp(entry.assetPath);

            if (entry.assetClass == ThumbnailAssetClass::Mesh ||
                entry.assetClass == ThumbnailAssetClass::Image)
            {
                EnqueueBackgroundMeshJob(entry);
                ++processedCount;
                continue;
            }

            ThumbnailRenderContext context {};
            context.assetPath = entry.assetPath;
            context.assetClass = entry.assetClass;
            context.size = entry.size;

            ThumbnailRenderOutput output {};
            for (const std::unique_ptr<IThumbnailRenderer>& thumbnailRenderer : m_Renderers)
            {
                if (!thumbnailRenderer || !thumbnailRenderer->CanRender(context))
                {
                    continue;
                }

                output = thumbnailRenderer->Render(renderer, context);
                if (output.texture != nullptr)
                {
                    break;
                }
            }

            if (entry.texture != output.texture)
            {
                DestroyEntryTexture(entry, m_LastRenderer);
            }

            entry.texture = output.texture;
            entry.width = output.width;
            entry.height = output.height;
            entry.state = (entry.texture != nullptr) ? ThumbnailState::Ready : ThumbnailState::Failed;
            ++processedCount;
        }

        m_LastProcessedCount = processedCount;
        m_LastTickMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - tickStartTime)
                           .count();
    }

    void ThumbnailService::Shutdown(IRenderBackend* renderer)
    {
        StopMeshWorkerThreads();
        IRenderBackend* backend = renderer != nullptr ? renderer : m_LastRenderer;
        if (backend != nullptr)
        {
            for (auto& [handle, entry] : m_Entries)
            {
                (void)handle;
                if (entry.texture != nullptr)
                {
                    backend->DestroyImGuiTexture(entry.texture);
                    entry.texture = nullptr;
                }
            }
        }

        m_Entries.clear();
        m_PathSizeToHandle.clear();
        m_AssetToHandles.clear();
        m_PendingQueue.clear();
        m_PendingSet.clear();
        m_LastRenderer = nullptr;
        m_LastProcessedCount = 0;
        m_LastUploadCount = 0;
        m_LastTickMs = 0.0;
        m_LastUploadMs = 0.0;
    }

    ThumbnailServiceStats ThumbnailService::GetStats() const
    {
        ThumbnailServiceStats stats {};
        stats.cacheEntryCount = m_Entries.size();
        stats.pendingQueueDepth = m_PendingQueue.size();
        stats.lastProcessedCount = m_LastProcessedCount;
        stats.lastUploadCount = m_LastUploadCount;
        stats.maxUploadsPerTick = m_MaxUploadsPerTick;
        stats.lastTickMs = m_LastTickMs;
        stats.lastUploadMs = m_LastUploadMs;

        for (const auto& [handle, entry] : m_Entries)
        {
            (void)handle;
            switch (entry.state)
            {
            case ThumbnailState::Ready:
                ++stats.readyCount;
                break;
            case ThumbnailState::Pending:
            case ThumbnailState::Missing:
                ++stats.pendingCount;
                break;
            case ThumbnailState::Failed:
                ++stats.failedCount;
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(m_BackgroundMeshMutex);
            stats.backgroundJobQueueDepth = m_BackgroundMeshJobs.size();
            stats.backgroundResultQueueDepth = m_BackgroundMeshResults.size();
        }

        return stats;
    }

    ThumbnailSize ThumbnailService::SanitizeSize(ThumbnailSize size) const
    {
        size.width = std::clamp(size.width, 32, 1024);
        size.height = std::clamp(size.height, 32, 1024);
        if (size.width <= 0)
        {
            size.width = kDefaultThumbnailSize;
        }
        if (size.height <= 0)
        {
            size.height = kDefaultThumbnailSize;
        }
        return size;
    }

    ThumbnailHandle ThumbnailService::EnsureRequest(
        const Assets::AssetID assetId,
        const std::filesystem::path& assetPath,
        const ThumbnailSize& size,
        const ThumbnailRequestOptions& options)
    {
        if (assetPath.empty())
        {
            return 0;
        }

        const std::filesystem::path normalizedPath = assetPath.lexically_normal();
        const ThumbnailAssetClass assetClass = DetectAssetClass(normalizedPath);
        if (assetClass == ThumbnailAssetClass::Unknown)
        {
            return 0;
        }

        const std::string pathSizeKey = BuildPathSizeKey(normalizedPath, size);

        const auto existingHandleIt = m_PathSizeToHandle.find(pathSizeKey);
        if (existingHandleIt != m_PathSizeToHandle.end())
        {
            auto entryIt = m_Entries.find(existingHandleIt->second);
            if (entryIt != m_Entries.end())
            {
                CacheEntry& entry = entryIt->second;
                if (assetId != 0 && entry.assetId == 0)
                {
                    entry.assetId = assetId;
                    m_AssetToHandles[assetId].push_back(entry.handle);
                }

                const bool shouldRegenerate =
                    options.forceRegenerate ||
                    entry.state == ThumbnailState::Missing;
                if (shouldRegenerate)
                {
                    if (options.forceRegenerate)
                    {
                        DestroyEntryTexture(entry, m_LastRenderer);
                        entry.sourceStamp = ComputeSourceStamp(entry.assetPath);
                    }
                    entry.state = ThumbnailState::Pending;
                    QueueHandle(entry.handle, options.highPriority);
                }
                return entry.handle;
            }
        }

        CacheEntry entry {};
        entry.handle = m_NextHandle++;
        entry.assetId = assetId;
        entry.assetPath = normalizedPath;
        entry.normalizedPathKey = NormalizePathKey(normalizedPath);
        entry.assetClass = assetClass;
        entry.size = size;
        entry.sourceStamp = ComputeSourceStamp(normalizedPath);
        entry.state = ThumbnailState::Pending;

        const ThumbnailHandle handle = entry.handle;
        m_Entries.emplace(handle, entry);
        m_PathSizeToHandle[pathSizeKey] = handle;
        if (assetId != 0)
        {
            m_AssetToHandles[assetId].push_back(handle);
        }

        QueueHandle(handle, options.highPriority);
        return handle;
    }

    ThumbnailAssetClass ThumbnailService::DetectAssetClass(const std::filesystem::path& assetPath) const
    {
        if (IsImageThumbnailSource(assetPath))
        {
            return ThumbnailAssetClass::Image;
        }
        if (IsMeshThumbnailSource(assetPath))
        {
            return ThumbnailAssetClass::Mesh;
        }
        return ThumbnailAssetClass::Unknown;
    }

    std::string ThumbnailService::NormalizePathKey(const std::filesystem::path& path) const
    {
        std::string key = path.lexically_normal().generic_string();
#if defined(_WIN32)
        key = ToLowerString(key);
#endif
        return key;
    }

    std::string ThumbnailService::BuildPathSizeKey(const std::filesystem::path& path, const ThumbnailSize& size) const
    {
        std::ostringstream stream;
        stream << NormalizePathKey(path) << "|w=" << size.width << "|h=" << size.height;
        return stream.str();
    }

    std::uint64_t ThumbnailService::ComputeSourceStamp(const std::filesystem::path& path) const
    {
        std::error_code ec;
        std::uint64_t stamp = 0;
        const auto writeTime = std::filesystem::last_write_time(path, ec);
        if (!ec)
        {
            stamp = CombineContentStamp(stamp, static_cast<std::uint64_t>(writeTime.time_since_epoch().count()));
        }

        const std::uintmax_t fileSize = std::filesystem::file_size(path, ec);
        if (!ec)
        {
            stamp = CombineContentStamp(stamp, static_cast<std::uint64_t>(fileSize));
        }

        return stamp;
    }

    void ThumbnailService::QueueHandle(const ThumbnailHandle handle, const bool highPriority)
    {
        auto entryIt = m_Entries.find(handle);
        if (entryIt == m_Entries.end())
        {
            return;
        }

        CacheEntry& entry = entryIt->second;
        if (entry.queued)
        {
            return;
        }
        if (!m_PendingSet.insert(handle).second)
        {
            return;
        }

        entry.queued = true;
        if (highPriority)
        {
            m_PendingQueue.push_front(handle);
        }
        else
        {
            m_PendingQueue.push_back(handle);
        }
    }

    void ThumbnailService::RemoveHandle(const ThumbnailHandle handle)
    {
        auto entryIt = m_Entries.find(handle);
        if (entryIt == m_Entries.end())
        {
            return;
        }

        CacheEntry& entry = entryIt->second;
        DestroyEntryTexture(entry, m_LastRenderer);

        const std::string pathSizeKey = BuildPathSizeKey(entry.assetPath, entry.size);
        m_PathSizeToHandle.erase(pathSizeKey);

        if (entry.assetId != 0)
        {
            auto assetIt = m_AssetToHandles.find(entry.assetId);
            if (assetIt != m_AssetToHandles.end())
            {
                auto& handles = assetIt->second;
                handles.erase(
                    std::remove(handles.begin(), handles.end(), handle),
                    handles.end());
                if (handles.empty())
                {
                    m_AssetToHandles.erase(assetIt);
                }
            }
        }

        m_PendingSet.erase(handle);
        m_PendingQueue.erase(
            std::remove(m_PendingQueue.begin(), m_PendingQueue.end(), handle),
            m_PendingQueue.end());
        m_Entries.erase(entryIt);
    }

    void ThumbnailService::DestroyEntryTexture(CacheEntry& entry, IRenderBackend* renderer)
    {
        if (entry.texture != nullptr && renderer != nullptr)
        {
            renderer->DestroyImGuiTexture(entry.texture);
        }
        entry.texture = nullptr;
    }

    void ThumbnailService::RegisterBuiltInRenderers()
    {
        RegisterRenderer(std::make_unique<ImageThumbnailRenderer>());
        RegisterRenderer(std::make_unique<MeshThumbnailRenderer>());
    }

    void ThumbnailService::StartMeshWorkerThreads()
    {
        StopMeshWorkerThreads();

        m_StopBackgroundMeshWorkers = false;
        const std::size_t workerCount = std::max<std::size_t>(1, m_Concurrency);
        m_MeshWorkerThreads.reserve(workerCount);
        for (std::size_t i = 0; i < workerCount; ++i)
        {
            m_MeshWorkerThreads.emplace_back(
                [this]()
                {
                    MeshWorkerLoop();
                });
        }
    }

    void ThumbnailService::StopMeshWorkerThreads()
    {
        {
            std::lock_guard<std::mutex> lock(m_BackgroundMeshMutex);
            m_StopBackgroundMeshWorkers = true;
        }
        m_BackgroundMeshCv.notify_all();

        for (std::thread& worker : m_MeshWorkerThreads)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        m_MeshWorkerThreads.clear();

        std::lock_guard<std::mutex> lock(m_BackgroundMeshMutex);
        m_BackgroundMeshJobs.clear();
        m_BackgroundMeshResults.clear();
        m_StopBackgroundMeshWorkers = false;
    }

    void ThumbnailService::RestartMeshWorkerThreads()
    {
        StopMeshWorkerThreads();
        StartMeshWorkerThreads();
    }

    void ThumbnailService::MeshWorkerLoop()
    {
        for (;;)
        {
            BackgroundThumbnailJob job {};
            {
                std::unique_lock<std::mutex> lock(m_BackgroundMeshMutex);
                m_BackgroundMeshCv.wait(
                    lock,
                    [this]()
                    {
                        return m_StopBackgroundMeshWorkers || !m_BackgroundMeshJobs.empty();
                    });

                if (m_StopBackgroundMeshWorkers && m_BackgroundMeshJobs.empty())
                {
                    return;
                }

                job = std::move(m_BackgroundMeshJobs.front());
                m_BackgroundMeshJobs.pop_front();
            }

            BackgroundThumbnailResult result {};
            result.handle = job.handle;
            result.sourceStamp = job.sourceStamp;
            if (job.assetClass == ThumbnailAssetClass::Mesh)
            {
                PreparedThumbnailPixels prepared = PrepareMeshThumbnailPixels(job.assetPath, job.size);
                result.width = prepared.width;
                result.height = prepared.height;
                result.succeeded = prepared.succeeded;
                if (prepared.succeeded)
                {
                    result.rgbaPixels = std::move(prepared.rgbaPixels);
                }
            }
            else if (job.assetClass == ThumbnailAssetClass::Image)
            {
                PreparedThumbnailPixels prepared = PrepareImageThumbnailPixels(job.assetPath);
                result.width = prepared.width;
                result.height = prepared.height;
                result.succeeded = prepared.succeeded;
                if (prepared.succeeded)
                {
                    result.rgbaPixels = std::move(prepared.rgbaPixels);
                }
            }

            std::lock_guard<std::mutex> lock(m_BackgroundMeshMutex);
            m_BackgroundMeshResults.push_back(std::move(result));
        }
    }

    void ThumbnailService::EnqueueBackgroundMeshJob(const CacheEntry& entry)
    {
        auto entryIt = m_Entries.find(entry.handle);
        if (entryIt == m_Entries.end())
        {
            return;
        }

        CacheEntry& mutableEntry = entryIt->second;
        if (mutableEntry.backgroundJobQueued)
        {
            return;
        }

        mutableEntry.backgroundJobQueued = true;
        mutableEntry.backgroundJobSourceStamp = mutableEntry.sourceStamp;

        BackgroundThumbnailJob job {};
        job.handle = mutableEntry.handle;
        job.assetPath = mutableEntry.assetPath;
        job.assetClass = mutableEntry.assetClass;
        job.size = mutableEntry.size;
        job.sourceStamp = mutableEntry.sourceStamp;

        {
            std::lock_guard<std::mutex> lock(m_BackgroundMeshMutex);
            m_BackgroundMeshJobs.push_back(std::move(job));
        }
        m_BackgroundMeshCv.notify_one();
    }

    void ThumbnailService::DrainBackgroundMeshResults(IRenderBackend& renderer)
    {
        const auto uploadStartTime = std::chrono::steady_clock::now();
        std::deque<BackgroundThumbnailResult> readyResults;
        {
            std::lock_guard<std::mutex> lock(m_BackgroundMeshMutex);
            if (m_BackgroundMeshResults.empty())
            {
                m_LastUploadCount = 0;
                m_LastUploadMs = 0.0;
                return;
            }

            readyResults.swap(m_BackgroundMeshResults);
        }

        std::size_t uploadedCount = 0;
        std::deque<BackgroundThumbnailResult> deferredResults;
        for (BackgroundThumbnailResult& result : readyResults)
        {
            if (uploadedCount >= m_MaxUploadsPerTick)
            {
                deferredResults.push_back(std::move(result));
                continue;
            }

            auto entryIt = m_Entries.find(result.handle);
            if (entryIt == m_Entries.end())
            {
                continue;
            }

            CacheEntry& entry = entryIt->second;
            if (entry.backgroundJobSourceStamp != result.sourceStamp)
            {
                continue;
            }

            entry.backgroundJobQueued = false;
            if (!result.succeeded)
            {
                DestroyEntryTexture(entry, m_LastRenderer);
                entry.texture = nullptr;
                entry.width = 0;
                entry.height = 0;
                entry.state = ThumbnailState::Failed;
                continue;
            }

            void* newTexture = CreateThumbnailTexture(renderer, result.rgbaPixels, result.width, result.height);
            if (entry.texture != newTexture)
            {
                DestroyEntryTexture(entry, m_LastRenderer);
            }

            entry.texture = newTexture;
            entry.width = result.width;
            entry.height = result.height;
            entry.state = (entry.texture != nullptr) ? ThumbnailState::Ready : ThumbnailState::Failed;
            ++uploadedCount;
        }

        if (!deferredResults.empty())
        {
            std::lock_guard<std::mutex> lock(m_BackgroundMeshMutex);
            while (!deferredResults.empty())
            {
                m_BackgroundMeshResults.push_front(std::move(deferredResults.back()));
                deferredResults.pop_back();
            }
        }

        m_LastUploadCount = uploadedCount;
        m_LastUploadMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - uploadStartTime)
                             .count();
        AdaptUploadBudget(readyResults.size() > uploadedCount ? readyResults.size() - uploadedCount : 0);
    }

    void ThumbnailService::AdaptUploadBudget(const std::size_t deferredCount)
    {
        constexpr std::size_t kMinUploadsPerTick = 1;
        constexpr std::size_t kMaxUploadsPerTick = 4;
        constexpr double kHighUploadMs = 0.90;
        constexpr double kLowUploadMs = 0.25;

        if (m_LastUploadMs >= kHighUploadMs && m_MaxUploadsPerTick > kMinUploadsPerTick)
        {
            --m_MaxUploadsPerTick;
            return;
        }

        if (deferredCount > 0 &&
            m_LastUploadMs <= kLowUploadMs &&
            m_MaxUploadsPerTick < kMaxUploadsPerTick)
        {
            ++m_MaxUploadsPerTick;
        }
    }
}
