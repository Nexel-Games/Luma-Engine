#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Luma::Assets
{
    using PackageId = std::string;
    using PackageVersion = std::string;

    struct PackageRef
    {
        PackageId id;
        PackageVersion version;
    };

    struct PackageDependency
    {
        PackageId id;
        std::string constraint;
    };

    struct PackageVersionInfo
    {
        PackageVersion version;
        std::string url;
        std::string sha256;
        std::uint64_t sizeBytes = 0;
        std::string description;
        std::string published;
        std::string category;
        std::vector<PackageDependency> dependencies;
        std::string engineConstraint;
        std::string engineMin;
        std::string engineMax;
    };

    struct PackageCatalogEntry
    {
        PackageId id;
        std::string latest;
        std::string category;
        std::vector<PackageVersion> versions;
    };

    struct PackageDetails
    {
        PackageCatalogEntry catalog;
        std::vector<PackageVersionInfo> versions;
        bool hasManifest = false;
    };

    struct PackageSearchQuery
    {
        std::string text;
        std::string category;
    };

    struct PackageSearchResult
    {
        bool success = false;
        std::string message;
        std::vector<PackageCatalogEntry> packages;
    };

    enum class PackageProgressStage : std::uint8_t
    {
        Idle = 0,
        RefreshingRegistry,
        ResolvingDependencies,
        Downloading,
        Verifying,
        Extracting,
        Committing,
        Mounting,
        HotLoading,
        Removing,
        Updating,
        VerifyingInstalled
    };

    struct PackageProgressEvent
    {
        PackageProgressStage stage = PackageProgressStage::Idle;
        float progress01 = 0.0f;
        std::string message;
        std::string packageId;
    };

    struct PackageOperationResult
    {
        bool success = false;
        bool restartRequired = false;
        std::string message;
        std::vector<std::string> diagnostics;
        std::vector<PackageRef> affectedPackages;
    };

    struct PackageInstallRequest
    {
        PackageId id;
        PackageVersion version;
        bool importSamplesToAssets = false;
        bool attemptHotLoad = true;
    };

    struct PackageInstallAction
    {
        PackageRef package;
        PackageVersionInfo versionInfo;
        bool alreadyInstalled = false;
    };

    struct PackageInstallPlan
    {
        bool success = false;
        std::string message;
        std::vector<std::string> diagnostics;
        std::vector<PackageInstallAction> actions;
    };

    struct PackageRegistrySource
    {
        std::string id;
        std::string location;
        bool enabled = true;
        bool preferLocal = false;
    };

    struct PackageRequirement
    {
        PackageId id;
        std::string versionConstraint;
    };

    struct InstalledPackageRecord
    {
        struct EditorToolMetadata
        {
            bool enabled = false;
            std::string displayName;
            std::filesystem::path entryRelativePath;
            std::filesystem::path workingDirectoryRelativePath;
        };

        PackageRef package;
        std::string source;
        std::string sha256;
        std::uint64_t sizeBytes = 0;
        std::filesystem::path installPath;
        std::filesystem::path cachePath;
        std::vector<PackageDependency> dependencies;
        std::string displayName;
        std::string description;
        EditorToolMetadata editorTool;
        bool restartRequired = false;
    };

    struct PackageManifest
    {
        int schemaVersion = 1;
        std::vector<PackageRegistrySource> sources;
        std::vector<PackageRequirement> dependencies;
    };

    struct PackageLock
    {
        int schemaVersion = 1;
        std::vector<InstalledPackageRecord> packages;
        std::vector<std::string> legacyEntries;
    };

    struct PackageMountRoot
    {
        std::string displayName;
        PackageId packageId;
        PackageVersion version;
        std::filesystem::path path;
        bool readOnly = true;
        std::string kind;
    };
}
