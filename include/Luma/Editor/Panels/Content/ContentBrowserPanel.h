#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Luma/Editor/Content/ContentBrowserCache.h"

namespace Luma
{
    class IRenderBackend;
}

namespace Luma::Editor
{
    struct ContentBrowserRootView
    {
        std::string id;
        std::string label;
        std::string badge;
        std::string source;
        std::filesystem::path path;
        bool readOnly = false;
        bool packageRoot = false;
    };

    struct ContentBrowserPanelContext
    {
        const std::vector<ContentBrowserRootView>* roots = nullptr;
        int* activeRootIndex = nullptr;
        const std::filesystem::path* activeRootPath = nullptr;
        const std::string* activeRootLabel = nullptr;
        const std::string* activeRootSource = nullptr;
        const std::string* activeRootBadge = nullptr;
        const bool* activeRootReadOnly = nullptr;
        std::filesystem::path* currentRelativePath = nullptr;
        std::string* currentRelativeString = nullptr;
        std::filesystem::path* selectedEntry = nullptr;
        std::string* status = nullptr;
        ContentBrowserCache* cache = nullptr;
        IRenderBackend* thumbnailRenderer = nullptr;
        std::function<void(int)> activateRoot;
        std::function<void(const std::filesystem::path&)> requestFolderTreeRebuild;
        std::function<bool(const std::filesystem::path&)> openDirectory;
        std::function<void(const std::filesystem::path&)> requestLoadScene;
        std::function<void()> refresh;
        std::function<void()> createFolder;
        std::function<void(const std::filesystem::path&, const std::string&)> openAsset;
    };

    class ContentBrowserPanel
    {
    public:
        void Draw(ContentBrowserPanelContext& context);
    };
}
