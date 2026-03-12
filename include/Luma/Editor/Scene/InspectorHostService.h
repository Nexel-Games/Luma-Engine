#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Luma/Editor/Assets/MeshStreamingGeometryService.h"
#include "Luma/Editor/Content/ContentBrowserController.h"
#include "Luma/Editor/Panels/Inspector/InspectorPanel.h"

namespace Luma
{
    struct MaterialComponent;
    struct MeshRendererComponent;
    struct PostProcessComponent;
    struct SkyLightComponent;
}

namespace Luma::Editor
{
    class InspectorAddComponentPanel;
    class InspectorAdvancedPhysicsPanel;
    class InspectorCameraLightingPanel;
    class InspectorDestructionPanel;
    class InspectorEntityPanel;
    class InspectorEnvironmentEffectsPanel;
    class InspectorFieldBuoyancyPanel;
    class InspectorJointPanel;
    class InspectorMaterialPanel;
    class InspectorMeshRendererPanel;
    class InspectorPhysicsEventsPanel;
    class InspectorPhysicsPanel;
    class InspectorScriptPanel;
    class InspectorVehiclePhysicsPanel;
    class MaterialTextureAssetPickerPanel;
    class MaterialTextureAssetPickerService;

    struct InspectorHostContext
    {
        bool* open = nullptr;
        Scene* scene = nullptr;
        const std::filesystem::path* selectedContentEntry = nullptr;
        EntityID selectedEntity = entt::null;
        std::string_view physicsBackendName;
        bool* physicsSimulationEnabled = nullptr;
        const std::vector<ContentBrowserRootState>* contentRoots = nullptr;
        const std::unordered_map<std::string, MeshStreamingImportedScenePartsState>* importedSceneParts = nullptr;
        const std::vector<std::string>* availableTags = nullptr;
        MaterialTextureAssetPickerService* materialTextureAssetPickerService = nullptr;
        MaterialTextureAssetPickerPanel* materialTextureAssetPickerPanel = nullptr;
        InspectorPanel* panel = nullptr;
        InspectorEntityPanel* entityPanel = nullptr;
        InspectorMeshRendererPanel* meshRendererPanel = nullptr;
        InspectorCameraLightingPanel* cameraLightingPanel = nullptr;
        InspectorScriptPanel* scriptPanel = nullptr;
        InspectorPhysicsPanel* physicsPanel = nullptr;
        InspectorDestructionPanel* destructionPanel = nullptr;
        InspectorJointPanel* jointPanel = nullptr;
        InspectorAdvancedPhysicsPanel* advancedPhysicsPanel = nullptr;
        InspectorVehiclePhysicsPanel* vehiclePhysicsPanel = nullptr;
        InspectorFieldBuoyancyPanel* fieldBuoyancyPanel = nullptr;
        InspectorPhysicsEventsPanel* physicsEventsPanel = nullptr;
        InspectorEnvironmentEffectsPanel* environmentEffectsPanel = nullptr;
        InspectorMaterialPanel* materialPanel = nullptr;
        InspectorAddComponentPanel* addComponentPanel = nullptr;
        std::function<void(EntityID, PrimitiveType)> ensurePrimitiveCollider;
        std::function<void()> markSceneRenderCacheDirty;
        std::function<void()> markSceneMaterialsDirty;
        std::function<void()> markSceneEnvironmentDirty;
        std::function<void()> markSceneGeometryDirty;
        std::function<std::filesystem::path(const std::string&)> resolveAssetPath;
        std::function<void*(const std::filesystem::path&, bool)> getThumbnail;
        std::function<void(MaterialComponent&)> initializeDefaultMaterial;
        std::function<bool(const std::filesystem::path&, MaterialComponent&, std::string&)> loadMaterialAsset;
        std::function<void(std::string_view)> logMaterialWarning;
        std::function<void(SkyLightComponent&)> initializeSkyLightDefaults;
        std::function<void(PostProcessComponent&)> initializePostProcessDefaults;
        std::function<bool()> isSelectionValid;
        std::function<void(std::string)> setContentStatus;
    };

    class InspectorHostService
    {
    public:
        void Draw(const InspectorHostContext& context) const;

    private:
        std::vector<std::filesystem::path> ListContentRootPaths(
            const std::vector<ContentBrowserRootState>* contentRoots) const;
        std::vector<std::string> ResolveMaterialSlotNames(
            const InspectorHostContext& context,
            const MeshRendererComponent& meshRenderer) const;
    };
}
