#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Luma::Editor
{
    struct MeshAssetPickerState
    {
        std::vector<std::filesystem::path> entries;
        std::string query;
    };

    class MeshAssetPickerService
    {
    public:
        MeshAssetPickerState& State();
        const MeshAssetPickerState& State() const;

        void RefreshEntries(const std::vector<std::filesystem::path>& rootPaths);

        static bool IsMeshAssetPathCandidate(const std::filesystem::path& path);

    private:
        MeshAssetPickerState m_State;
    };
}
