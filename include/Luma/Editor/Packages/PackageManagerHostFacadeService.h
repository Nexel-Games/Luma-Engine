#pragma once

#include <functional>

#include "Luma/Editor/Packages/PackageManagerHostService.h"

namespace Luma::Editor
{
    struct PackageManagerHostFacadeContext
    {
        PackageManagerHostService* hostService = nullptr;
        PackageManagerPanel* panel = nullptr;
        bool projectLoaded = false;
        const std::filesystem::path* projectRoot = nullptr;
        bool* showPackageManagerPanel = nullptr;
        std::function<void()> refreshContentRoots;
        std::function<void()> refreshContentEntries;
    };

    class PackageManagerHostFacadeService
    {
    public:
        void Initialize(const PackageManagerHostFacadeContext& context) const;
        void Shutdown(const PackageManagerHostFacadeContext& context) const;
        void Tick(const PackageManagerHostFacadeContext& context) const;
        void Draw(const PackageManagerHostFacadeContext& context) const;
        void Open(const PackageManagerHostFacadeContext& context) const;
        void RequestRefresh(const PackageManagerHostFacadeContext& context) const;
        void RequestInstall(const PackageManagerHostFacadeContext& context, std::string_view packageId, std::string_view version) const;
        void RequestUpdate(const PackageManagerHostFacadeContext& context, std::string_view packageId, std::string_view version) const;
        void RequestRemove(const PackageManagerHostFacadeContext& context, std::string_view packageId) const;
        void RequestVerify(const PackageManagerHostFacadeContext& context, std::string_view packageIdOrEmpty) const;

    private:
        PackageManagerHostContext BuildHostContext(const PackageManagerHostFacadeContext& context) const;
    };
}
