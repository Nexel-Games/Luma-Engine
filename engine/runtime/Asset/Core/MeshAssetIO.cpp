#include "Luma/Asset/Core/MeshAssetIO.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "Luma/Core/App/Project.h"

namespace Luma::Assets
{
    namespace
    {
        constexpr std::array<char, 8> kCookedMeshMagic { 'L', 'M', 'S', 'H', 'C', 'K', '0', '1' };
        constexpr std::uint32_t kCookedMeshVersion = 4;
        constexpr std::array<char, 8> kMeshChunkMagic { 'L', 'M', 'S', 'H', 'S', 'C', '0', '1' };
        constexpr std::uint32_t kMeshChunkVersion = 1;
        constexpr std::size_t kMaxTrianglesPerSection = 2048;

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

        bool ReadFileBytes(const std::filesystem::path& filePath, std::vector<std::uint8_t>& outBytes, std::string& outError)
        {
            outBytes.clear();
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file)
            {
                outError = "Failed to open mesh file: " + filePath.string();
                return false;
            }

            const std::streamsize fileSize = file.tellg();
            if (fileSize <= 0)
            {
                outError = "Mesh file is empty: " + filePath.string();
                return false;
            }
            file.seekg(0, std::ios::beg);

            outBytes.resize(static_cast<std::size_t>(fileSize));
            file.read(reinterpret_cast<char*>(outBytes.data()), fileSize);
            if (!file.good() && !file.eof())
            {
                outError = "Failed to read mesh file: " + filePath.string();
                return false;
            }
            return true;
        }

        template <typename T>
        void AppendPod(std::vector<std::uint8_t>& outBytes, const T& value)
        {
            const std::size_t start = outBytes.size();
            outBytes.resize(start + sizeof(T));
            std::memcpy(outBytes.data() + start, &value, sizeof(T));
        }

        template <typename T>
        bool ReadPod(const std::uint8_t*& cursor, std::size_t& remainingBytes, T& outValue)
        {
            if (remainingBytes < sizeof(T))
            {
                return false;
            }
            std::memcpy(&outValue, cursor, sizeof(T));
            cursor += sizeof(T);
            remainingBytes -= sizeof(T);
            return true;
        }

        struct IntermediateSourceData
        {
            const std::uint8_t* sourceBytes = nullptr;
            std::size_t sourceSize = 0;
            std::string sourceName;
        };

        void BuildMeshSections(const PrimitiveMeshData& meshData, std::vector<MeshAssetSection>& outSections);
        PrimitiveMeshData MergeMeshSections(const std::vector<MeshAssetSection>& sections);
        void UpdateMaterialSlotNames(const std::vector<MeshAssetSection>& sections, std::vector<std::string>& outSlotNames);
        void BuildSectionsFromSceneParts(const std::vector<MeshScenePart>& sceneParts, std::vector<MeshAssetSection>& outSections);
        bool LoadExternalSectionChunks(
            const std::filesystem::path& logicalPath,
            MeshAssetData& outData,
            std::string& outError);
        void GatherAssimpNodeSceneParts(
            const aiScene& scene,
            const aiNode& node,
            const aiMatrix4x4& parentTransform,
            const std::filesystem::path& sourcePath,
            std::vector<MeshScenePart>& outParts);

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

        bool TryLoadCookedMeshPayload(
            const std::uint8_t* payloadBytes,
            const std::size_t payloadSize,
            const std::filesystem::path& logicalPath,
            const bool loadExternalChunks,
            MeshAssetData& outData,
            std::string& outError)
        {
            outData = {};
            if (payloadBytes == nullptr || payloadSize < kCookedMeshMagic.size())
            {
                return false;
            }
            if (!std::equal(kCookedMeshMagic.begin(), kCookedMeshMagic.end(), reinterpret_cast<const char*>(payloadBytes)))
            {
                return false;
            }

            const std::uint8_t* cursor = payloadBytes + kCookedMeshMagic.size();
            std::size_t remainingBytes = payloadSize - kCookedMeshMagic.size();

            std::uint32_t version = 0;
            if (!ReadPod(cursor, remainingBytes, version))
            {
                outError = "Cooked mesh header is truncated.";
                return false;
            }

            if (version == 1)
            {
                std::uint32_t vertexCount = 0;
                std::uint32_t indexCount = 0;
                std::uint32_t sourceNameLength = 0;
                if (!ReadPod(cursor, remainingBytes, vertexCount) ||
                    !ReadPod(cursor, remainingBytes, indexCount) ||
                    !ReadPod(cursor, remainingBytes, sourceNameLength))
                {
                    outError = "Cooked mesh header is truncated.";
                    return false;
                }

                for (int axis = 0; axis < 3; ++axis)
                {
                    if (!ReadPod(cursor, remainingBytes, outData.boundsMin[axis]))
                    {
                        outError = "Cooked mesh bounds are truncated.";
                        return false;
                    }
                }
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (!ReadPod(cursor, remainingBytes, outData.boundsMax[axis]))
                    {
                        outError = "Cooked mesh bounds are truncated.";
                        return false;
                    }
                }

                if (remainingBytes < sourceNameLength)
                {
                    outError = "Cooked mesh source metadata is truncated.";
                    return false;
                }
                outData.sourceName.assign(reinterpret_cast<const char*>(cursor), sourceNameLength);
                cursor += sourceNameLength;
                remainingBytes -= sourceNameLength;

                const std::size_t vertexBytes = static_cast<std::size_t>(vertexCount) * sizeof(PrimitiveVertex);
                const std::size_t indexBytes = static_cast<std::size_t>(indexCount) * sizeof(std::uint32_t);
                if (remainingBytes < vertexBytes + indexBytes)
                {
                    outError = "Cooked mesh payload is truncated.";
                    return false;
                }

                outData.mesh.vertices.resize(vertexCount);
                outData.mesh.indices.resize(indexCount);
                if (vertexBytes > 0)
                {
                    std::memcpy(outData.mesh.vertices.data(), cursor, vertexBytes);
                    cursor += vertexBytes;
                    remainingBytes -= vertexBytes;
                }
                if (indexBytes > 0)
                {
                    std::memcpy(outData.mesh.indices.data(), cursor, indexBytes);
                    remainingBytes -= indexBytes;
                }

                outData.cooked = true;
                BuildMeshSections(outData.mesh, outData.sections);
                UpdateMaterialSlotNames(outData.sections, outData.materialSlotNames);
                return !outData.mesh.vertices.empty() && !outData.mesh.indices.empty();
            }

            if (version == 2)
            {
                std::uint32_t sourceNameLength = 0;
                std::uint32_t sectionCount = 0;
                if (!ReadPod(cursor, remainingBytes, sourceNameLength) ||
                    !ReadPod(cursor, remainingBytes, sectionCount))
                {
                    outError = "Cooked mesh header is truncated.";
                    return false;
                }

                for (int axis = 0; axis < 3; ++axis)
                {
                    if (!ReadPod(cursor, remainingBytes, outData.boundsMin[axis]))
                    {
                        outError = "Cooked mesh bounds are truncated.";
                        return false;
                    }
                }
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (!ReadPod(cursor, remainingBytes, outData.boundsMax[axis]))
                    {
                        outError = "Cooked mesh bounds are truncated.";
                        return false;
                    }
                }

                if (remainingBytes < sourceNameLength)
                {
                    outError = "Cooked mesh source metadata is truncated.";
                    return false;
                }
                outData.sourceName.assign(reinterpret_cast<const char*>(cursor), sourceNameLength);
                cursor += sourceNameLength;
                remainingBytes -= sourceNameLength;

                outData.sections.clear();
                outData.sections.reserve(sectionCount);
                for (std::uint32_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
                {
                    MeshAssetSection section;
                    std::uint32_t vertexCount = 0;
                    std::uint32_t indexCount = 0;
                    if (!ReadPod(cursor, remainingBytes, vertexCount) ||
                        !ReadPod(cursor, remainingBytes, indexCount))
                    {
                        outError = "Cooked mesh section header is truncated.";
                        return false;
                    }

                    for (int axis = 0; axis < 3; ++axis)
                    {
                        if (!ReadPod(cursor, remainingBytes, section.boundsMin[axis]))
                        {
                            outError = "Cooked mesh section bounds are truncated.";
                            return false;
                        }
                    }
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        if (!ReadPod(cursor, remainingBytes, section.boundsMax[axis]))
                        {
                            outError = "Cooked mesh section bounds are truncated.";
                            return false;
                        }
                    }

                    const std::size_t vertexBytes = static_cast<std::size_t>(vertexCount) * sizeof(PrimitiveVertex);
                    const std::size_t indexBytes = static_cast<std::size_t>(indexCount) * sizeof(std::uint32_t);
                    if (remainingBytes < vertexBytes + indexBytes)
                    {
                        outError = "Cooked mesh section payload is truncated.";
                        return false;
                    }

                    section.mesh.vertices.resize(vertexCount);
                    section.mesh.indices.resize(indexCount);
                    if (vertexBytes > 0)
                    {
                        std::memcpy(section.mesh.vertices.data(), cursor, vertexBytes);
                        cursor += vertexBytes;
                        remainingBytes -= vertexBytes;
                    }
                    if (indexBytes > 0)
                    {
                        std::memcpy(section.mesh.indices.data(), cursor, indexBytes);
                        cursor += indexBytes;
                        remainingBytes -= indexBytes;
                    }

                    section.materialSlotIndex = 0;
                    outData.sections.push_back(std::move(section));
                }

                if (outData.sections.empty())
                {
                    outError = "Cooked mesh payload does not contain any sections.";
                    return false;
                }

                outData.mesh = MergeMeshSections(outData.sections);
                UpdateMaterialSlotNames(outData.sections, outData.materialSlotNames);
                outData.cooked = true;
                return !outData.mesh.vertices.empty() && !outData.mesh.indices.empty();
            }

            if (version != 3 && version != kCookedMeshVersion)
            {
                outError = "Unsupported cooked mesh version.";
                return false;
            }

            std::uint32_t sourceNameLength = 0;
            std::uint32_t sectionCount = 0;
            if (!ReadPod(cursor, remainingBytes, sourceNameLength) ||
                !ReadPod(cursor, remainingBytes, sectionCount))
            {
                outError = "Cooked mesh header is truncated.";
                return false;
            }

            for (int axis = 0; axis < 3; ++axis)
            {
                if (!ReadPod(cursor, remainingBytes, outData.boundsMin[axis]))
                {
                    outError = "Cooked mesh bounds are truncated.";
                    return false;
                }
            }
            for (int axis = 0; axis < 3; ++axis)
            {
                if (!ReadPod(cursor, remainingBytes, outData.boundsMax[axis]))
                {
                    outError = "Cooked mesh bounds are truncated.";
                    return false;
                }
            }

            if (remainingBytes < sourceNameLength)
            {
                outError = "Cooked mesh source metadata is truncated.";
                return false;
            }
            outData.sourceName.assign(reinterpret_cast<const char*>(cursor), sourceNameLength);
            cursor += sourceNameLength;
            remainingBytes -= sourceNameLength;

            outData.sections.clear();
            outData.sections.reserve(sectionCount);
            for (std::uint32_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
            {
                MeshAssetSection section;
                std::uint32_t chunkPathLength = 0;
                std::uint32_t materialSlotNameLength = 0;
                if (!ReadPod(cursor, remainingBytes, chunkPathLength))
                {
                    outError = "Cooked mesh section header is truncated.";
                    return false;
                }
                if (version >= 4 && !ReadPod(cursor, remainingBytes, section.materialSlotIndex))
                {
                    outError = "Cooked mesh material slot header is truncated.";
                    return false;
                }
                if (version >= 4 && !ReadPod(cursor, remainingBytes, materialSlotNameLength))
                {
                    outError = "Cooked mesh material slot metadata is truncated.";
                    return false;
                }
                if (version < 4)
                {
                    section.materialSlotIndex = 0;
                }

                for (int axis = 0; axis < 3; ++axis)
                {
                    if (!ReadPod(cursor, remainingBytes, section.boundsMin[axis]))
                    {
                        outError = "Cooked mesh section bounds are truncated.";
                        return false;
                    }
                }
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (!ReadPod(cursor, remainingBytes, section.boundsMax[axis]))
                    {
                        outError = "Cooked mesh section bounds are truncated.";
                        return false;
                    }
                }

                if (remainingBytes < chunkPathLength)
                {
                    outError = "Cooked mesh section path is truncated.";
                    return false;
                }
                section.chunkPath = std::filesystem::path(
                    std::string(reinterpret_cast<const char*>(cursor), chunkPathLength));
                cursor += chunkPathLength;
                remainingBytes -= chunkPathLength;

                if (version >= 4)
                {
                    if (remainingBytes < materialSlotNameLength)
                    {
                        outError = "Cooked mesh material slot name is truncated.";
                        return false;
                    }
                    section.materialSlotName.assign(
                        reinterpret_cast<const char*>(cursor),
                        materialSlotNameLength);
                    cursor += materialSlotNameLength;
                    remainingBytes -= materialSlotNameLength;
                }

                outData.sections.push_back(std::move(section));
            }

            if (outData.sections.empty())
            {
                outError = "Cooked mesh payload does not contain any sections.";
                return false;
            }

            outData.cooked = true;
            UpdateMaterialSlotNames(outData.sections, outData.materialSlotNames);
            if (!loadExternalChunks)
            {
                outData.mesh = {};
                return true;
            }
            return LoadExternalSectionChunks(logicalPath, outData, outError);
        }

        void UpdateBounds(
            const aiVector3D& position,
            std::array<float, 3>& boundsMin,
            std::array<float, 3>& boundsMax)
        {
            boundsMin[0] = std::min(boundsMin[0], position.x);
            boundsMin[1] = std::min(boundsMin[1], position.y);
            boundsMin[2] = std::min(boundsMin[2], position.z);
            boundsMax[0] = std::max(boundsMax[0], position.x);
            boundsMax[1] = std::max(boundsMax[1], position.y);
            boundsMax[2] = std::max(boundsMax[2], position.z);
        }

        void UpdateBounds(
            const PrimitiveVertex& vertex,
            std::array<float, 3>& boundsMin,
            std::array<float, 3>& boundsMax)
        {
            boundsMin[0] = std::min(boundsMin[0], vertex.position[0]);
            boundsMin[1] = std::min(boundsMin[1], vertex.position[1]);
            boundsMin[2] = std::min(boundsMin[2], vertex.position[2]);
            boundsMax[0] = std::max(boundsMax[0], vertex.position[0]);
            boundsMax[1] = std::max(boundsMax[1], vertex.position[1]);
            boundsMax[2] = std::max(boundsMax[2], vertex.position[2]);
        }

        void BuildSectionBounds(
            const PrimitiveMeshData& mesh,
            std::array<float, 3>& outBoundsMin,
            std::array<float, 3>& outBoundsMax)
        {
            outBoundsMin = {
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()
            };
            outBoundsMax = {
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()
            };

            for (const PrimitiveVertex& vertex : mesh.vertices)
            {
                UpdateBounds(vertex, outBoundsMin, outBoundsMax);
            }
        }

        PrimitiveMeshData MergeMeshSections(const std::vector<MeshAssetSection>& sections)
        {
            PrimitiveMeshData merged;
            std::size_t totalVertices = 0;
            std::size_t totalIndices = 0;
            for (const MeshAssetSection& section : sections)
            {
                totalVertices += section.mesh.vertices.size();
                totalIndices += section.mesh.indices.size();
            }

            merged.vertices.reserve(totalVertices);
            merged.indices.reserve(totalIndices);
            std::uint32_t baseVertex = 0;
            for (const MeshAssetSection& section : sections)
            {
                merged.vertices.insert(
                    merged.vertices.end(),
                    section.mesh.vertices.begin(),
                    section.mesh.vertices.end());
                for (const std::uint32_t index : section.mesh.indices)
                {
                    merged.indices.push_back(baseVertex + index);
                }
                baseVertex += static_cast<std::uint32_t>(section.mesh.vertices.size());
            }
            return merged;
        }

        void UpdateMaterialSlotNames(const std::vector<MeshAssetSection>& sections, std::vector<std::string>& outSlotNames)
        {
            outSlotNames.clear();
            for (const MeshAssetSection& section : sections)
            {
                const std::size_t slotIndex = static_cast<std::size_t>(section.materialSlotIndex);
                if (outSlotNames.size() <= slotIndex)
                {
                    outSlotNames.resize(slotIndex + 1);
                }
                if (!section.materialSlotName.empty())
                {
                    outSlotNames[slotIndex] = section.materialSlotName;
                }
            }
        }

        void BuildSectionsFromSceneParts(const std::vector<MeshScenePart>& sceneParts, std::vector<MeshAssetSection>& outSections)
        {
            outSections.clear();
            outSections.reserve(sceneParts.size());
            for (std::size_t partIndex = 0; partIndex < sceneParts.size(); ++partIndex)
            {
                const MeshScenePart& part = sceneParts[partIndex];
                MeshAssetSection section;
                section.mesh = part.mesh;
                section.boundsMin = part.boundsMin;
                section.boundsMax = part.boundsMax;
                section.materialSlotIndex = static_cast<std::uint32_t>(partIndex);
                section.materialSlotName =
                    !part.material.name.empty()
                        ? part.material.name
                        : (!part.name.empty() ? part.name : ("Element " + std::to_string(partIndex)));
                outSections.push_back(std::move(section));
            }
        }

        std::filesystem::path ResolveAssimpTexturePath(
            const std::filesystem::path& sourcePath,
            const aiMaterial* material,
            const aiTextureType textureType)
        {
            if (material == nullptr)
            {
                return {};
            }

            aiString texturePath;
            if (material->GetTexture(textureType, 0, &texturePath) != aiReturn_SUCCESS)
            {
                return {};
            }

            std::string value = texturePath.C_Str();
            if (value.empty())
            {
                return {};
            }

            std::replace(value.begin(), value.end(), '\\', '/');
            std::filesystem::path resolved(value);
            if (resolved.is_relative())
            {
                resolved = (sourcePath.parent_path() / resolved).lexically_normal();
            }
            return resolved.lexically_normal();
        }

        std::filesystem::path ResolveFirstAssimpTexturePath(
            const std::filesystem::path& sourcePath,
            const aiMaterial* material,
            const std::initializer_list<aiTextureType> textureTypes)
        {
            for (const aiTextureType textureType : textureTypes)
            {
                const std::filesystem::path resolved = ResolveAssimpTexturePath(sourcePath, material, textureType);
                if (!resolved.empty())
                {
                    return resolved;
                }
            }

            return {};
        }

        template <typename T>
        bool TryGetMaterialScalar(
            const aiMaterial* material,
            const char* key,
            const unsigned int type,
            const unsigned int index,
            T& outValue)
        {
            if (material == nullptr)
            {
                return false;
            }

            return material->Get(key, type, index, outValue) == aiReturn_SUCCESS;
        }

        bool TryGetMaterialColor3(
            const aiMaterial* material,
            const char* key,
            const unsigned int type,
            const unsigned int index,
            std::array<float, 3>& outColor)
        {
            if (material == nullptr)
            {
                return false;
            }

            aiColor3D value {};
            if (material->Get(key, type, index, value) != aiReturn_SUCCESS)
            {
                return false;
            }

            outColor = {
                std::max(0.0f, value.r),
                std::max(0.0f, value.g),
                std::max(0.0f, value.b)
            };
            return true;
        }

        MeshMaterialInfo ExtractAssimpMaterialInfo(const std::filesystem::path& sourcePath, const aiMaterial* material)
        {
            MeshMaterialInfo info;
            if (material == nullptr)
            {
                return info;
            }

            info.name = material->GetName().C_Str();
            info.albedoTexture = ResolveFirstAssimpTexturePath(
                sourcePath,
                material,
                { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE });
            info.normalTexture = ResolveAssimpTexturePath(sourcePath, material, aiTextureType_NORMALS);
            info.heightTexture = ResolveAssimpTexturePath(sourcePath, material, aiTextureType_HEIGHT);
            info.emissiveTexture = ResolveAssimpTexturePath(sourcePath, material, aiTextureType_EMISSIVE);
            info.opacityTexture = ResolveAssimpTexturePath(sourcePath, material, aiTextureType_OPACITY);
            info.ambientOcclusionTexture = ResolveAssimpTexturePath(sourcePath, material, aiTextureType_AMBIENT_OCCLUSION);

            const std::filesystem::path roughnessTexture =
                ResolveAssimpTexturePath(sourcePath, material, aiTextureType_DIFFUSE_ROUGHNESS);
            const std::filesystem::path metallicTexture =
                ResolveAssimpTexturePath(sourcePath, material, aiTextureType_METALNESS);
            if (!roughnessTexture.empty() && roughnessTexture == metallicTexture)
            {
                info.ormTexture = roughnessTexture;
            }
            else
            {
                info.roughnessTexture = roughnessTexture;
                info.metallicTexture = metallicTexture;
            }

            std::array<float, 3> baseColor3 {};
            if (TryGetMaterialColor3(material, AI_MATKEY_BASE_COLOR, baseColor3))
            {
                info.baseColorTint = { baseColor3[0], baseColor3[1], baseColor3[2], 1.0f };
            }
            else if (TryGetMaterialColor3(material, AI_MATKEY_COLOR_DIFFUSE, baseColor3))
            {
                info.baseColorTint = { baseColor3[0], baseColor3[1], baseColor3[2], 1.0f };
            }

            std::array<float, 3> emissiveColor3 {};
            if (TryGetMaterialColor3(material, AI_MATKEY_COLOR_EMISSIVE, emissiveColor3))
            {
                info.emissiveColor = { emissiveColor3[0], emissiveColor3[1], emissiveColor3[2], 1.0f };
                info.emissiveIntensity = std::max({ emissiveColor3[0], emissiveColor3[1], emissiveColor3[2], 0.0f });
            }

            float emissiveIntensity = 0.0f;
            if (TryGetMaterialScalar(material, AI_MATKEY_EMISSIVE_INTENSITY, emissiveIntensity))
            {
                info.emissiveIntensity = std::max(info.emissiveIntensity, emissiveIntensity);
            }

            float metallic = 0.0f;
            if (TryGetMaterialScalar(material, AI_MATKEY_METALLIC_FACTOR, metallic))
            {
                info.metallic = std::clamp(metallic, 0.0f, 1.0f);
            }

            float roughness = 0.0f;
            if (TryGetMaterialScalar(material, AI_MATKEY_ROUGHNESS_FACTOR, roughness))
            {
                info.roughness = std::clamp(roughness, 0.04f, 1.0f);
            }
            else
            {
                float shininess = 0.0f;
                if (TryGetMaterialScalar(material, AI_MATKEY_SHININESS, shininess))
                {
                    const float clampedShininess = std::clamp(shininess, 0.0f, 1024.0f);
                    info.roughness = std::clamp(std::sqrt(2.0f / (clampedShininess + 2.0f)), 0.04f, 1.0f);
                }
            }

            std::array<float, 3> specularColor3 {};
            if (TryGetMaterialColor3(material, AI_MATKEY_COLOR_SPECULAR, specularColor3))
            {
                info.specular = std::clamp(
                    (specularColor3[0] + specularColor3[1] + specularColor3[2]) / 3.0f,
                    0.0f,
                    1.0f);
            }

            float shininessStrength = 0.0f;
            if (TryGetMaterialScalar(material, AI_MATKEY_SHININESS_STRENGTH, shininessStrength))
            {
                info.specular = std::clamp(std::max(info.specular, shininessStrength), 0.0f, 1.0f);
            }

            float opacity = 1.0f;
            if (TryGetMaterialScalar(material, AI_MATKEY_OPACITY, opacity))
            {
                info.opacity = std::clamp(opacity, 0.0f, 1.0f);
            }

            float bumpScaling = 1.0f;
            if (TryGetMaterialScalar(material, AI_MATKEY_BUMPSCALING, bumpScaling))
            {
                info.normalStrength = std::max(0.0f, bumpScaling);
            }

            int twoSided = 0;
            if (TryGetMaterialScalar(material, AI_MATKEY_TWOSIDED, twoSided))
            {
                info.twoSided = twoSided != 0;
            }

            return info;
        }

        void GatherAssimpNodeSceneParts(
            const aiScene& scene,
            const aiNode& node,
            const aiMatrix4x4& parentTransform,
            const std::filesystem::path& sourcePath,
            std::vector<MeshScenePart>& outParts)
        {
            const aiMatrix4x4 worldTransform = parentTransform * node.mTransformation;

            for (unsigned int meshListIndex = 0; meshListIndex < node.mNumMeshes; ++meshListIndex)
            {
                const aiMesh* mesh = scene.mMeshes[node.mMeshes[meshListIndex]];
                if (mesh == nullptr || !mesh->HasPositions() || mesh->mNumVertices == 0)
                {
                    continue;
                }

                MeshScenePart part;
                part.boundsMin = {
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max(),
                    std::numeric_limits<float>::max()
                };
                part.boundsMax = {
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest(),
                    std::numeric_limits<float>::lowest()
                };

                const aiMaterial* material =
                    mesh->mMaterialIndex < scene.mNumMaterials ? scene.mMaterials[mesh->mMaterialIndex] : nullptr;
                part.material = ExtractAssimpMaterialInfo(sourcePath, material);
                part.name = mesh->mName.C_Str();
                if (part.name.empty())
                {
                    part.name = part.material.name;
                }
                if (part.name.empty())
                {
                    part.name = node.mName.C_Str();
                }

                bool hasBounds = false;
                part.mesh.vertices.reserve(mesh->mNumVertices);
                for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex)
                {
                    PrimitiveVertex vertex {};
                    const aiVector3D transformedPosition = worldTransform * mesh->mVertices[vertexIndex];
                    vertex.position = { transformedPosition.x, transformedPosition.y, transformedPosition.z };

                    if (mesh->HasVertexColors(0))
                    {
                        const aiColor4D& color = mesh->mColors[0][vertexIndex];
                        vertex.color = { color.r, color.g, color.b };
                    }
                    else
                    {
                        vertex.color = { 1.0f, 1.0f, 1.0f };
                    }

                    if (mesh->HasTextureCoords(0))
                    {
                        const aiVector3D& uv = mesh->mTextureCoords[0][vertexIndex];
                        vertex.uv = { uv.x, uv.y };
                    }
                    else
                    {
                        vertex.uv = { 0.0f, 0.0f };
                    }

                    part.mesh.vertices.push_back(vertex);
                    UpdateBounds(transformedPosition, part.boundsMin, part.boundsMax);
                    hasBounds = true;
                }

                for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
                {
                    const aiFace& face = mesh->mFaces[faceIndex];
                    if (face.mNumIndices < 3)
                    {
                        continue;
                    }

                    for (unsigned int triangleIndex = 1; triangleIndex + 1 < face.mNumIndices; ++triangleIndex)
                    {
                        part.mesh.indices.push_back(face.mIndices[0]);
                        part.mesh.indices.push_back(face.mIndices[triangleIndex]);
                        part.mesh.indices.push_back(face.mIndices[triangleIndex + 1]);
                    }
                }

                if (hasBounds && !part.mesh.vertices.empty() && !part.mesh.indices.empty())
                {
                    outParts.push_back(std::move(part));
                }
            }

            for (unsigned int childIndex = 0; childIndex < node.mNumChildren; ++childIndex)
            {
                if (node.mChildren[childIndex] != nullptr)
                {
                    GatherAssimpNodeSceneParts(scene, *node.mChildren[childIndex], worldTransform, sourcePath, outParts);
                }
            }
        }

        void BuildMeshSections(const PrimitiveMeshData& meshData, std::vector<MeshAssetSection>& outSections)
        {
            outSections.clear();
            if (meshData.vertices.empty() || meshData.indices.size() < 3)
            {
                return;
            }

            const std::size_t triangleCount = meshData.indices.size() / 3;
            const std::size_t sectionTriangleCount =
                std::max<std::size_t>(1, std::min<std::size_t>(kMaxTrianglesPerSection, triangleCount));
            const std::size_t sectionCount =
                std::max<std::size_t>(1, (triangleCount + sectionTriangleCount - 1) / sectionTriangleCount);
            outSections.reserve(sectionCount);

            for (std::size_t sectionIndex = 0; sectionIndex < sectionCount; ++sectionIndex)
            {
                const std::size_t triangleStart = sectionIndex * sectionTriangleCount;
                const std::size_t triangleEnd = std::min(triangleStart + sectionTriangleCount, triangleCount);
                if (triangleStart >= triangleEnd)
                {
                    continue;
                }

                MeshAssetSection section;
                std::unordered_map<std::uint32_t, std::uint32_t> vertexRemap;
                vertexRemap.reserve((triangleEnd - triangleStart) * 3);

                for (std::size_t triangle = triangleStart; triangle < triangleEnd; ++triangle)
                {
                    for (std::size_t corner = 0; corner < 3; ++corner)
                    {
                        const std::uint32_t sourceIndex = meshData.indices[triangle * 3 + corner];
                        const auto found = vertexRemap.find(sourceIndex);
                        if (found != vertexRemap.end())
                        {
                            section.mesh.indices.push_back(found->second);
                            continue;
                        }

                        const std::uint32_t remappedIndex =
                            static_cast<std::uint32_t>(section.mesh.vertices.size());
                        vertexRemap.emplace(sourceIndex, remappedIndex);
                        section.mesh.vertices.push_back(meshData.vertices[sourceIndex]);
                        section.mesh.indices.push_back(remappedIndex);
                    }
                }

                if (section.mesh.vertices.empty() || section.mesh.indices.empty())
                {
                    continue;
                }

                BuildSectionBounds(section.mesh, section.boundsMin, section.boundsMax);
                outSections.push_back(std::move(section));
            }
        }

        bool BuildMeshChunkPayload(
            const MeshAssetSection& section,
            std::vector<std::uint8_t>& outPayload,
            std::string& outError)
        {
            outPayload.clear();
            if (section.mesh.vertices.empty() || section.mesh.indices.empty())
            {
                outError = "Cooked mesh chunk requires geometry.";
                return false;
            }

            outPayload.insert(
                outPayload.end(),
                reinterpret_cast<const std::uint8_t*>(kMeshChunkMagic.data()),
                reinterpret_cast<const std::uint8_t*>(kMeshChunkMagic.data()) + kMeshChunkMagic.size());
            AppendPod(outPayload, kMeshChunkVersion);
            AppendPod(outPayload, static_cast<std::uint32_t>(section.mesh.vertices.size()));
            AppendPod(outPayload, static_cast<std::uint32_t>(section.mesh.indices.size()));

            const std::size_t vertexBytes = section.mesh.vertices.size() * sizeof(PrimitiveVertex);
            const std::size_t indexBytes = section.mesh.indices.size() * sizeof(std::uint32_t);
            const std::size_t start = outPayload.size();
            outPayload.resize(start + vertexBytes + indexBytes);
            std::memcpy(outPayload.data() + start, section.mesh.vertices.data(), vertexBytes);
            std::memcpy(outPayload.data() + start + vertexBytes, section.mesh.indices.data(), indexBytes);
            outError.clear();
            return true;
        }

        bool TryLoadMeshChunkPayload(
            const std::uint8_t* bytes,
            const std::size_t byteCount,
            PrimitiveMeshData& outMesh,
            std::string& outError)
        {
            outMesh = {};
            if (bytes == nullptr || byteCount < kMeshChunkMagic.size())
            {
                outError = "Mesh chunk payload is truncated.";
                return false;
            }
            if (!std::equal(kMeshChunkMagic.begin(), kMeshChunkMagic.end(), reinterpret_cast<const char*>(bytes)))
            {
                outError = "Mesh chunk payload has invalid magic.";
                return false;
            }

            const std::uint8_t* cursor = bytes + kMeshChunkMagic.size();
            std::size_t remainingBytes = byteCount - kMeshChunkMagic.size();
            std::uint32_t version = 0;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            if (!ReadPod(cursor, remainingBytes, version) ||
                !ReadPod(cursor, remainingBytes, vertexCount) ||
                !ReadPod(cursor, remainingBytes, indexCount))
            {
                outError = "Mesh chunk header is truncated.";
                return false;
            }
            if (version != kMeshChunkVersion)
            {
                outError = "Unsupported mesh chunk version.";
                return false;
            }

            const std::size_t vertexBytes = static_cast<std::size_t>(vertexCount) * sizeof(PrimitiveVertex);
            const std::size_t indexBytes = static_cast<std::size_t>(indexCount) * sizeof(std::uint32_t);
            if (remainingBytes < vertexBytes + indexBytes)
            {
                outError = "Mesh chunk payload is truncated.";
                return false;
            }

            outMesh.vertices.resize(vertexCount);
            outMesh.indices.resize(indexCount);
            if (vertexBytes > 0)
            {
                std::memcpy(outMesh.vertices.data(), cursor, vertexBytes);
                cursor += vertexBytes;
                remainingBytes -= vertexBytes;
            }
            if (indexBytes > 0)
            {
                std::memcpy(outMesh.indices.data(), cursor, indexBytes);
            }
            if (outMesh.vertices.empty() || outMesh.indices.empty())
            {
                outError = "Mesh chunk payload is empty.";
                return false;
            }

            for (const std::uint32_t index : outMesh.indices)
            {
                if (index >= outMesh.vertices.size())
                {
                    outMesh = {};
                    outError = "Mesh chunk payload contains out-of-range indices.";
                    return false;
                }
            }

            return true;
        }

        bool LoadExternalSectionChunks(
            const std::filesystem::path& logicalPath,
            MeshAssetData& outData,
            std::string& outError)
        {
            if (outData.sections.empty())
            {
                outData.mesh = {};
                return true;
            }

            const std::filesystem::path baseDirectory = logicalPath.parent_path();
            for (MeshAssetSection& section : outData.sections)
            {
                const std::filesystem::path sectionPath =
                    section.chunkPath.is_absolute() ? section.chunkPath : (baseDirectory / section.chunkPath).lexically_normal();
                std::vector<std::uint8_t> chunkBytes;
                if (!ReadFileBytes(sectionPath, chunkBytes, outError))
                {
                    outError = "Failed to load mesh chunk: " + sectionPath.string();
                    return false;
                }

                if (!TryLoadMeshChunkPayload(chunkBytes.data(), chunkBytes.size(), section.mesh, outError))
                {
                    outError = "Failed to decode mesh chunk '" + sectionPath.string() + "': " + outError;
                    return false;
                }

                BuildSectionBounds(section.mesh, section.boundsMin, section.boundsMax);
                section.chunkPath = sectionPath.lexically_normal();
            }

            outData.mesh = MergeMeshSections(outData.sections);
            return !outData.mesh.vertices.empty() && !outData.mesh.indices.empty();
        }

        void GatherAssimpNodeMeshes(
            const aiScene& scene,
            const aiNode& node,
            const aiMatrix4x4& parentTransform,
            PrimitiveMeshData& outMesh,
            std::array<float, 3>& boundsMin,
            std::array<float, 3>& boundsMax,
            bool& hasBounds)
        {
            const aiMatrix4x4 worldTransform = parentTransform * node.mTransformation;

            for (unsigned int meshListIndex = 0; meshListIndex < node.mNumMeshes; ++meshListIndex)
            {
                const aiMesh* mesh = scene.mMeshes[node.mMeshes[meshListIndex]];
                if (mesh == nullptr || !mesh->HasPositions() || mesh->mNumVertices == 0)
                {
                    continue;
                }

                const std::uint32_t vertexOffset = static_cast<std::uint32_t>(outMesh.vertices.size());
                outMesh.vertices.reserve(outMesh.vertices.size() + mesh->mNumVertices);

                for (unsigned int vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex)
                {
                    PrimitiveVertex vertex {};
                    const aiVector3D transformedPosition = worldTransform * mesh->mVertices[vertexIndex];
                    vertex.position = { transformedPosition.x, transformedPosition.y, transformedPosition.z };

                    if (mesh->HasVertexColors(0))
                    {
                        const aiColor4D& color = mesh->mColors[0][vertexIndex];
                        vertex.color = { color.r, color.g, color.b };
                    }
                    else
                    {
                        vertex.color = { 1.0f, 1.0f, 1.0f };
                    }

                    if (mesh->HasTextureCoords(0))
                    {
                        const aiVector3D& uv = mesh->mTextureCoords[0][vertexIndex];
                        vertex.uv = { uv.x, uv.y };
                    }
                    else
                    {
                        vertex.uv = { 0.0f, 0.0f };
                    }

                    outMesh.vertices.push_back(vertex);
                    UpdateBounds(transformedPosition, boundsMin, boundsMax);
                    hasBounds = true;
                }

                for (unsigned int faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
                {
                    const aiFace& face = mesh->mFaces[faceIndex];
                    if (face.mNumIndices < 3)
                    {
                        continue;
                    }

                    for (unsigned int triangleIndex = 1; triangleIndex + 1 < face.mNumIndices; ++triangleIndex)
                    {
                        outMesh.indices.push_back(vertexOffset + face.mIndices[0]);
                        outMesh.indices.push_back(vertexOffset + face.mIndices[triangleIndex]);
                        outMesh.indices.push_back(vertexOffset + face.mIndices[triangleIndex + 1]);
                    }
                }
            }

            for (unsigned int childIndex = 0; childIndex < node.mNumChildren; ++childIndex)
            {
                if (node.mChildren[childIndex] != nullptr)
                {
                    GatherAssimpNodeMeshes(scene, *node.mChildren[childIndex], worldTransform, outMesh, boundsMin, boundsMax, hasBounds);
                }
            }
        }

        bool LoadMeshDataFromAssimpScene(const aiScene* scene, MeshAssetData& outData, std::string& outError)
        {
            outData = {};
            if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr)
            {
                outError = "Assimp failed to parse mesh scene.";
                return false;
            }

            outData.boundsMin = {
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()
            };
            outData.boundsMax = {
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()
            };

            bool hasBounds = false;
            GatherAssimpNodeMeshes(*scene, *scene->mRootNode, aiMatrix4x4(), outData.mesh, outData.boundsMin, outData.boundsMax, hasBounds);
            if (!hasBounds || outData.mesh.vertices.empty() || outData.mesh.indices.empty())
            {
                outError = "Mesh does not contain any renderable triangles.";
                return false;
            }
            return true;
        }

        const aiScene* ReadAssimpScene(
            Assimp::Importer& importer,
            const std::uint8_t* bytes,
            const std::size_t byteCount,
            const std::filesystem::path& logicalPath,
            const unsigned int importFlags)
        {
            std::error_code existsError;
            if (!logicalPath.empty() && std::filesystem::exists(logicalPath, existsError) && !existsError)
            {
                const std::string nativePath = logicalPath.string();
                if (!nativePath.empty())
                {
                    if (const aiScene* scene = importer.ReadFile(nativePath, importFlags); scene != nullptr)
                    {
                        return scene;
                    }
                }
            }

            const std::string extension = ToLowerCopy(logicalPath.extension().string());
            std::string formatHint = extension;
            if (!formatHint.empty() && formatHint.front() == '.')
            {
                formatHint.erase(0, 1);
            }
            if (formatHint.empty())
            {
                formatHint = "obj";
            }

            return importer.ReadFileFromMemory(
                bytes,
                static_cast<unsigned int>(byteCount),
                importFlags,
                formatHint.c_str());
        }

        bool LoadMeshDataWithAssimp(
            const std::uint8_t* bytes,
            const std::size_t byteCount,
            const std::filesystem::path& logicalPath,
            MeshAssetData& outData,
            std::string& outError)
        {
            const unsigned int importFlags =
                aiProcess_Triangulate |
                aiProcess_JoinIdenticalVertices |
                aiProcess_OptimizeMeshes |
                aiProcess_OptimizeGraph;

            Assimp::Importer importer;
            const aiScene* scene = ReadAssimpScene(importer, bytes, byteCount, logicalPath, importFlags);
            if (!LoadMeshDataFromAssimpScene(scene, outData, outError))
            {
                const char* assimpError = importer.GetErrorString();
                if (assimpError != nullptr && assimpError[0] != '\0')
                {
                    outError = assimpError;
                }
                return false;
            }

            outData.sourceName = logicalPath.filename().string();
            outData.cooked = false;
            std::vector<MeshScenePart> sceneParts;
            GatherAssimpNodeSceneParts(*scene, *scene->mRootNode, aiMatrix4x4(), logicalPath, sceneParts);
            if (!sceneParts.empty())
            {
                BuildSectionsFromSceneParts(sceneParts, outData.sections);
            }
            else
            {
                BuildMeshSections(outData.mesh, outData.sections);
            }
            UpdateMaterialSlotNames(outData.sections, outData.materialSlotNames);
            return true;
        }

        bool LoadMeshDataFromLegacyIntermediate(
            const std::uint8_t* bytes,
            const std::size_t byteCount,
            MeshAssetData& outData,
            std::string& outError)
        {
            IntermediateSourceData intermediate {};
            if (!LoadIntermediateSourceData(bytes, byteCount, intermediate))
            {
                outError = "Mesh payload is neither cooked nor a valid legacy intermediate mesh.";
                return false;
            }

            if (!LoadMeshDataWithAssimp(
                    intermediate.sourceBytes,
                    intermediate.sourceSize,
                    std::filesystem::path(intermediate.sourceName),
                    outData,
                    outError))
            {
                return false;
            }

            outData.sourceName = intermediate.sourceName;
            return true;
        }
    }

    std::filesystem::path ResolveMeshAssetPath(const std::string& meshSource)
    {
        if (meshSource.empty())
        {
            return {};
        }

        auto preferCookedSibling = [](const std::filesystem::path& path) -> std::filesystem::path
        {
            const std::string extension = ToLowerCopy(path.extension().string());
            if (extension == ".lumamesh")
            {
                return path.lexically_normal();
            }

            if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb")
            {
                std::error_code cookedEc;
                const std::filesystem::path cookedPath = path.parent_path() / (path.stem().string() + ".lumamesh");
                if (std::filesystem::exists(cookedPath, cookedEc))
                {
                    return cookedPath.lexically_normal();
                }
            }

            return path.lexically_normal();
        };

        const std::filesystem::path sourcePath(meshSource);
        std::error_code ec;
        if (sourcePath.is_absolute() && std::filesystem::exists(sourcePath, ec))
        {
            return preferCookedSibling(sourcePath);
        }

        std::vector<std::filesystem::path> bases;
        if (Project::IsLoaded())
        {
            bases.push_back(Project::GetAssetsPath());
            bases.push_back(Project::GetProjectRoot());
        }

        for (const auto& base : bases)
        {
            const std::filesystem::path candidate = (base / sourcePath).lexically_normal();
            if (std::filesystem::exists(candidate, ec))
            {
                return preferCookedSibling(candidate);
            }
        }

        if (!sourcePath.has_extension())
        {
            static const std::array<std::string, 5> kMeshExtensions {
                ".lumamesh",
                ".obj",
                ".fbx",
                ".gltf",
                ".glb"
            };

            for (const auto& base : bases)
            {
                for (const std::string& extension : kMeshExtensions)
                {
                    const std::filesystem::path candidate = (base / (meshSource + extension)).lexically_normal();
                    if (std::filesystem::exists(candidate, ec))
                    {
                        return candidate;
                    }
                }
            }

            if (Project::IsLoaded())
            {
                const std::string wantedStem = ToLowerCopy(sourcePath.stem().string().empty() ? sourcePath.filename().string() : sourcePath.stem().string());
                const std::filesystem::path assetsRoot = Project::GetAssetsPath();
                for (const auto& entry : std::filesystem::recursive_directory_iterator(
                         assetsRoot,
                         std::filesystem::directory_options::skip_permission_denied,
                         ec))
                {
                    if (ec)
                    {
                        break;
                    }
                    if (!entry.is_regular_file(ec))
                    {
                        continue;
                    }
                    const std::filesystem::path entryPath = entry.path();
                    const std::string extension = ToLowerCopy(entryPath.extension().string());
                    if (extension != ".lumamesh" && extension != ".obj" && extension != ".fbx" && extension != ".gltf" && extension != ".glb")
                    {
                        continue;
                    }
                    if (ToLowerCopy(entryPath.stem().string()) == wantedStem)
                    {
                        return entryPath.lexically_normal();
                    }
                }
            }
        }

        return {};
    }

    bool BuildCookedMeshPayload(
        const PrimitiveMeshData& meshData,
        const std::string_view sourceName,
        const std::array<float, 3>& boundsMin,
        const std::array<float, 3>& boundsMax,
        std::vector<std::uint8_t>& outPayload,
        std::string& outError)
    {
        outPayload.clear();
        if (meshData.vertices.empty() || meshData.indices.empty())
        {
            outError = "Cooked mesh payload requires at least one triangle.";
            return false;
        }

        std::vector<MeshAssetSection> sections;
        BuildMeshSections(meshData, sections);
        if (sections.empty())
        {
            outError = "Failed to generate cooked mesh sections.";
            return false;
        }

        outPayload.insert(outPayload.end(), reinterpret_cast<const std::uint8_t*>(kCookedMeshMagic.data()), reinterpret_cast<const std::uint8_t*>(kCookedMeshMagic.data()) + kCookedMeshMagic.size());
        AppendPod(outPayload, static_cast<std::uint32_t>(2));
        AppendPod(outPayload, static_cast<std::uint32_t>(sourceName.size()));
        AppendPod(outPayload, static_cast<std::uint32_t>(sections.size()));
        for (int axis = 0; axis < 3; ++axis)
        {
            AppendPod(outPayload, boundsMin[axis]);
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            AppendPod(outPayload, boundsMax[axis]);
        }
        outPayload.insert(outPayload.end(), sourceName.begin(), sourceName.end());

        for (const MeshAssetSection& section : sections)
        {
            AppendPod(outPayload, static_cast<std::uint32_t>(section.mesh.vertices.size()));
            AppendPod(outPayload, static_cast<std::uint32_t>(section.mesh.indices.size()));
            for (int axis = 0; axis < 3; ++axis)
            {
                AppendPod(outPayload, section.boundsMin[axis]);
            }
            for (int axis = 0; axis < 3; ++axis)
            {
                AppendPod(outPayload, section.boundsMax[axis]);
            }

            const std::size_t vertexBytes = section.mesh.vertices.size() * sizeof(PrimitiveVertex);
            const std::size_t indexBytes = section.mesh.indices.size() * sizeof(std::uint32_t);
            const std::size_t start = outPayload.size();
            outPayload.resize(start + vertexBytes + indexBytes);
            std::memcpy(outPayload.data() + start, section.mesh.vertices.data(), vertexBytes);
            std::memcpy(outPayload.data() + start + vertexBytes, section.mesh.indices.data(), indexBytes);
        }
        outError.clear();
        return true;
    }

    bool BuildCookedMeshAssetPayload(
        const MeshAssetData& meshAsset,
        const std::string_view sourceName,
        const std::filesystem::path& chunkDirectory,
        std::vector<std::uint8_t>& outPayload,
        std::vector<MeshAssetChunkPayload>& outChunks,
        std::string& outError)
    {
        outPayload.clear();
        outChunks.clear();
        if (meshAsset.mesh.vertices.empty() || meshAsset.mesh.indices.empty())
        {
            outError = "Cooked mesh payload requires at least one triangle.";
            return false;
        }

        std::vector<MeshAssetSection> sections = meshAsset.sections;
        if (sections.empty())
        {
            BuildMeshSections(meshAsset.mesh, sections);
        }
        if (sections.empty())
        {
            outError = "Failed to generate cooked mesh sections.";
            return false;
        }

        outPayload.insert(
            outPayload.end(),
            reinterpret_cast<const std::uint8_t*>(kCookedMeshMagic.data()),
            reinterpret_cast<const std::uint8_t*>(kCookedMeshMagic.data()) + kCookedMeshMagic.size());
        AppendPod(outPayload, kCookedMeshVersion);
        AppendPod(outPayload, static_cast<std::uint32_t>(sourceName.size()));
        AppendPod(outPayload, static_cast<std::uint32_t>(sections.size()));
        for (int axis = 0; axis < 3; ++axis)
        {
            AppendPod(outPayload, meshAsset.boundsMin[axis]);
        }
        for (int axis = 0; axis < 3; ++axis)
        {
            AppendPod(outPayload, meshAsset.boundsMax[axis]);
        }
        outPayload.insert(outPayload.end(), sourceName.begin(), sourceName.end());

        outChunks.reserve(sections.size());
        for (std::size_t sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex)
        {
            MeshAssetChunkPayload chunk;
            chunk.relativePath =
                (chunkDirectory / ("section_" + std::to_string(sectionIndex) + ".lmshchunk")).lexically_normal();
            if (!BuildMeshChunkPayload(sections[sectionIndex], chunk.payload, outError))
            {
                outPayload.clear();
                outChunks.clear();
                return false;
            }

            const std::string chunkPathString = chunk.relativePath.generic_string();
            AppendPod(outPayload, static_cast<std::uint32_t>(chunkPathString.size()));
            AppendPod(outPayload, sections[sectionIndex].materialSlotIndex);
            AppendPod(outPayload, static_cast<std::uint32_t>(sections[sectionIndex].materialSlotName.size()));
            for (int axis = 0; axis < 3; ++axis)
            {
                AppendPod(outPayload, sections[sectionIndex].boundsMin[axis]);
            }
            for (int axis = 0; axis < 3; ++axis)
            {
                AppendPod(outPayload, sections[sectionIndex].boundsMax[axis]);
            }
            outPayload.insert(outPayload.end(), chunkPathString.begin(), chunkPathString.end());
            outPayload.insert(
                outPayload.end(),
                sections[sectionIndex].materialSlotName.begin(),
                sections[sectionIndex].materialSlotName.end());
            outChunks.push_back(std::move(chunk));
        }

        outError.clear();
        return true;
    }

    bool LoadMeshAssetData(
        const std::filesystem::path& meshPath,
        MeshAssetData& outData,
        std::string& outError)
    {
        std::vector<std::uint8_t> bytes;
        if (!ReadFileBytes(meshPath, bytes, outError))
        {
            return false;
        }
        return LoadMeshAssetDataFromBytes(bytes.data(), bytes.size(), meshPath, outData, outError);
    }

    bool LoadMeshSceneParts(
        const std::filesystem::path& meshPath,
        std::vector<MeshScenePart>& outParts,
        std::string& outError)
    {
        outParts.clear();
        const std::string extension = ToLowerCopy(meshPath.extension().string());
        if (extension == ".lumamesh")
        {
            outError = "Scene part extraction is only supported from raw source mesh files.";
            return false;
        }

        const unsigned int importFlags =
            aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices |
            aiProcess_OptimizeMeshes |
            aiProcess_OptimizeGraph;

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(meshPath.string(), importFlags);
        if (scene == nullptr || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) != 0 || scene->mRootNode == nullptr)
        {
            outError = importer.GetErrorString();
            if (outError.empty())
            {
                outError = "Assimp failed to parse mesh scene parts.";
            }
            return false;
        }

        GatherAssimpNodeSceneParts(*scene, *scene->mRootNode, aiMatrix4x4(), meshPath, outParts);
        if (outParts.empty())
        {
            outError = "Mesh does not contain any scene parts.";
            return false;
        }

        return true;
    }

    bool LoadMeshAssetDataFromBytes(
        const std::uint8_t* bytes,
        const std::size_t byteCount,
        const std::filesystem::path& logicalPath,
        MeshAssetData& outData,
        std::string& outError)
    {
        outData = {};
        if (bytes == nullptr || byteCount == 0)
        {
            outError = "Mesh payload is empty.";
            return false;
        }

        const std::string extension = ToLowerCopy(logicalPath.extension().string());
        if (extension == ".lumamesh")
        {
            if (TryLoadCookedMeshPayload(bytes, byteCount, logicalPath, true, outData, outError))
            {
                return true;
            }

            return LoadMeshDataFromLegacyIntermediate(bytes, byteCount, outData, outError);
        }

        return LoadMeshDataWithAssimp(bytes, byteCount, logicalPath, outData, outError);
    }

    bool LoadMeshAssetManifestFromBytes(
        const std::uint8_t* bytes,
        const std::size_t byteCount,
        const std::filesystem::path& logicalPath,
        MeshAssetData& outData,
        std::string& outError)
    {
        outData = {};
        if (bytes == nullptr || byteCount == 0)
        {
            outError = "Mesh payload is empty.";
            return false;
        }
        if (ToLowerCopy(logicalPath.extension().string()) != ".lumamesh")
        {
            outError = "Mesh manifest loading only supports .lumamesh assets.";
            return false;
        }
        return TryLoadCookedMeshPayload(bytes, byteCount, logicalPath, false, outData, outError);
    }

    bool LoadMeshChunkPayloadFromBytes(
        const std::uint8_t* bytes,
        const std::size_t byteCount,
        PrimitiveMeshData& outMesh,
        std::string& outError)
    {
        return TryLoadMeshChunkPayload(bytes, byteCount, outMesh, outError);
    }

    bool GetMeshAssetHalfExtents(
        const std::filesystem::path& meshPath,
        std::array<float, 3>& outHalfExtents,
        std::string& outError)
    {
        outHalfExtents = { 0.5f, 0.5f, 0.5f };
        MeshAssetData meshData;
        if (!LoadMeshAssetData(meshPath, meshData, outError))
        {
            return false;
        }

        outHalfExtents = {
            std::max(0.001f, (meshData.boundsMax[0] - meshData.boundsMin[0]) * 0.5f),
            std::max(0.001f, (meshData.boundsMax[1] - meshData.boundsMin[1]) * 0.5f),
            std::max(0.001f, (meshData.boundsMax[2] - meshData.boundsMin[2]) * 0.5f)
        };
        return true;
    }
}
