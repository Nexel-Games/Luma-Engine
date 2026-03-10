#pragma once

#include <string>
#include <vector>

#include "Luma/Asset/Package/PackageTypes.h"

namespace Luma::Assets
{
    class IPackageRegistryProvider
    {
    public:
        virtual ~IPackageRegistryProvider() = default;

        virtual bool FetchIndex(
            const std::vector<PackageRegistrySource>& sources,
            std::vector<PackageCatalogEntry>& outEntries,
            std::string& outError) = 0;

        virtual bool FetchManifest(
            const std::vector<PackageRegistrySource>& sources,
            const std::string& packageId,
            std::vector<PackageVersionInfo>& outVersions,
            std::string& outError) = 0;
    };
}

