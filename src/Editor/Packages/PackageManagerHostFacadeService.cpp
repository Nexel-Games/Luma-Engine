#include "Luma/Editor/Packages/PackageManagerHostFacadeService.h"

namespace Luma::Editor
{
    void PackageManagerHostFacadeService::Initialize(const PackageManagerHostFacadeContext& context) const
    {
        if (context.hostService == nullptr)
        {
            return;
        }

        PackageManagerHostContext hostContext = BuildHostContext(context);
        context.hostService->Initialize(hostContext);
    }

    void PackageManagerHostFacadeService::Shutdown(const PackageManagerHostFacadeContext& context) const
    {
        if (context.hostService == nullptr || context.panel == nullptr)
        {
            return;
        }

        context.hostService->Shutdown(*context.panel);
    }

    void PackageManagerHostFacadeService::Tick(const PackageManagerHostFacadeContext& context) const
    {
        if (context.hostService == nullptr)
        {
            return;
        }

        PackageManagerHostContext hostContext = BuildHostContext(context);
        context.hostService->Tick(hostContext);
    }

    void PackageManagerHostFacadeService::Draw(const PackageManagerHostFacadeContext& context) const
    {
        if (context.hostService == nullptr)
        {
            return;
        }

        PackageManagerHostContext hostContext = BuildHostContext(context);
        context.hostService->Draw(hostContext);
    }

    void PackageManagerHostFacadeService::Open(const PackageManagerHostFacadeContext& context) const
    {
        if (context.hostService == nullptr)
        {
            return;
        }

        PackageManagerHostContext hostContext = BuildHostContext(context);
        context.hostService->Open(hostContext);
    }

    void PackageManagerHostFacadeService::RequestRefresh(const PackageManagerHostFacadeContext& context) const
    {
        if (context.hostService == nullptr)
        {
            return;
        }

        PackageManagerHostContext hostContext = BuildHostContext(context);
        context.hostService->RequestRefresh(hostContext);
    }

    void PackageManagerHostFacadeService::RequestInstall(
        const PackageManagerHostFacadeContext& context,
        const std::string_view packageId,
        const std::string_view version) const
    {
        if (context.hostService == nullptr)
        {
            return;
        }

        PackageManagerHostContext hostContext = BuildHostContext(context);
        context.hostService->RequestInstall(hostContext, packageId, version);
    }

    void PackageManagerHostFacadeService::RequestUpdate(
        const PackageManagerHostFacadeContext& context,
        const std::string_view packageId,
        const std::string_view version) const
    {
        if (context.hostService == nullptr)
        {
            return;
        }

        PackageManagerHostContext hostContext = BuildHostContext(context);
        context.hostService->RequestUpdate(hostContext, packageId, version);
    }

    void PackageManagerHostFacadeService::RequestRemove(
        const PackageManagerHostFacadeContext& context,
        const std::string_view packageId) const
    {
        if (context.hostService == nullptr)
        {
            return;
        }

        PackageManagerHostContext hostContext = BuildHostContext(context);
        context.hostService->RequestRemove(hostContext, packageId);
    }

    void PackageManagerHostFacadeService::RequestVerify(
        const PackageManagerHostFacadeContext& context,
        const std::string_view packageIdOrEmpty) const
    {
        if (context.hostService == nullptr)
        {
            return;
        }

        PackageManagerHostContext hostContext = BuildHostContext(context);
        context.hostService->RequestVerify(hostContext, packageIdOrEmpty);
    }

    PackageManagerHostContext PackageManagerHostFacadeService::BuildHostContext(
        const PackageManagerHostFacadeContext& context) const
    {
        PackageManagerHostContext hostContext {};
        hostContext.panel = context.panel;
        hostContext.projectLoaded = context.projectLoaded;
        hostContext.projectRoot = context.projectRoot;
        hostContext.showPackageManagerPanel = context.showPackageManagerPanel;
        hostContext.refreshContentRoots = context.refreshContentRoots;
        hostContext.refreshContentEntries = context.refreshContentEntries;
        return hostContext;
    }
}
