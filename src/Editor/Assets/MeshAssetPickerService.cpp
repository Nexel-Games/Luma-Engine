#include "Luma/Editor/Assets/MeshAssetPickerService.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace Luma::Editor
{
    namespace
    {
        std::string ToLowerString(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }
    }

    MeshAssetPickerState& MeshAssetPickerService::State()
    {
        return m_State;
    }

    const MeshAssetPickerState& MeshAssetPickerService::State() const
    {
        return m_State;
    }

    void MeshAssetPickerService::RefreshEntries(const std::vector<std::filesystem::path>& rootPaths)
    {
        m_State.entries.clear();

        std::error_code ec;
        for (const std::filesystem::path& rootPath : rootPaths)
        {
            if (rootPath.empty() || !std::filesystem::exists(rootPath, ec))
            {
                continue;
            }

            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     rootPath,
                     std::filesystem::directory_options::skip_permission_denied,
                     ec))
            {
                if (ec)
                {
                    break;
                }
                if (!entry.is_regular_file(ec))
                {
                    continue;
                }
                if (!IsMeshAssetPathCandidate(entry.path()))
                {
                    continue;
                }
                m_State.entries.push_back(entry.path().lexically_normal());
            }
        }

        std::sort(
            m_State.entries.begin(),
            m_State.entries.end(),
            [](const std::filesystem::path& lhs, const std::filesystem::path& rhs)
            {
                return ToLowerString(lhs.generic_string()) < ToLowerString(rhs.generic_string());
            });
        m_State.entries.erase(
            std::unique(m_State.entries.begin(), m_State.entries.end()),
            m_State.entries.end());
    }

    bool MeshAssetPickerService::IsMeshAssetPathCandidate(const std::filesystem::path& path)
    {
        const std::string extension = ToLowerString(path.extension().string());
        return extension == ".lumamesh" ||
            extension == ".obj" ||
            extension == ".fbx" ||
            extension == ".gltf" ||
            extension == ".glb";
    }
}
