#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Luma/Asset/Package/PackageTypes.h"

namespace Luma::Assets
{
    class IPackageInstaller
    {
    public:
        using ProgressCallback = std::function<void(const PackageProgressEvent&)>;

        virtual ~IPackageInstaller() = default;

        virtual PackageOperationResult Install(
            const PackageInstallPlan& plan,
            const std::filesystem::path& globalCacheRoot,
            const std::filesystem::path& projectCacheRoot,
            const std::filesystem::path& projectInstallRoot,
            ProgressCallback progressCallback) = 0;

        virtual PackageOperationResult Remove(
            const std::string& packageId,
            const std::vector<InstalledPackageRecord>& installed,
            const std::filesystem::path& projectInstallRoot,
            ProgressCallback progressCallback) = 0;

        virtual PackageOperationResult VerifyInstalled(
            const std::string& packageIdOrEmpty,
            const std::vector<InstalledPackageRecord>& installed,
            ProgressCallback progressCallback) = 0;
    };
}

