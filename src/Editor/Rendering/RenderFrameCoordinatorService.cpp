#include "Luma/Editor/Rendering/RenderFrameCoordinatorService.h"

#include <algorithm>
#include <chrono>
#include <iostream>

#include "Luma/Asset/Streaming/ResourceStreamingService.h"
#include "Luma/Scene/CameraComponent.h"
#include "Luma/Editor/Viewport/EditorViewportController.h"
#include "Luma/RHI/GPUResourceManager.h"
#include "Luma/RHI/IRenderBackend.h"

namespace Luma::Editor
{
    bool RenderFrameCoordinatorService::Render(RenderFrameCoordinatorContext& context) const
    {
        if (context.renderer == nullptr ||
            context.lastRenderer == nullptr ||
            context.gpuResourceManager == nullptr ||
            context.activePipeline == nullptr ||
            context.activeProfile == nullptr ||
            context.viewportController == nullptr)
        {
            return false;
        }

        UpdateRendererBinding(context);

        context.gpuResourceManager->BeginFrame(*context.renderer);
        struct ResourceFrameScope final
        {
            GPUResourceManager& manager;
            ~ResourceFrameScope()
            {
                manager.EndFrame();
            }
        } resourceFrameScope { *context.gpuResourceManager };

        if (context.syncSkyEnvironmentResources)
        {
            context.syncSkyEnvironmentResources();
        }

        std::uint32_t outputWidth = 1;
        std::uint32_t outputHeight = 1;
        ResolveSceneOutputSize(context, outputWidth, outputHeight);
        context.renderer->SetSceneOutputSize(outputWidth, outputHeight);

        if (!EnsureActivePipeline(context))
        {
            return false;
        }

        RebuildSceneRenderCacheIfNeeded(context);

        const auto sceneViewBuildStart = std::chrono::steady_clock::now();
        SceneViewBuildResult sceneViewBuildResult = BuildSceneView(BuildSceneViewInput(context, outputWidth, outputHeight));
        if (context.lastSceneViewBuildMs != nullptr)
        {
            *context.lastSceneViewBuildMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - sceneViewBuildStart)
                                                .count();
        }

        if (context.setLensSourceEntity)
        {
            context.setLensSourceEntity(sceneViewBuildResult.lensSourceEntity);
        }
        if (context.findPrimarySkyEntity && context.clearSkyRebuildRequested)
        {
            context.clearSkyRebuildRequested(context.findPrimarySkyEntity());
        }
        if (context.buildBlendedPostProcessView)
        {
            context.buildBlendedPostProcessView(
                sceneViewBuildResult.sceneView.cameraWorldPosition,
                sceneViewBuildResult.sceneView.postProcess);
        }

        if (sceneViewBuildResult.lensSourceEntity != entt::null &&
            context.scene != nullptr)
        {
            const auto& registry = context.scene->GetRegistry();
            if (registry.valid(sceneViewBuildResult.lensSourceEntity) &&
                registry.all_of<CameraComponent>(sceneViewBuildResult.lensSourceEntity))
            {
                const auto& camera = registry.get<CameraComponent>(sceneViewBuildResult.lensSourceEntity);
                if (!camera.allowPostProcess)
                {
                    sceneViewBuildResult.sceneView.postProcess = ScenePostProcessView {};
                }
                else
                {
                    sceneViewBuildResult.sceneView.postProcess.exposureCompensationEV += camera.exposure;
                }
            }
        }

        const auto renderFrameStart = std::chrono::steady_clock::now();
        (*context.activePipeline)->RenderFrame(*context.renderer, sceneViewBuildResult.sceneView);
        if (context.lastRenderFrameMs != nullptr)
        {
            *context.lastRenderFrameMs = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - renderFrameStart)
                                             .count();
        }

        return true;
    }

    void RenderFrameCoordinatorService::UpdateRendererBinding(RenderFrameCoordinatorContext& context) const
    {
        if (*context.lastRenderer != nullptr && *context.lastRenderer != context.renderer && context.onRendererChanged)
        {
            context.onRendererChanged();
        }
        *context.lastRenderer = context.renderer;
    }

    void RenderFrameCoordinatorService::ResolveSceneOutputSize(
        const RenderFrameCoordinatorContext& context,
        std::uint32_t& outWidth,
        std::uint32_t& outHeight) const
    {
        outWidth = static_cast<std::uint32_t>(std::max(context.viewportController->ViewportWidth(), 1.0f));
        outHeight = static_cast<std::uint32_t>(std::max(context.viewportController->ViewportHeight(), 1.0f));
        if (outWidth <= 1 && outHeight <= 1)
        {
            outWidth = 1280;
            outHeight = 720;
        }
    }

    bool RenderFrameCoordinatorService::EnsureActivePipeline(RenderFrameCoordinatorContext& context) const
    {
        if (!*context.activePipeline || context.desiredProfile != *context.activeProfile)
        {
            if (*context.activePipeline)
            {
                (*context.activePipeline)->Shutdown(*context.renderer, *context.gpuResourceManager);
                context.activePipeline->reset();
            }

            *context.activeProfile = context.desiredProfile;
            *context.activePipeline = CreateRenderPipeline(*context.activeProfile);
            if (!*context.activePipeline)
            {
                std::cerr << "Failed to create render pipeline." << '\n';
                return false;
            }

            if (!(*context.activePipeline)->Init(*context.renderer, *context.gpuResourceManager))
            {
                std::cerr << "Failed to initialize render pipeline: "
                          << (*context.activePipeline)->GetDebugName() << '\n';
                return false;
            }
        }

        (*context.activePipeline)->SetStreamingService(context.streamingService);
        return true;
    }

    void RenderFrameCoordinatorService::RebuildSceneRenderCacheIfNeeded(RenderFrameCoordinatorContext& context) const
    {
        const bool environmentDirty =
            HasAnySceneRenderCacheDirtyFlags(context.renderSceneCacheDirtyFlags, SceneRenderCacheDirtyFlags::Environment);

        std::string currentSkyMeshSignature;
        if (environmentDirty && context.findPrimarySkyEntity && context.buildSkyMeshSignature)
        {
            currentSkyMeshSignature = context.buildSkyMeshSignature(context.findPrimarySkyEntity());
        }
        else if (context.lastSkyMeshSignature != nullptr)
        {
            currentSkyMeshSignature = *context.lastSkyMeshSignature;
        }

        const bool viewportGridChanged = context.viewportController->ShowGrid() != context.lastViewportGridEnabled;
        const bool skyMeshChanged =
            context.lastSkyMeshSignature != nullptr && currentSkyMeshSignature != *context.lastSkyMeshSignature;
        const bool sceneGridMissing =
            context.viewportController->ShowGrid() ? !context.hasScenePrimitiveMesh : context.hasScenePrimitiveMesh;
        const bool skyMeshMissing = currentSkyMeshSignature.empty() ? context.hasSkyPrimitiveMesh : !context.hasSkyPrimitiveMesh;

        if (HasAnySceneRenderCacheDirtyFlags(context.renderSceneCacheDirtyFlags, SceneRenderCacheDirtyFlags::All) ||
            viewportGridChanged ||
            skyMeshChanged ||
            sceneGridMissing ||
            skyMeshMissing)
        {
            const auto sceneRebuildStart = std::chrono::steady_clock::now();
            if (context.rebuildScenePrimitiveMesh)
            {
                context.rebuildScenePrimitiveMesh();
            }
            if (context.lastSceneRebuildMs != nullptr)
            {
                *context.lastSceneRebuildMs = std::chrono::duration<float, std::milli>(
                    std::chrono::steady_clock::now() - sceneRebuildStart)
                                                  .count();
            }
        }
        else if (context.lastSceneRebuildMs != nullptr)
        {
            *context.lastSceneRebuildMs = 0.0f;
        }
    }

    SceneViewBuildInput RenderFrameCoordinatorService::BuildSceneViewInput(
        const RenderFrameCoordinatorContext& context,
        const std::uint32_t outputWidth,
        const std::uint32_t outputHeight) const
    {
        SceneViewBuildInput input {};
        input.scene = context.scene;
        input.rendererApi = context.renderer->GetAPI();
        input.timeSeconds = context.timeSeconds;
        input.outputWidth = outputWidth;
        input.outputHeight = outputHeight;
        input.previewSceneCameraLens = false;
        input.activeCameraEntity = context.activeCameraEntity;
        input.selectedEntity = context.selectedEntity;
        input.editorCamera.position = context.viewportController->Camera().position;
        input.editorCamera.yaw = context.viewportController->Camera().yaw;
        input.editorCamera.pitch = context.viewportController->Camera().pitch;
        input.skyMesh = context.hasSkyMesh ? context.skyMesh : nullptr;
        input.skyMeshRevision = context.skyMeshRevision;
        input.hasSkyMesh = context.hasSkyMesh;
        input.gridMesh = context.hasGridMesh ? context.gridMesh : nullptr;
        input.gridMeshRevision = context.gridMeshRevision;
        input.hasGridMesh = context.hasGridMesh;
        input.renderItems = context.renderItems;
        input.renderItemsRevision = context.renderItemsRevision;
        input.skyAverageColor = context.skyAverageColor;
        input.resolveSkyAssetPath = context.resolveSkyAssetPath;
        return input;
    }
}
