#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Editor/Content/ContentBrowserController.h"

namespace Luma::Editor
{
    class MaterialTextureAssetPickerService;

    struct MaterialTextureAssetPickerPanelContext
    {
        MaterialTextureAssetPickerService* pickerService = nullptr;
        const std::vector<ContentBrowserRootState>* roots = nullptr;
        bool projectLoaded = false;
        std::filesystem::path projectAssetsPath;
        std::function<std::filesystem::path(const std::string&)> resolveAssetPath;
        std::function<void*(const std::filesystem::path&, bool)> getThumbnail;
        std::function<void()> markSceneRenderCacheDirty;
    };

    class MaterialTextureAssetPickerPanel
    {
    public:
        void DrawMaterialSelector(
            const MaterialTextureAssetPickerPanelContext& context,
            const char* label,
            std::string& value,
            const char* popupId,
            std::string_view tooltip);
        void DrawTextureSelector(
            const MaterialTextureAssetPickerPanelContext& context,
            const char* label,
            std::string& value,
            const char* popupId,
            std::string_view tooltip);
    };
}
