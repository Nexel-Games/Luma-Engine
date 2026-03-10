#pragma once

#include <filesystem>
#include <functional>
#include <string_view>

#include "Luma/Editor/Panels/Packages/PackageManagerPanel.h"

namespace Luma::Editor
{
    struct PackageManagerHostContext
    {
        PackageManagerPanel* panel = nullptr;
        bool projectLoaded = false;
        const std::filesystem::path* projectRoot = nullptr;
        bool* showPackageManagerPanel = nullptr;
        std::function<void()> refreshContentRoots;
        std::function<void()> refreshContentEntries;
    };

    class PackageManagerHostService
    {
    public:
        void Initialize(PackageManagerHostContext& context) const;
        void Shutdown(PackageManagerPanel& panel) const;
        void Tick(PackageManagerHostContext& context) const;
        void Draw(PackageManagerHostContext& context) const;
        void Open(PackageManagerHostContext& context) const;
        void RequestRefresh(PackageManagerHostContext& context) const;
        void RequestInstall(PackageManagerHostContext& context, std::string_view packageId, std::string_view version) const;
        void RequestUpdate(PackageManagerHostContext& context, std::string_view packageId, std::string_view version) const;
        void RequestRemove(PackageManagerHostContext& context, std::string_view packageId) const;
        void RequestVerify(PackageManagerHostContext& context, std::string_view packageIdOrEmpty) const;

    private:
        std::filesystem::path ResolveProjectRoot(const PackageManagerHostContext& context) const;
    };
}
