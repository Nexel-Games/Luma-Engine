#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Editor/Content/ContentBrowserController.h"
#include "Luma/Scene/Scene.h"

namespace Luma
{
    struct MaterialComponent;
    struct MeshRendererComponent;
}

namespace Luma::Editor
{
    class MaterialTextureAssetPickerPanel;
    class MaterialTextureAssetPickerService;

    struct InspectorMaterialPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
        const std::vector<ContentBrowserRootState>* contentRoots = nullptr;
        MaterialTextureAssetPickerService* pickerService = nullptr;
        MaterialTextureAssetPickerPanel* pickerPanel = nullptr;
        std::function<std::filesystem::path(const std::string&)> resolveAssetPath;
        std::function<void*(const std::filesystem::path&, bool)> getThumbnail;
        std::function<void()> markSceneMaterialsDirty;
        std::function<void(MaterialComponent&)> initializeDefaultMaterial;
        std::function<bool(const std::filesystem::path&, MaterialComponent&, std::string&)> loadMaterialAsset;
        std::function<void(std::string_view)> logMaterialWarning;
        std::function<std::vector<std::string>(const MeshRendererComponent&)> resolveMaterialSlotNames;
    };

    class InspectorMaterialPanel
    {
    public:
        void Draw(const InspectorMaterialPanelContext& context);
    };
}
