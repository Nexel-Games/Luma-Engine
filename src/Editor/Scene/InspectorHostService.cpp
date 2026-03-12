#include "Luma/Editor/Scene/InspectorHostService.h"

#include <algorithm>
#include <cctype>

#include "Luma/Asset/Core/MeshAssetIO.h"
#include "Luma/Scene/MaterialComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/PostProcessComponent.h"
#include "Luma/Scene/SkyLightComponent.h"

namespace Luma::Editor
{
    void InspectorHostService::Draw(const InspectorHostContext& context) const
    {
        if (context.panel == nullptr)
        {
            return;
        }

        context.panel->Draw({
            context.open,
            context.scene,
            context.selectedContentEntry,
            context.selectedEntity,
            context.luaScriptRuntime,
            context.playModeActive,
            context.physicsBackendName,
            context.physicsSimulationEnabled,
            context.contentRoots,
            context.materialTextureAssetPickerService,
            context.materialTextureAssetPickerPanel,
            context.entityPanel,
            context.meshRendererPanel,
            context.audioPanel,
            context.cameraLightingPanel,
            context.scriptPanel,
            context.destructionPanel,
            context.physicsPanel,
            context.jointPanel,
            context.advancedPhysicsPanel,
            context.vehiclePhysicsPanel,
            context.fieldBuoyancyPanel,
            context.physicsEventsPanel,
            context.environmentEffectsPanel,
            context.materialPanel,
            context.addComponentPanel,
            context.availableTags,
            [this, contentRoots = context.contentRoots]()
            {
                return ListContentRootPaths(contentRoots);
            },
            context.createPrefabFromEntity,
            context.applyPrefabInstance,
            context.revertPrefabInstance,
            context.getPrefabInstanceStatus,
            context.getPrefabOverridePaths,
            context.revertPrefabComponent,
            context.revertPrefabOverridePath,
            context.selectPrefabAsset,
            context.ensurePrimitiveCollider,
            context.markSceneRenderCacheDirty,
            context.markSceneMaterialsDirty,
            context.markSceneEnvironmentDirty,
            context.markSceneGeometryDirty,
            context.resolveAssetPath,
            context.getThumbnail,
            context.initializeDefaultMaterial,
            context.loadMaterialAsset,
            context.logMaterialWarning,
            [this, &context](const MeshRendererComponent& meshRenderer)
            {
                return ResolveMaterialSlotNames(context, meshRenderer);
            },
            context.initializeSkyLightDefaults,
            context.initializePostProcessDefaults,
            context.isSelectionValid,
            context.setContentStatus
        });
    }

    std::vector<std::filesystem::path> InspectorHostService::ListContentRootPaths(
        const std::vector<ContentBrowserRootState>* contentRoots) const
    {
        std::vector<std::filesystem::path> rootPaths;
        if (contentRoots == nullptr)
        {
            return rootPaths;
        }

        rootPaths.reserve(contentRoots->size());
        for (const ContentBrowserRootState& root : *contentRoots)
        {
            rootPaths.push_back(root.path);
        }
        return rootPaths;
    }

    std::vector<std::string> InspectorHostService::ResolveMaterialSlotNames(
        const InspectorHostContext& context,
        const MeshRendererComponent& meshRenderer) const
    {
        std::vector<std::string> slotNames;
        if (!meshRenderer.importedSceneSource.empty())
        {
            if (context.importedSceneParts != nullptr)
            {
                if (const auto importedIt = context.importedSceneParts->find(meshRenderer.importedSceneSource);
                    importedIt != context.importedSceneParts->end())
                {
                    slotNames.reserve(importedIt->second.parts.size());
                    for (const Assets::MeshScenePart& part : importedIt->second.parts)
                    {
                        slotNames.push_back(part.name);
                    }
                }
            }
        }
        else if (!meshRenderer.usePrimitive && !meshRenderer.meshSource.empty())
        {
            const std::filesystem::path resolvedMeshPath = Assets::ResolveMeshAssetPath(meshRenderer.meshSource);
            std::string extension = resolvedMeshPath.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch)
            {
                return static_cast<char>(std::tolower(ch));
            });
            if (!resolvedMeshPath.empty() && extension == ".lumamesh")
            {
                Assets::MeshAssetData meshAssetData;
                std::string meshAssetError;
                if (Assets::LoadMeshAssetData(resolvedMeshPath, meshAssetData, meshAssetError))
                {
                    slotNames = meshAssetData.materialSlotNames;
                }
            }
        }

        return slotNames;
    }
}
