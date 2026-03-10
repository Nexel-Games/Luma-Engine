#include "Luma/Editor/Packages/PackageManagerHostService.h"

namespace Luma::Editor
{
    void PackageManagerHostService::Initialize(PackageManagerHostContext& context) const
    {
        if (context.panel == nullptr)
        {
            return;
        }

        const std::filesystem::path projectRoot = ResolveProjectRoot(context);
        context.panel->Reset();
        context.panel->EnsureInitialized(projectRoot);
        context.panel->RefreshRegistry(projectRoot);
        if (context.refreshContentRoots)
        {
            context.refreshContentRoots();
        }
        if (context.refreshContentEntries)
        {
            context.refreshContentEntries();
        }
    }

    void PackageManagerHostService::Shutdown(PackageManagerPanel& panel) const
    {
        panel.Reset();
    }

    void PackageManagerHostService::Tick(PackageManagerHostContext& context) const
    {
        if (context.panel == nullptr)
        {
            return;
        }

        context.panel->Tick(ResolveProjectRoot(context));
        if (context.panel->ConsumeMountRootsDirty() && context.refreshContentRoots)
        {
            context.refreshContentRoots();
        }
    }

    void PackageManagerHostService::Draw(PackageManagerHostContext& context) const
    {
        if (context.panel == nullptr || context.showPackageManagerPanel == nullptr || !*context.showPackageManagerPanel)
        {
            return;
        }

        context.panel->Draw(ResolveProjectRoot(context), context.showPackageManagerPanel);
    }

    void PackageManagerHostService::Open(PackageManagerHostContext& context) const
    {
        if (context.panel == nullptr || context.showPackageManagerPanel == nullptr)
        {
            return;
        }

        *context.showPackageManagerPanel = true;
        context.panel->EnsureInitialized(ResolveProjectRoot(context));
    }

    void PackageManagerHostService::RequestRefresh(PackageManagerHostContext& context) const
    {
        if (context.panel != nullptr)
        {
            context.panel->RequestRefresh(ResolveProjectRoot(context));
        }
    }

    void PackageManagerHostService::RequestInstall(
        PackageManagerHostContext& context,
        const std::string_view packageId,
        const std::string_view version) const
    {
        if (context.panel == nullptr)
        {
            return;
        }

        Assets::PackageInstallRequest request {};
        request.id = packageId;
        request.version = version;
        request.importSamplesToAssets = false;
        request.attemptHotLoad = true;
        context.panel->RequestInstall(ResolveProjectRoot(context), request);
    }

    void PackageManagerHostService::RequestUpdate(
        PackageManagerHostContext& context,
        const std::string_view packageId,
        const std::string_view version) const
    {
        if (context.panel != nullptr)
        {
            context.panel->RequestUpdate(ResolveProjectRoot(context), std::string(packageId), std::string(version));
        }
    }

    void PackageManagerHostService::RequestRemove(
        PackageManagerHostContext& context,
        const std::string_view packageId) const
    {
        if (context.panel != nullptr)
        {
            context.panel->RequestRemove(ResolveProjectRoot(context), std::string(packageId));
        }
    }

    void PackageManagerHostService::RequestVerify(
        PackageManagerHostContext& context,
        const std::string_view packageIdOrEmpty) const
    {
        if (context.panel != nullptr)
        {
            context.panel->RequestVerify(ResolveProjectRoot(context), std::string(packageIdOrEmpty));
        }
    }

    std::filesystem::path PackageManagerHostService::ResolveProjectRoot(const PackageManagerHostContext& context) const
    {
        if (context.projectLoaded && context.projectRoot != nullptr && !context.projectRoot->empty())
        {
            return *context.projectRoot;
        }

        return std::filesystem::current_path();
    }
}
