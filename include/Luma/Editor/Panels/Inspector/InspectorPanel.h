#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Editor/Content/ContentBrowserController.h"
#include "Luma/Renderer/PrimitiveMeshFactory.h"
#include "Luma/Scene/Scene.h"

namespace Luma
{
    struct MaterialComponent;
    struct MeshRendererComponent;
    struct PostProcessComponent;
    struct SkyLightComponent;
    class LuaScriptRuntime;
}

namespace Luma::Editor
{
    class InspectorAddComponentPanel;
    class InspectorAdvancedPhysicsPanel;
    class InspectorAudioPanel;
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

    struct InspectorPanelContext
    {
        bool* open = nullptr;
        Scene* scene = nullptr;
        const std::filesystem::path* selectedContentEntry = nullptr;
        EntityID selectedEntity = entt::null;
        ::Luma::LuaScriptRuntime* luaScriptRuntime = nullptr;
        bool playModeActive = false;
        std::string_view physicsBackendName;
        bool* physicsSimulationEnabled = nullptr;
        const std::vector<ContentBrowserRootState>* contentRoots = nullptr;
        MaterialTextureAssetPickerService* materialTextureAssetPickerService = nullptr;
        MaterialTextureAssetPickerPanel* materialTextureAssetPickerPanel = nullptr;
        InspectorEntityPanel* entityPanel = nullptr;
        InspectorMeshRendererPanel* meshRendererPanel = nullptr;
        InspectorAudioPanel* audioPanel = nullptr;
        InspectorCameraLightingPanel* cameraLightingPanel = nullptr;
        InspectorScriptPanel* scriptPanel = nullptr;
        InspectorDestructionPanel* destructionPanel = nullptr;
        InspectorPhysicsPanel* physicsPanel = nullptr;
        InspectorJointPanel* jointPanel = nullptr;
        InspectorAdvancedPhysicsPanel* advancedPhysicsPanel = nullptr;
        InspectorVehiclePhysicsPanel* vehiclePhysicsPanel = nullptr;
        InspectorFieldBuoyancyPanel* fieldBuoyancyPanel = nullptr;
        InspectorPhysicsEventsPanel* physicsEventsPanel = nullptr;
        InspectorEnvironmentEffectsPanel* environmentEffectsPanel = nullptr;
        InspectorMaterialPanel* materialPanel = nullptr;
        InspectorAddComponentPanel* addComponentPanel = nullptr;
        const std::vector<std::string>* availableTags = nullptr;
        const std::vector<std::string>* availableLayers = nullptr;
        std::function<std::vector<std::filesystem::path>()> listContentRootPaths;
        std::function<bool(EntityID)> createPrefabFromEntity;
        std::function<bool(EntityID)> applyPrefabInstance;
        std::function<bool(EntityID)> revertPrefabInstance;
        std::function<std::string(EntityID)> getPrefabInstanceStatus;
        std::function<std::vector<std::string>(EntityID)> getPrefabOverridePaths;
        std::function<bool(EntityID, std::string_view)> revertPrefabComponent;
        std::function<bool(EntityID, std::string_view)> revertPrefabOverridePath;
        std::function<void(EntityID)> selectPrefabAsset;
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
        std::function<std::vector<std::string>(const MeshRendererComponent&)> resolveMaterialSlotNames;
        std::function<void(SkyLightComponent&)> initializeSkyLightDefaults;
        std::function<void(PostProcessComponent&)> initializePostProcessDefaults;
        std::function<bool()> isSelectionValid;
        std::function<void(std::string)> setContentStatus;
    };

    class InspectorPanel
    {
    public:
        void Draw(const InspectorPanelContext& context);
    };
}
