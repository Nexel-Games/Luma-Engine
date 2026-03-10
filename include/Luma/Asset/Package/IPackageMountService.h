#pragma once

#include <vector>

#include "Luma/Asset/Package/PackageTypes.h"

namespace Luma::Assets
{
    class IPackageMountService
    {
    public:
        virtual ~IPackageMountService() = default;

        virtual void Refresh(const std::vector<InstalledPackageRecord>& installed) = 0;
        virtual std::vector<PackageMountRoot> GetMountRoots() const = 0;
    };
}

