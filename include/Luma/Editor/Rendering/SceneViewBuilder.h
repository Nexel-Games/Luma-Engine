#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "Luma/Core/App/RenderPipeline.h"
#include "Luma/RHI/RendererAPI.h"
#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct SceneViewBuilderCameraState
    {
        std::array<float, 3> position { 0.0f, 0.0f, 5.0f };
        float yaw = -90.0f;
        float pitch = 0.0f;
    };

    struct SceneViewBuildInput
    {
        const Scene* scene = nullptr;
        RendererAPI rendererApi = RendererAPI::OpenGL;
        float timeSeconds = 0.0f;
        std::uint32_t outputWidth = 1;
        std::uint32_t outputHeight = 1;
        bool previewSceneCameraLens = true;
        EntityID activeCameraEntity = entt::null;
        EntityID selectedEntity = entt::null;
        SceneViewBuilderCameraState editorCamera {};
        const MeshDesc* skyMesh = nullptr;
        std::uint64_t skyMeshRevision = 0;
        bool hasSkyMesh = false;
        const MeshDesc* gridMesh = nullptr;
        std::uint64_t gridMeshRevision = 0;
        bool hasGridMesh = false;
        const std::vector<SceneRenderItem>* renderItems = nullptr;
        std::uint64_t renderItemsRevision = 0;
        std::array<float, 3> skyAverageColor { 0.0f, 0.0f, 0.0f };
        std::function<std::filesystem::path(const std::string&)> resolveSkyAssetPath;
    };

    struct SceneViewBuildResult
    {
        SceneView sceneView {};
        EntityID lensSourceEntity = entt::null;
    };

    EntityID FindEditorCameraEntity(const Scene& scene, EntityID selectedEntity);
    EntityID FindHighestPriorityPrimaryCameraEntity(const Scene& scene);
    EntityID FindPrimarySkyEntity(const Scene& scene);
    SceneViewBuildResult BuildSceneView(const SceneViewBuildInput& input);
}
