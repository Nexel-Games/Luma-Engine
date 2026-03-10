#pragma once

#include <functional>
#include <string>
#include <vector>

#include "Luma/Asset/Package/PackageTypes.h"

namespace Luma::Assets
{
    class IPackageManager
    {
    public:
        using ProgressCallback = std::function<void(const PackageProgressEvent&)>;

        virtual ~IPackageManager() = default;

        virtual bool RefreshRegistrySources(std::string& outError) = 0;
        virtual PackageSearchResult Search(const PackageSearchQuery& query) const = 0;
        virtual bool GetPackage(const std::string& packageId, PackageDetails& outDetails, std::string& outError) = 0;

        virtual PackageOperationResult PreflightInstall(const PackageInstallRequest& request) = 0;
        virtual PackageOperationResult Install(const PackageInstallRequest& request) = 0;
        virtual PackageOperationResult Update(const std::string& packageId, const std::string& version) = 0;
        virtual PackageOperationResult Remove(const std::string& packageId) = 0;
        virtual PackageOperationResult Verify(const std::string& packageIdOrEmpty) = 0;

        virtual std::vector<InstalledPackageRecord> GetInstalled() const = 0;
        virtual std::vector<PackageMountRoot> GetMountRoots() const = 0;
        virtual std::vector<std::string> GetOperationLog() const = 0;
        virtual const PackageManifest& GetManifest() const = 0;
        virtual const PackageLock& GetLock() const = 0;

        virtual void SetProgressCallback(ProgressCallback callback) = 0;
    };
}
