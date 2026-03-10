#include "Luma/Editor/Assets/MaterialTextureAssetPickerService.h"

#include <algorithm>
#include <cctype>
#include <functional>
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

    AssetPickerState& MaterialTextureAssetPickerService::Material()
    {
        return m_Material;
    }

    const AssetPickerState& MaterialTextureAssetPickerService::Material() const
    {
        return m_Material;
    }

    AssetPickerState& MaterialTextureAssetPickerService::Texture()
    {
        return m_Texture;
    }

    const AssetPickerState& MaterialTextureAssetPickerService::Texture() const
    {
        return m_Texture;
    }

    void MaterialTextureAssetPickerService::RefreshMaterialEntries(const std::vector<ContentBrowserRootState>& roots)
    {
        RefreshEntries(m_Material, roots, IsMaterialAssetPathCandidate);
    }

    void MaterialTextureAssetPickerService::RefreshTextureEntries(const std::vector<ContentBrowserRootState>& roots)
    {
        RefreshEntries(m_Texture, roots, IsTextureAssetPathCandidate);
    }

    bool MaterialTextureAssetPickerService::IsMaterialAssetPathCandidate(const std::filesystem::path& path)
    {
        const std::string extension = ToLowerString(path.extension().string());
        return extension == ".material" ||
            extension == ".mat" ||
            extension == ".lumamat" ||
            extension == ".mtl" ||
            extension == ".luma_material";
    }

    bool MaterialTextureAssetPickerService::IsTextureAssetPathCandidate(const std::filesystem::path& path)
    {
        const std::string extension = ToLowerString(path.extension().string());
        return extension == ".png" ||
            extension == ".jpg" ||
            extension == ".jpeg" ||
            extension == ".tga" ||
            extension == ".bmp" ||
            extension == ".dds" ||
            extension == ".hdr" ||
            extension == ".exr" ||
            extension == ".ktx2" ||
            extension == ".lumatex" ||
            extension == ".lumasky";
    }

    void MaterialTextureAssetPickerService::RefreshEntries(
        AssetPickerState& state,
        const std::vector<ContentBrowserRootState>& roots,
        const std::function<bool(const std::filesystem::path&)>& isCandidate)
    {
        state.entries.clear();

        std::error_code ec;
        for (const ContentBrowserRootState& root : roots)
        {
            if (root.path.empty() || !std::filesystem::exists(root.path, ec))
            {
                continue;
            }

            for (const auto& entry : std::filesystem::recursive_directory_iterator(
                     root.path,
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
                if (!isCandidate(entry.path()))
                {
                    continue;
                }
                state.entries.push_back(entry.path().lexically_normal());
            }
        }

        std::sort(
            state.entries.begin(),
            state.entries.end(),
            [](const std::filesystem::path& lhs, const std::filesystem::path& rhs)
            {
                return ToLowerString(lhs.generic_string()) < ToLowerString(rhs.generic_string());
            });
        state.entries.erase(
            std::unique(state.entries.begin(), state.entries.end()),
            state.entries.end());
    }
}
