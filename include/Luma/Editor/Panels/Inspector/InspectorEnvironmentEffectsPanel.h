#pragma once

#include <filesystem>
#include <functional>

#include "Luma/Editor/Content/ContentBrowserController.h"

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    class MaterialTextureAssetPickerPanel;
    class MaterialTextureAssetPickerService;

    struct InspectorEnvironmentEffectsPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
        const std::vector<ContentBrowserRootState>* contentRoots = nullptr;
        MaterialTextureAssetPickerService* materialTextureAssetPickerService = nullptr;
        MaterialTextureAssetPickerPanel* materialTextureAssetPickerPanel = nullptr;
        std::function<std::filesystem::path(const std::string&)> resolveAssetPath;
        std::function<void*(const std::filesystem::path&, bool)> getThumbnail;
        std::function<void()> onEnvironmentChanged;
    };

    class InspectorEnvironmentEffectsPanel
    {
    public:
        void Draw(const InspectorEnvironmentEffectsPanelContext& context);
    };
}
