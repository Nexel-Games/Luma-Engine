#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Luma/Renderer/PrimitiveMeshFactory.h"

namespace Luma::Assets
{
    struct MeshAssetSection
    {
        PrimitiveMeshData mesh;
        std::array<float, 3> boundsMin { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> boundsMax { 0.0f, 0.0f, 0.0f };
        std::filesystem::path chunkPath;
        std::uint32_t materialSlotIndex = 0;
        std::string materialSlotName;
    };

    struct MeshAssetChunkPayload
    {
        std::filesystem::path relativePath;
        std::vector<std::uint8_t> payload;
    };

    struct MeshAssetData
    {
        PrimitiveMeshData mesh;
        std::vector<MeshAssetSection> sections;
        std::array<float, 3> boundsMin { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> boundsMax { 0.0f, 0.0f, 0.0f };
        std::string sourceName;
        std::vector<std::string> materialSlotNames;
        bool cooked = false;
    };

    struct MeshMaterialInfo
    {
        std::string name;
        std::array<float, 4> baseColorTint { 1.0f, 1.0f, 1.0f, 1.0f };
        std::array<float, 4> emissiveColor { 0.0f, 0.0f, 0.0f, 1.0f };
        float emissiveIntensity = 0.0f;
        float metallic = 0.0f;
        float roughness = 0.5f;
        float specular = 0.5f;
        float ambientOcclusion = 1.0f;
        float normalStrength = 1.0f;
        float opacity = 1.0f;
        bool twoSided = false;
        std::filesystem::path albedoTexture;
        std::filesystem::path normalTexture;
        std::filesystem::path heightTexture;
        std::filesystem::path ormTexture;
        std::filesystem::path metallicTexture;
        std::filesystem::path roughnessTexture;
        std::filesystem::path ambientOcclusionTexture;
        std::filesystem::path emissiveTexture;
        std::filesystem::path opacityTexture;
    };

    struct MeshScenePart
    {
        std::string name;
        PrimitiveMeshData mesh;
        std::array<float, 3> boundsMin { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> boundsMax { 0.0f, 0.0f, 0.0f };
        MeshMaterialInfo material;
    };

    std::filesystem::path ResolveMeshAssetPath(const std::string& meshSource);

    bool BuildCookedMeshPayload(
        const PrimitiveMeshData& meshData,
        std::string_view sourceName,
        const std::array<float, 3>& boundsMin,
        const std::array<float, 3>& boundsMax,
        std::vector<std::uint8_t>& outPayload,
        std::string& outError);

    bool BuildCookedMeshAssetPayload(
        const MeshAssetData& meshAsset,
        std::string_view sourceName,
        const std::filesystem::path& chunkDirectory,
        std::vector<std::uint8_t>& outPayload,
        std::vector<MeshAssetChunkPayload>& outChunks,
        std::string& outError);

    bool LoadMeshAssetData(
        const std::filesystem::path& meshPath,
        MeshAssetData& outData,
        std::string& outError);

    bool LoadMeshSceneParts(
        const std::filesystem::path& meshPath,
        std::vector<MeshScenePart>& outParts,
        std::string& outError);

    bool LoadMeshAssetDataFromBytes(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        const std::filesystem::path& logicalPath,
        MeshAssetData& outData,
        std::string& outError);

    bool LoadMeshAssetManifestFromBytes(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        const std::filesystem::path& logicalPath,
        MeshAssetData& outData,
        std::string& outError);

    bool LoadMeshChunkPayloadFromBytes(
        const std::uint8_t* bytes,
        std::size_t byteCount,
        PrimitiveMeshData& outMesh,
        std::string& outError);

    bool GetMeshAssetHalfExtents(
        const std::filesystem::path& meshPath,
        std::array<float, 3>& outHalfExtents,
        std::string& outError);
}
