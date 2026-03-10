#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Luma/Asset/Core/MeshAssetIO.h"
#include "Luma/Asset/Streaming/IResourceStreamingService.h"
#include "Luma/Renderer/PrimitiveMeshFactory.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/TransformComponent.h"

namespace Luma::Editor
{
    struct MeshStreamingSectionStreamState
    {
        Assets::StreamRequestHandle streamHandle = 0;
        std::uint64_t residentCpuBytes = 0;
        bool decodeFailed = false;
        PrimitiveMeshData mesh;
    };

    struct MeshStreamingAssetState
    {
        std::filesystem::path requestPath;
        std::filesystem::path resolvedPath;
        Assets::StreamRequestHandle streamHandle = 0;
        std::uint32_t resolvedLod = std::numeric_limits<std::uint32_t>::max();
        std::uint64_t residentCpuBytes = 0;
        bool decodeFailed = false;
        bool sectionStreamingDisabled = false;
        std::vector<std::uint32_t> activeSections;
        Assets::MeshAssetData assetData;
        std::vector<MeshStreamingSectionStreamState> sectionStreams;
        PrimitiveMeshData mesh;
    };

    struct MeshStreamingImportedScenePartsState
    {
        std::filesystem::path resolvedPath;
        std::vector<Assets::MeshScenePart> parts;
        bool loadAttempted = false;
        bool loadFailed = false;
    };

    struct MeshStreamingGeometryContext
    {
        std::uint32_t primitiveMeshLod = 0;
        std::array<float, 3> cameraPosition { 0.0f, 0.0f, 0.0f };
        bool projectLoaded = false;
        std::filesystem::path projectAssetsPath;
        std::filesystem::path projectRoot;
        Assets::IResourceStreamingService* streamingService = nullptr;
        std::unordered_map<std::string, MeshStreamingImportedScenePartsState>* importedSceneParts = nullptr;
        std::unordered_map<std::string, MeshStreamingAssetState>* streamedMeshAssets = nullptr;
        std::function<void(std::string_view)> logImportError;
        std::function<void(std::string_view)> logStreamingError;
        std::function<void(std::string_view)> logStreamingWarn;
    };

    class MeshStreamingGeometryService
    {
    public:
        std::uint32_t ComputeRequestedMeshLod(
            const MeshStreamingGeometryContext& context,
            const TransformComponent& transform,
            const MeshRendererComponent& meshRenderer) const;

        const PrimitiveMeshData* ResolveMeshRendererGeometry(
            const MeshStreamingGeometryContext& context,
            const TransformComponent& transform,
            const MeshRendererComponent& meshRenderer) const;

        MeshStreamingImportedScenePartsState* ResolveImportedSceneParts(
            const MeshStreamingGeometryContext& context,
            const std::string& sourcePath) const;

        bool InvalidateFromStreamingEvent(
            const Assets::StreamEvent& event,
            std::unordered_map<std::string, MeshStreamingAssetState>& streamedMeshAssets) const;
    };
}
