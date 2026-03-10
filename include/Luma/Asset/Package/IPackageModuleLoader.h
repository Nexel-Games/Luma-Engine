#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Luma::Assets
{
    class IPackageModuleLoader
    {
    public:
        virtual ~IPackageModuleLoader() = default;

        virtual bool TryHotLoadPackage(
            const std::string& packageId,
            const std::filesystem::path& installRoot,
            std::vector<std::string>& outWarnings) = 0;

        virtual bool UnloadPackage(
            const std::string& packageId,
            std::vector<std::string>& outWarnings) = 0;
    };
}

