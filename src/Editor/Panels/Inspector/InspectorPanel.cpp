#include "Luma/Editor/Panels/Inspector/InspectorPanel.h"

#include <imgui.h>

#include "Luma/Editor/Panels/Inspector/InspectorAddComponentPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorAdvancedPhysicsPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorCameraLightingPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorEntityPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorEnvironmentEffectsPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorFieldBuoyancyPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorJointPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorMaterialPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorMeshRendererPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorPhysicsEventsPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorPhysicsPanel.h"
#include "Luma/Editor/Panels/Inspector/InspectorVehiclePhysicsPanel.h"
#include "Luma/Scene/IDComponent.h"
#include "Luma/Scene/MaterialComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/PostProcessComponent.h"
#include "Luma/Scene/RelationshipComponent.h"
#include "Luma/Scene/SkyLightComponent.h"
#include "Luma/Scene/TagComponent.h"
#include "Luma/Scene/TransformComponent.h"

namespace Luma::Editor
{
    void InspectorPanel::Draw(const InspectorPanelContext& context)
    {
        if (context.scene == nullptr)
        {
            return;
        }

        if (!ImGui::Begin("Inspector", context.open))
        {
            ImGui::End();
            return;
        }

        std::error_code assetInspectorEc;
        if (context.selectedContentEntry != nullptr &&
            !context.selectedContentEntry->empty() &&
            std::filesystem::is_regular_file(*context.selectedContentEntry, assetInspectorEc) &&
            context.selectedContentEntry->extension() == ".lumamat")
        {
            ImGui::TextDisabled("Material asset editing is temporarily disabled.");
            ImGui::TextWrapped(
                "The previous material system has been removed so the renderer can be rebuilt on a cleaner base.");
            ImGui::End();
            return;
        }

        auto& registry = context.scene->GetRegistry();
        const bool selectionValid = context.isSelectionValid ? context.isSelectionValid() : false;
        if (!selectionValid ||
            !registry.all_of<TagComponent, TransformComponent, RelationshipComponent, IDComponent>(context.selectedEntity))
        {
            ImGui::TextDisabled("Select an entity from Hierarchy.");
            ImGui::End();
            return;
        }

        if (context.entityPanel != nullptr)
        {
            context.entityPanel->Draw({
                context.scene,
                context.selectedEntity,
                context.physicsBackendName,
                context.physicsSimulationEnabled
            });
        }

        if (context.meshRendererPanel != nullptr)
        {
            context.meshRendererPanel->Draw({
                context.scene,
                context.selectedEntity,
                context.listContentRootPaths,
                context.ensurePrimitiveCollider,
                context.markSceneGeometryDirty,
                context.markSceneMaterialsDirty
            });
        }

        if (context.cameraLightingPanel != nullptr)
        {
            context.cameraLightingPanel->Draw({
                context.scene,
                context.selectedEntity
            });
        }

        if (context.physicsPanel != nullptr)
        {
            context.physicsPanel->Draw({
                context.scene,
                context.selectedEntity
            });
        }

        if (context.jointPanel != nullptr)
        {
            context.jointPanel->Draw({
                context.scene,
                context.selectedEntity
            });
        }

        if (context.advancedPhysicsPanel != nullptr)
        {
            context.advancedPhysicsPanel->Draw({
                context.scene,
                context.selectedEntity
            });
        }

        if (context.vehiclePhysicsPanel != nullptr)
        {
            context.vehiclePhysicsPanel->Draw({
                context.scene,
                context.selectedEntity
            });
        }

        if (context.fieldBuoyancyPanel != nullptr)
        {
            context.fieldBuoyancyPanel->Draw({
                context.scene,
                context.selectedEntity
            });
        }

        if (context.physicsEventsPanel != nullptr)
        {
            context.physicsEventsPanel->Draw({
                context.scene,
                context.selectedEntity
            });
        }

        if (context.environmentEffectsPanel != nullptr)
        {
            context.environmentEffectsPanel->Draw({
                context.scene,
                context.selectedEntity,
                context.contentRoots,
                context.materialTextureAssetPickerService,
                context.materialTextureAssetPickerPanel,
                context.resolveAssetPath,
                context.getThumbnail,
                context.markSceneEnvironmentDirty
            });
        }

        if (context.materialPanel != nullptr)
        {
            context.materialPanel->Draw({
                context.scene,
                context.selectedEntity,
                context.contentRoots,
                context.materialTextureAssetPickerService,
                context.materialTextureAssetPickerPanel,
                context.resolveAssetPath,
                context.getThumbnail,
                context.markSceneMaterialsDirty,
                context.initializeDefaultMaterial,
                context.loadMaterialAsset,
                context.logMaterialWarning,
                context.resolveMaterialSlotNames
            });
        }

        if (context.addComponentPanel != nullptr)
        {
            context.addComponentPanel->Draw({
                context.scene,
                context.selectedEntity,
                context.initializeDefaultMaterial,
                context.initializeSkyLightDefaults,
                context.initializePostProcessDefaults,
                context.ensurePrimitiveCollider,
                context.markSceneGeometryDirty,
                context.markSceneMaterialsDirty,
                context.markSceneEnvironmentDirty
            });
        }

        ImGui::End();
    }
}
