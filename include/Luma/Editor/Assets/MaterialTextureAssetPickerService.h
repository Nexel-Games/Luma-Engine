#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Luma/Editor/Content/ContentBrowserController.h"

namespace Luma::Editor
{
    struct AssetPickerState
    {
        std::vector<std::filesystem::path> entries;
        std::string query;
        std::string selection;
    };

    class MaterialTextureAssetPickerService
    {
    public:
        AssetPickerState& Material();
        const AssetPickerState& Material() const;

        AssetPickerState& Texture();
        const AssetPickerState& Texture() const;

        void RefreshMaterialEntries(const std::vector<ContentBrowserRootState>& roots);
        void RefreshTextureEntries(const std::vector<ContentBrowserRootState>& roots);

        static bool IsMaterialAssetPathCandidate(const std::filesystem::path& path);
        static bool IsTextureAssetPathCandidate(const std::filesystem::path& path);

    private:
        static void RefreshEntries(
            AssetPickerState& state,
            const std::vector<ContentBrowserRootState>& roots,
            const std::function<bool(const std::filesystem::path&)>& isCandidate);

        AssetPickerState m_Material;
        AssetPickerState m_Texture;
    };
}
