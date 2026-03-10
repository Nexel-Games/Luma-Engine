#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Luma/Asset/Package/IPackageInstaller.h"
#include "Luma/Asset/Package/IPackageManager.h"
#include "Luma/Asset/Package/IPackageManifestStore.h"
#include "Luma/Asset/Package/IPackageModuleLoader.h"
#include "Luma/Asset/Package/IPackageMountService.h"
#include "Luma/Asset/Package/IPackageRegistryProvider.h"
#include "Luma/Asset/Package/IPackageResolver.h"

namespace Luma::Assets
{
    class PackageManager final : public IPackageManager
    {
    public:
        PackageManager();
        ~PackageManager() override;

        bool Initialize(const std::filesystem::path& projectRoot, std::string& outError);
        bool InstallPackage(const std::filesystem::path& packageSource, std::string& outMessage);

        bool RefreshRegistrySources(std::string& outError) override;
        PackageSearchResult Search(const PackageSearchQuery& query) const override;
        bool GetPackage(const std::string& packageId, PackageDetails& outDetails, std::string& outError) override;

        PackageOperationResult PreflightInstall(const PackageInstallRequest& request) override;
        PackageOperationResult Install(const PackageInstallRequest& request) override;
        PackageOperationResult Update(const std::string& packageId, const std::string& version) override;
        PackageOperationResult Remove(const std::string& packageId) override;
        PackageOperationResult Verify(const std::string& packageIdOrEmpty) override;

        std::vector<InstalledPackageRecord> GetInstalled() const override;
        std::vector<PackageMountRoot> GetMountRoots() const override;
        std::vector<std::string> GetOperationLog() const override;
        const PackageManifest& GetManifest() const override;
        const PackageLock& GetLock() const override;
        void SetProgressCallback(ProgressCallback callback) override;

    private:
        void AppendLog(const std::string& line);
        std::filesystem::path ResolveGlobalCacheRoot() const;
        bool EnsureManifestForPackage(const std::string& packageId, std::string& outError);
        bool SaveState(std::string& outError);
        void EmitProgress(PackageProgressStage stage, float progress01, const std::string& message, const std::string& packageId);
        std::string NormalizeEngineVersion(const std::string& value) const;
        bool RebuildMounts();
        bool EnsureInitialized(std::string& outError) const;
        PackageOperationResult InstallFromArchive(const std::filesystem::path& archivePath);

        std::filesystem::path m_ProjectRoot;
        std::filesystem::path m_PackagesRoot;
        std::filesystem::path m_ProjectCacheRoot;
        std::filesystem::path m_ProjectInstallRoot;
        std::filesystem::path m_GlobalCacheRoot;
        bool m_Initialized = false;

        PackageManifest m_Manifest {};
        PackageLock m_Lock {};
        bool m_LoadedLegacyLock = false;
        mutable std::unordered_map<std::string, std::vector<PackageVersionInfo>> m_ManifestCache;
        std::vector<PackageCatalogEntry> m_Catalog;
        std::vector<std::string> m_OperationLog;
        ProgressCallback m_ProgressCallback;

        std::unique_ptr<IPackageRegistryProvider> m_RegistryProvider;
        std::unique_ptr<IPackageResolver> m_Resolver;
        std::unique_ptr<IPackageInstaller> m_Installer;
        std::unique_ptr<IPackageManifestStore> m_ManifestStore;
        std::unique_ptr<IPackageMountService> m_MountService;
        std::unique_ptr<IPackageModuleLoader> m_ModuleLoader;
    };
}
