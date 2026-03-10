#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "Luma/Asset/Package/PackageTypes.h"

namespace Luma::Assets
{
    class IPackageResolver
    {
    public:
        virtual ~IPackageResolver() = default;

        virtual PackageInstallPlan ResolveInstallPlan(
            const PackageInstallRequest& request,
            const std::vector<InstalledPackageRecord>& installedPackages,
            const std::vector<PackageCatalogEntry>& catalog,
            const std::unordered_map<std::string, std::vector<PackageVersionInfo>>& manifestCache,
            const std::string& engineVersionNormalized) = 0;
    };
}

