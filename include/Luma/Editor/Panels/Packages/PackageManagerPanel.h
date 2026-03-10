#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Luma/Asset/Package/PackageManager.h"
#include "Luma/Editor/Core/EditorTaskManager.h"

namespace Luma::Editor
{
    class PackageManagerPanel
    {
    public:
        void Reset();

        bool EnsureInitialized(const std::filesystem::path& projectRoot);
        bool RefreshRegistry(const std::filesystem::path& projectRoot);
        bool RequestRefresh(const std::filesystem::path& projectRoot);
        bool RequestInstall(
            const std::filesystem::path& projectRoot,
            const Assets::PackageInstallRequest& request);
        bool RequestUpdate(
            const std::filesystem::path& projectRoot,
            const std::string& packageId,
            const std::string& version);
        bool RequestRemove(
            const std::filesystem::path& projectRoot,
            const std::string& packageId);
        bool RequestVerify(
            const std::filesystem::path& projectRoot,
            const std::string& packageIdOrEmpty);
        void Tick(const std::filesystem::path& projectRoot);
        void Draw(const std::filesystem::path& projectRoot, bool* open);

        Assets::PackageOperationResult Install(
            const std::filesystem::path& projectRoot,
            const Assets::PackageInstallRequest& request);
        Assets::PackageOperationResult Update(
            const std::filesystem::path& projectRoot,
            const std::string& packageId,
            const std::string& version);
        Assets::PackageOperationResult Remove(
            const std::filesystem::path& projectRoot,
            const std::string& packageId);
        Assets::PackageOperationResult Verify(
            const std::filesystem::path& projectRoot,
            const std::string& packageIdOrEmpty);
        bool HasLaunchableInstalledEditorTool(
            const std::filesystem::path& projectRoot,
            const std::string& packageId,
            std::string* outDisplayName = nullptr);
        bool LaunchInstalledEditorTool(
            const std::filesystem::path& projectRoot,
            const std::string& packageId);
        void SetEditorToolLaunchOverride(
            std::function<bool(const std::filesystem::path&, const std::string&)> callback);

        const std::string& GetStatus() const;
        bool IsRegistryLoaded() const;
        std::vector<Assets::PackageMountRoot> GetMountRoots() const;
        bool ConsumeMountRootsDirty();

    private:
        enum class DeferredOperationType
        {
            None = 0,
            Refresh,
            Install,
            Update,
            Remove,
            Verify
        };

        struct DeferredOperation
        {
            DeferredOperationType type = DeferredOperationType::None;
            Assets::PackageInstallRequest installRequest;
            std::string packageId;
            std::string version;
        };

        Assets::PackageManager* GetManager();
        const Assets::PackageManager* GetManager() const;
        std::filesystem::path NormalizeProjectRoot(const std::filesystem::path& projectRoot) const;
        void SetStatus(std::string status);
        const Assets::InstalledPackageRecord* FindInstalledPackageRecord(
            const std::vector<Assets::InstalledPackageRecord>& installedPackages,
            const std::string& packageId,
            const std::string& version = {}) const;
        void LogOperationResult(
            const Assets::PackageOperationResult& result,
            const char* successVerb,
            const char* failureVerb);
        bool HasPendingOrRunningOperation() const;
        bool QueueOperation(DeferredOperation operation, std::string queuedStatus);
        EditorTaskDesc BuildOperationTaskDesc(const DeferredOperation& operation) const;
        void BeginOperationTask(const DeferredOperation& operation);
        void FinishOperationTask();
        void QueueInstall(const Assets::PackageInstallRequest& request);
        void QueueUpdate(std::string packageId, std::string version);
        void QueueRefresh();
        void QueueVerify(std::string packageIdOrEmpty);
        void QueueRemove(std::string packageId);
        void ExecuteDeferredOperation(const std::filesystem::path& projectRoot);

        std::filesystem::path m_ProjectRoot;
        std::unique_ptr<Assets::PackageManager> m_PackageManager;
        bool m_Initialized = false;
        bool m_RegistryLoaded = false;
        bool m_MountRootsDirty = false;
        std::string m_Status;
        std::string m_SearchQuery;
        int m_CategoryIndex = 0;
        std::string m_SelectedPackageId;
        int m_SelectedVersionIndex = 0;
        std::optional<DeferredOperation> m_DeferredOperation;
        std::optional<DeferredOperation> m_ActiveOperation;
        bool m_OperationRunning = false;
        EditorTaskHandle m_OperationTask = 0;
        std::function<bool(const std::filesystem::path&, const std::string&)> m_EditorToolLaunchOverride;
    };
}
