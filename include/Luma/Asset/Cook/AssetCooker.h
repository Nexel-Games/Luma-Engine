#pragma once

#include <filesystem>
#include <string>

#include "Luma/Asset/Core/AssetRegistry.h"

namespace Luma::Assets
{
    class AssetCooker
    {
    public:
        bool Initialize(const std::filesystem::path& projectRoot, std::string& outError);
        bool CookAll(std::string_view platformName, std::string& outReport);

        const std::filesystem::path& GetProjectRoot() const;

    private:
        std::filesystem::path ResolveAbsolute(const std::filesystem::path& path) const;

        std::filesystem::path m_ProjectRoot;
        AssetRegistry m_Registry;
        bool m_Initialized = false;
    };
}

