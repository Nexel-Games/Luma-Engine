#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include "Luma/Asset/Package/PackageTypes.h"
#include "Luma/Editor/Content/ContentBrowserController.h"

namespace Luma::Editor
{
    struct ContentBrowserRootRefreshContext
    {
        std::filesystem::path* contentRoot = nullptr;
        std::filesystem::path* currentDirectory = nullptr;
        std::filesystem::path* selectedEntry = nullptr;
        std::vector<ContentBrowserRootState>* roots = nullptr;
        int* activeRootIndex = nullptr;
        const std::vector<Assets::PackageMountRoot>* mountRoots = nullptr;
        std::function<void()> invalidateFolderTreeCache;
        std::function<void()> clearEntries;
    };

    class ContentBrowserRootService
    {
    public:
        std::filesystem::path ResolveInitialContentRoot(bool projectLoaded, const std::filesystem::path& assetsPath) const;
        void RefreshRoots(ContentBrowserRootRefreshContext& context) const;
        const ContentBrowserRootState* GetActiveRoot(const std::vector<ContentBrowserRootState>& roots, int activeRootIndex) const;
    };
}
