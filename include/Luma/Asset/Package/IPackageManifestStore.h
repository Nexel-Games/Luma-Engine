#pragma once

#include <filesystem>
#include <string>

#include "Luma/Asset/Package/PackageTypes.h"

namespace Luma::Assets
{
    class IPackageManifestStore
    {
    public:
        virtual ~IPackageManifestStore() = default;

        virtual bool Load(
            const std::filesystem::path& packagesRoot,
            PackageManifest& outManifest,
            PackageLock& outLock,
            bool& outMigratedLegacyLock,
            std::string& outError) = 0;

        virtual bool Save(
            const std::filesystem::path& packagesRoot,
            const PackageManifest& manifest,
            const PackageLock& lock,
            std::string& outError) = 0;
    };
}

