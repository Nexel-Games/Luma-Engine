#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Luma::Editor
{
    class MeshAssetPickerService;

    struct MeshAssetPickerPanelContext
    {
        MeshAssetPickerService* pickerService = nullptr;
        const std::vector<std::filesystem::path>* rootPaths = nullptr;
        bool projectLoaded = false;
        std::filesystem::path projectAssetsPath;
    };

    class MeshAssetPickerPanel
    {
    public:
        void Draw(const MeshAssetPickerPanelContext& context, std::string& meshSource);
    };
}
