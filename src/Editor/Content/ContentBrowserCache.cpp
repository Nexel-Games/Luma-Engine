#include "Luma/Editor/Content/ContentBrowserCache.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <system_error>

#include <stb_image.h>

#include "Luma/RHI/IRenderBackend.h"

namespace Luma::Editor
{
    namespace
    {
        enum class ContentItemType : std::uint8_t
        {
            Folder = 0,
            Scene,
            Script,
            Image,
            Mesh,
            Material,
            Shader,
            Audio,
            Procedural,
            Package,
            Other
        };

        std::string ToLowerString(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });
            return value;
        }

        std::filesystem::path NormalizePathForComparison(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return {};
            }

            std::error_code ec;
            std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
            if (ec)
            {
                normalized = path.lexically_normal();
            }
            return normalized.lexically_normal();
        }

        ContentItemType DetectContentItemType(const std::filesystem::path& path, const bool isDirectory)
        {
            if (isDirectory)
            {
                return ContentItemType::Folder;
            }

            const std::string extension = ToLowerString(path.extension().string());
            if (extension == ".scene" || extension == ".lumascene")
            {
                return ContentItemType::Scene;
            }
            if (extension == ".lua" || extension == ".cs" || extension == ".cpp" || extension == ".h" ||
                extension == ".hpp" || extension == ".py")
            {
                return ContentItemType::Script;
            }
            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" ||
                extension == ".bmp" || extension == ".dds" || extension == ".hdr" || extension == ".exr" ||
                extension == ".lumatex" || extension == ".lumasky")
            {
                return ContentItemType::Image;
            }
            if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb" ||
                extension == ".lumamesh")
            {
                return ContentItemType::Mesh;
            }
            if (extension == ".material" || extension == ".mat" || extension == ".lumamat" || extension == ".mtl")
            {
                return ContentItemType::Material;
            }
            if (extension == ".glsl" || extension == ".hlsl" || extension == ".vert" || extension == ".frag" || extension == ".comp")
            {
                return ContentItemType::Shader;
            }
            if (extension == ".wav" || extension == ".mp3" || extension == ".ogg" || extension == ".flac" || extension == ".lumaaudio")
            {
                return ContentItemType::Audio;
            }
            if (extension == ".lpro" || extension == ".lumaproc" || extension == ".procjson" || extension == ".lumaprocedural")
            {
                return ContentItemType::Procedural;
            }
            if (extension == ".lpk" || extension == ".lumapkg" || extension == ".lpkmanifest")
            {
                return ContentItemType::Package;
            }

            return ContentItemType::Other;
        }

        bool SupportsThumbnailForPath(const std::filesystem::path& path, const bool isDirectory)
        {
            if (isDirectory)
            {
                return true;
            }

            const std::string extension = ToLowerString(path.extension().string());
            return extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" ||
                extension == ".bmp" || extension == ".dds" || extension == ".hdr" || extension == ".exr" ||
                extension == ".lumatex" || extension == ".lumasky" || extension == ".obj" || extension == ".fbx" ||
                extension == ".gltf" || extension == ".glb" || extension == ".lumamesh";
        }
    }

    ContentBrowserCache::~ContentBrowserCache()
    {
        Shutdown(nullptr);
    }

    void ContentBrowserCache::Shutdown(IRenderBackend* renderer)
    {
        if (m_ContentFolderTreeFuture.valid())
        {
            m_ContentFolderTreeFuture.wait();
        }

        if (m_ContentEntriesRefreshFuture.valid())
        {
            m_ContentEntriesRefreshFuture.wait();
        }

        ReleaseFolderThumbnailTexture(renderer);
        m_ThumbnailService.Shutdown(renderer);

        m_ContentEntries.clear();
        m_ContentEntryMetadata.clear();
        m_ContentFolderTreeNodes.clear();
        m_FilteredContentEntryIndices.clear();
        m_ContentFolderTreeRootPath.clear();
        m_ContentFolderTreeBuildingRootPath.clear();
        m_ContentFolderTreeQueuedRootPath.clear();
        m_ContentEntriesLoadedDirectory.clear();
        m_ContentEntriesRefreshDirectory.clear();
        m_ContentEntriesQueuedDirectory.clear();
        m_ContentFolderTreeBuildInFlight = false;
        m_ContentFolderTreeBuildQueued = false;
        m_ContentEntriesRefreshInFlight = false;
        m_ContentEntriesRefreshQueued = false;
        m_ContentFolderTreeDirty = true;
        m_ContentFilterCacheDirty = true;
        m_ContentFolderThumbnailSourcePath.clear();
        m_ContentFolderThumbnailLookupComplete = false;
    }

    void ContentBrowserCache::Tick(IRenderBackend& renderer)
    {
        m_ThumbnailService.Tick(renderer);
    }

    void ContentBrowserCache::SetThumbnailBudgetMsPerFrame(const double milliseconds)
    {
        m_ThumbnailService.SetBudgetMsPerFrame(milliseconds);
    }

    void ContentBrowserCache::SetThumbnailConcurrency(const std::size_t concurrency)
    {
        m_ThumbnailService.SetConcurrency(concurrency);
    }

    void ContentBrowserCache::InvalidateAllThumbnails()
    {
        m_ThumbnailService.InvalidateAll();
    }

    void ContentBrowserCache::PruneThumbnailsToPaths()
    {
        m_ThumbnailService.PruneToPaths(m_ContentEntries);
    }

    void ContentBrowserCache::BeginThumbnailRequestFrame()
    {
        AdaptThumbnailRequestBudget();
        m_NewThumbnailRequestsIssuedThisFrame = 0;
    }

    void ContentBrowserCache::AdaptThumbnailRequestBudget()
    {
        constexpr std::size_t kMinRequestsPerFrame = 2;
        constexpr std::size_t kMaxRequestsPerFrame = 10;
        constexpr std::size_t kHighBacklog = 24;
        constexpr std::size_t kLowBacklog = 6;
        constexpr double kHighTickMs = 1.25;
        constexpr double kHighUploadMs = 0.90;
        constexpr double kLowTickMs = 0.35;
        constexpr double kLowUploadMs = 0.20;

        const ThumbnailServiceStats stats = m_ThumbnailService.GetStats();
        const std::size_t totalBacklog =
            stats.pendingQueueDepth +
            stats.backgroundJobQueueDepth +
            stats.backgroundResultQueueDepth;

        if ((totalBacklog >= kHighBacklog || stats.lastTickMs >= kHighTickMs || stats.lastUploadMs >= kHighUploadMs) &&
            m_MaxNewThumbnailRequestsPerFrame > kMinRequestsPerFrame)
        {
            --m_MaxNewThumbnailRequestsPerFrame;
            return;
        }

        if (totalBacklog <= kLowBacklog &&
            stats.lastTickMs <= kLowTickMs &&
            stats.lastUploadMs <= kLowUploadMs &&
            m_MaxNewThumbnailRequestsPerFrame < kMaxRequestsPerFrame)
        {
            ++m_MaxNewThumbnailRequestsPerFrame;
        }
    }

    void* ContentBrowserCache::GetOrCreateThumbnail(IRenderBackend* renderer, ContentBrowserEntry& entry)
    {
        if (entry.isDirectory)
        {
            return EnsureFolderThumbnailLoaded(renderer) ? m_ContentFolderThumbnailTexture : nullptr;
        }

        if (!entry.supportsThumbnail)
        {
            return nullptr;
        }

        ThumbnailQueryResult thumbnail {};
        if (entry.thumbnailHandle != 0)
        {
            thumbnail = m_ThumbnailService.GetThumbnail(entry.thumbnailHandle);
            if (thumbnail.state != ThumbnailState::Missing)
            {
                return thumbnail.texture;
            }
        }

        if (m_NewThumbnailRequestsIssuedThisFrame >= m_MaxNewThumbnailRequestsPerFrame)
        {
            return thumbnail.texture;
        }

        ThumbnailRequestOptions requestOptions {};
        requestOptions.highPriority = false;
        const ThumbnailHandle handle = m_ThumbnailService.RequestThumbnail(entry.path, {}, requestOptions);
        if (handle == 0)
        {
            return thumbnail.texture;
        }

        entry.thumbnailHandle = handle;
        ++m_NewThumbnailRequestsIssuedThisFrame;
        thumbnail = m_ThumbnailService.GetThumbnail(handle);
        return thumbnail.texture;
    }

    bool ContentBrowserCache::EnsureFolderThumbnailLoaded(IRenderBackend* renderer)
    {
        if (m_ContentFolderThumbnailTexture != nullptr)
        {
            return true;
        }
        if (renderer == nullptr)
        {
            return false;
        }

        std::error_code ec;
        if (!m_ContentFolderThumbnailSourcePath.empty())
        {
            if (!std::filesystem::exists(m_ContentFolderThumbnailSourcePath, ec) ||
                !std::filesystem::is_regular_file(m_ContentFolderThumbnailSourcePath, ec))
            {
                m_ContentFolderThumbnailSourcePath.clear();
                m_ContentFolderThumbnailLookupComplete = false;
            }
        }

        if (!m_ContentFolderThumbnailLookupComplete)
        {
            const std::array<std::filesystem::path, 7> candidates = {
                std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/Folders/Folder_Base_256x.png"),
                std::filesystem::current_path() / "thirdparty" / "editor-icons" / "imgs" / "Icons" / "Folders" /
                    "Folder_Base_256x.png",
                std::filesystem::current_path().parent_path() / "thirdparty" / "editor-icons" / "imgs" / "Icons" / "Folders" /
                    "Folder_Base_256x.png",
                std::filesystem::current_path().parent_path().parent_path() / "thirdparty" / "editor-icons" / "imgs" / "Icons" /
                    "Folders" / "Folder_Base_256x.png",
                std::filesystem::current_path() / "LumaEngine" / "thirdparty" / "editor-icons" / "imgs" / "Icons" / "Folders" /
                    "Folder_Base_256x.png",
                std::filesystem::current_path().parent_path() / "LumaEngine" / "thirdparty" / "editor-icons" / "imgs" / "Icons" /
                    "Folders" / "Folder_Base_256x.png",
                std::filesystem::current_path().parent_path().parent_path() / "LumaEngine" / "thirdparty" / "editor-icons" /
                    "imgs" / "Icons" / "Folders" / "Folder_Base_256x.png"
            };

            for (const std::filesystem::path& candidate : candidates)
            {
                if (!std::filesystem::exists(candidate, ec) || !std::filesystem::is_regular_file(candidate, ec))
                {
                    continue;
                }

                m_ContentFolderThumbnailSourcePath = candidate;
                break;
            }

            m_ContentFolderThumbnailLookupComplete = true;
        }

        if (m_ContentFolderThumbnailSourcePath.empty())
        {
            return false;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* pixels =
            stbi_load(m_ContentFolderThumbnailSourcePath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if (pixels == nullptr || width <= 0 || height <= 0)
        {
            return false;
        }

        void* texture = renderer->CreateImGuiTextureRGBA8(
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height),
            pixels);
        stbi_image_free(pixels);
        if (texture == nullptr)
        {
            return false;
        }

        m_ContentFolderThumbnailTexture = texture;
        m_ContentFolderThumbnailWidth = width;
        m_ContentFolderThumbnailHeight = height;
        return true;
    }

    void ContentBrowserCache::ReleaseFolderThumbnailTexture(IRenderBackend* renderer)
    {
        if (m_ContentFolderThumbnailTexture != nullptr && renderer != nullptr)
        {
            renderer->DestroyImGuiTexture(m_ContentFolderThumbnailTexture);
        }

        m_ContentFolderThumbnailTexture = nullptr;
        m_ContentFolderThumbnailWidth = 0;
        m_ContentFolderThumbnailHeight = 0;
    }

    void* ContentBrowserCache::GetOrCreateThumbnail(
        IRenderBackend* renderer,
        const std::filesystem::path& entryPath,
        const bool isDirectory)
    {
        if (isDirectory)
        {
            return EnsureFolderThumbnailLoaded(renderer) ? m_ContentFolderThumbnailTexture : nullptr;
        }

        ThumbnailRequestOptions requestOptions {};
        requestOptions.highPriority = false;
        const ThumbnailQueryResult cachedThumbnail = m_ThumbnailService.GetThumbnail(entryPath, {});
        if (cachedThumbnail.state != ThumbnailState::Missing)
        {
            return cachedThumbnail.texture;
        }

        if (m_NewThumbnailRequestsIssuedThisFrame >= m_MaxNewThumbnailRequestsPerFrame)
        {
            return nullptr;
        }

        const ThumbnailHandle handle = m_ThumbnailService.RequestThumbnail(entryPath, {}, requestOptions);
        if (handle == 0)
        {
            return nullptr;
        }

        ++m_NewThumbnailRequestsIssuedThisFrame;
        const ThumbnailQueryResult thumbnail = m_ThumbnailService.GetThumbnail(handle);
        return thumbnail.texture;
    }

    void ContentBrowserCache::ClearEntries()
    {
        m_ContentEntries.clear();
        m_ContentEntryMetadata.clear();
        m_ContentEntriesLoadedDirectory.clear();
        InvalidateFilterCache();
    }

    void ContentBrowserCache::InvalidateFilterCache()
    {
        m_ContentFilterCacheDirty = true;
        m_FilteredContentEntryIndices.clear();
    }

    void ContentBrowserCache::RebuildFilteredContentEntries()
    {
        if (!m_ContentFilterCacheDirty)
        {
            return;
        }

        m_FilteredContentEntryIndices.clear();
        const std::string normalizedSearch = ToLowerString(m_SearchQuery);
        m_FilteredContentEntryIndices.reserve(m_ContentEntryMetadata.size());
        for (std::size_t i = 0; i < m_ContentEntryMetadata.size(); ++i)
        {
            const ContentBrowserEntry& entry = m_ContentEntryMetadata[i];
            if (!normalizedSearch.empty() && entry.lowerName.find(normalizedSearch) == std::string::npos)
            {
                continue;
            }

            if (m_TypeFilterIndex != 0 &&
                static_cast<int>(entry.type) != (m_TypeFilterIndex - 1))
            {
                continue;
            }

            m_FilteredContentEntryIndices.push_back(i);
        }

        m_ContentFilterCacheDirty = false;
    }

    void ContentBrowserCache::InvalidateFolderTreeCache()
    {
        m_ContentFolderTreeDirty = true;
        m_ContentFolderTreeNodes.clear();
        m_ContentFolderTreeRootPath.clear();
    }

    void ContentBrowserCache::RebuildContentFolderTreeCache(const std::filesystem::path& rootPath)
    {
        const std::filesystem::path normalizedRootPath = rootPath.lexically_normal();
        if (normalizedRootPath.empty())
        {
            InvalidateFolderTreeCache();
            return;
        }

        if (m_ContentFolderTreeBuildInFlight)
        {
            if (NormalizePathForComparison(m_ContentFolderTreeBuildingRootPath) ==
                NormalizePathForComparison(normalizedRootPath))
            {
                return;
            }

            m_ContentFolderTreeBuildQueued = true;
            m_ContentFolderTreeQueuedRootPath = normalizedRootPath;
            return;
        }

        if (NormalizePathForComparison(m_ContentFolderTreeRootPath) ==
                NormalizePathForComparison(normalizedRootPath) &&
            !m_ContentFolderTreeNodes.empty() &&
            !m_ContentFolderTreeDirty)
        {
            return;
        }

        m_ContentFolderTreeNodes.clear();
        m_ContentFolderTreeRootPath.clear();
        m_ContentFolderTreeBuildInFlight = true;
        m_ContentFolderTreeBuildingRootPath = normalizedRootPath;
        m_ContentFolderTreeFuture = std::async(
            std::launch::async,
            [normalizedRootPath]() -> ContentFolderTreeBuildResult
            {
                ContentFolderTreeBuildResult result;
                result.rootPath = normalizedRootPath;

                std::error_code ec;
                if (!std::filesystem::exists(normalizedRootPath, ec) ||
                    !std::filesystem::is_directory(normalizedRootPath, ec))
                {
                    return result;
                }

                result.nodes.push_back(ContentFolderTreeNode {});
                auto buildNode = [&](auto&& self, const std::size_t nodeIndex) -> void
                {
                    if (nodeIndex >= result.nodes.size())
                    {
                        return;
                    }

                    const std::filesystem::path absoluteDirectory =
                        normalizedRootPath / result.nodes[nodeIndex].relativePath;
                    std::vector<std::pair<std::string, std::filesystem::path>> childDirectories;
                    std::error_code iterateEc;
                    for (const auto& entry : std::filesystem::directory_iterator(
                             absoluteDirectory,
                             std::filesystem::directory_options::skip_permission_denied,
                             iterateEc))
                    {
                        if (iterateEc)
                        {
                            break;
                        }

                        std::error_code entryEc;
                        if (!entry.is_directory(entryEc))
                        {
                            continue;
                        }

                        const std::filesystem::path childPath = entry.path().filename();
                        childDirectories.emplace_back(ToLowerString(childPath.string()), childPath);
                    }

                    std::sort(
                        childDirectories.begin(),
                        childDirectories.end(),
                        [](const auto& lhs, const auto& rhs)
                        {
                            return lhs.first < rhs.first;
                        });

                    for (const auto& childDirectory : childDirectories)
                    {
                        ContentFolderTreeNode childNode;
                        childNode.relativePath = result.nodes[nodeIndex].relativePath / childDirectory.second;
                        childNode.relativePathString = childNode.relativePath.generic_string();
                        childNode.treeNodeId = "FolderNode##" + childNode.relativePathString;
                        childNode.name = childDirectory.second.string();
                        childNode.lowerName = childDirectory.first;

                        const std::size_t childIndex = result.nodes.size();
                        result.nodes.push_back(std::move(childNode));
                        result.nodes[nodeIndex].children.push_back(childIndex);
                        self(self, childIndex);
                    }
                };

                buildNode(buildNode, 0);
                return result;
            });
    }

    void ContentBrowserCache::PumpContentFolderTreeRebuild(const std::filesystem::path& activeRootPath)
    {
        if (!m_ContentFolderTreeBuildInFlight || !m_ContentFolderTreeFuture.valid())
        {
            return;
        }

        if (m_ContentFolderTreeFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        {
            return;
        }

        ContentFolderTreeBuildResult result = m_ContentFolderTreeFuture.get();
        m_ContentFolderTreeBuildInFlight = false;
        m_ContentFolderTreeBuildingRootPath.clear();

        if (!activeRootPath.empty() &&
            NormalizePathForComparison(result.rootPath) == NormalizePathForComparison(activeRootPath))
        {
            m_ContentFolderTreeNodes = std::move(result.nodes);
            m_ContentFolderTreeRootPath = result.rootPath;
            m_ContentFolderTreeDirty = false;
        }

        if (m_ContentFolderTreeBuildQueued)
        {
            const std::filesystem::path queuedRootPath = m_ContentFolderTreeQueuedRootPath;
            m_ContentFolderTreeBuildQueued = false;
            m_ContentFolderTreeQueuedRootPath.clear();
            if (!queuedRootPath.empty())
            {
                RebuildContentFolderTreeCache(queuedRootPath);
            }
        }
    }

    void ContentBrowserCache::RefreshContentEntries(const std::filesystem::path& targetDirectory, std::string& ioStatus)
    {
        const std::filesystem::path normalizedTargetDirectory = targetDirectory.lexically_normal();
        if (normalizedTargetDirectory.empty())
        {
            ClearEntries();
            ioStatus = "Content path is not set.";
            return;
        }

        const std::filesystem::path targetKey = NormalizePathForComparison(normalizedTargetDirectory);
        if (m_ContentEntriesRefreshInFlight)
        {
            if (NormalizePathForComparison(m_ContentEntriesRefreshDirectory) == targetKey)
            {
                return;
            }

            if (m_ContentEntriesRefreshQueued &&
                NormalizePathForComparison(m_ContentEntriesQueuedDirectory) == targetKey)
            {
                return;
            }

            m_ContentEntriesRefreshQueued = true;
            m_ContentEntriesQueuedDirectory = normalizedTargetDirectory;
            ioStatus = "Loading folder...";
            return;
        }

        if (NormalizePathForComparison(m_ContentEntriesLoadedDirectory) != targetKey)
        {
            ClearEntries();
        }

        m_ContentEntriesRefreshDirectory = normalizedTargetDirectory;
        m_ContentEntriesRefreshInFlight = true;
        ioStatus = "Loading folder...";
        m_ContentEntriesRefreshFuture = std::async(
            std::launch::async,
            [normalizedTargetDirectory]() -> ContentEntriesRefreshResult
            {
                ContentEntriesRefreshResult result;
                result.directory = normalizedTargetDirectory;

                std::error_code ec;
                if (!std::filesystem::exists(normalizedTargetDirectory, ec))
                {
                    result.status = "Directory missing: " + normalizedTargetDirectory.string();
                    return result;
                }

                if (!std::filesystem::is_directory(normalizedTargetDirectory, ec))
                {
                    result.status = "Path is not a directory: " + normalizedTargetDirectory.string();
                    return result;
                }

                for (const auto& entry : std::filesystem::directory_iterator(
                         normalizedTargetDirectory,
                         std::filesystem::directory_options::skip_permission_denied,
                         ec))
                {
                    if (ec)
                    {
                        result.status = "Cannot read full directory listing.";
                        break;
                    }

                    const std::string fileName = entry.path().filename().string();
                    if (fileName == ".asset_registry.json" || entry.path().extension() == ".meta")
                    {
                        continue;
                    }

                    ContentBrowserEntry cachedEntry;
                    cachedEntry.path = entry.path().lexically_normal();
                    cachedEntry.name = cachedEntry.path.filename().string();
                    cachedEntry.lowerName = ToLowerString(cachedEntry.name);
                    cachedEntry.genericPath = cachedEntry.path.generic_string();
                    cachedEntry.tileId = "ContentEntry##" + cachedEntry.genericPath;
                    cachedEntry.isDirectory = entry.is_directory(ec);
                    if (ec)
                    {
                        ec.clear();
                        cachedEntry.isDirectory = std::filesystem::is_directory(cachedEntry.path, ec);
                    }
                    cachedEntry.supportsThumbnail = SupportsThumbnailForPath(cachedEntry.path, cachedEntry.isDirectory);
                    cachedEntry.type = static_cast<std::uint8_t>(
                        DetectContentItemType(cachedEntry.path, cachedEntry.isDirectory));
                    if (!cachedEntry.isDirectory)
                    {
                        cachedEntry.byteSize = std::filesystem::file_size(cachedEntry.path, ec);
                        cachedEntry.hasByteSize = !ec;
                        if (ec)
                        {
                            ec.clear();
                            cachedEntry.byteSize = 0;
                        }
                    }
                    result.entries.push_back(std::move(cachedEntry));
                }

                std::sort(
                    result.entries.begin(),
                    result.entries.end(),
                    [](const ContentBrowserEntry& lhs, const ContentBrowserEntry& rhs)
                    {
                        if (lhs.isDirectory != rhs.isDirectory)
                        {
                            return lhs.isDirectory && !rhs.isDirectory;
                        }

                        return lhs.lowerName < rhs.lowerName;
                    });

                return result;
            });
    }

    void ContentBrowserCache::PumpContentEntriesRefresh(
        const std::filesystem::path& currentDirectory,
        std::filesystem::path& ioSelectedEntry,
        std::string& ioStatus)
    {
        if (!m_ContentEntriesRefreshInFlight || !m_ContentEntriesRefreshFuture.valid())
        {
            return;
        }

        if (m_ContentEntriesRefreshFuture.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
        {
            return;
        }

        ContentEntriesRefreshResult result = m_ContentEntriesRefreshFuture.get();
        m_ContentEntriesRefreshInFlight = false;
        m_ContentEntriesRefreshDirectory.clear();

        if (NormalizePathForComparison(result.directory) == NormalizePathForComparison(currentDirectory))
        {
            m_ContentEntryMetadata = std::move(result.entries);
            m_ContentEntries.clear();
            m_ContentEntries.reserve(m_ContentEntryMetadata.size());
            for (const ContentBrowserEntry& entry : m_ContentEntryMetadata)
            {
                m_ContentEntries.push_back(entry.path);
            }

            m_ContentEntriesLoadedDirectory = result.directory;
            InvalidateFilterCache();
            PruneThumbnailsToPaths();
            ioStatus = std::move(result.status);

            if (!ioSelectedEntry.empty() && !std::filesystem::exists(ioSelectedEntry))
            {
                ioSelectedEntry.clear();
            }
        }

        if (m_ContentEntriesRefreshQueued)
        {
            const std::filesystem::path queuedDirectory = m_ContentEntriesQueuedDirectory;
            m_ContentEntriesRefreshQueued = false;
            m_ContentEntriesQueuedDirectory.clear();
            if (!queuedDirectory.empty())
            {
                RefreshContentEntries(queuedDirectory, ioStatus);
            }
        }
    }

    ContentBrowserThumbnailStats ContentBrowserCache::GetThumbnailStats() const
    {
        ContentBrowserThumbnailStats stats {};
        stats.serviceStats = m_ThumbnailService.GetStats();
        stats.totalEntryCount = m_ContentEntryMetadata.size();
        stats.filteredEntryCount = m_FilteredContentEntryIndices.size();
        stats.newRequestsIssuedThisFrame = m_NewThumbnailRequestsIssuedThisFrame;
        stats.maxNewRequestsPerFrame = m_MaxNewThumbnailRequestsPerFrame;
        stats.folderThumbnailLoaded = (m_ContentFolderThumbnailTexture != nullptr);
        return stats;
    }

    const std::vector<std::filesystem::path>& ContentBrowserCache::GetContentEntries() const
    {
        return m_ContentEntries;
    }

    std::vector<ContentBrowserEntry>& ContentBrowserCache::GetContentEntryMetadata()
    {
        return m_ContentEntryMetadata;
    }

    const std::vector<ContentBrowserEntry>& ContentBrowserCache::GetContentEntryMetadata() const
    {
        return m_ContentEntryMetadata;
    }

    const std::vector<ContentFolderTreeNode>& ContentBrowserCache::GetContentFolderTreeNodes() const
    {
        return m_ContentFolderTreeNodes;
    }

    const std::vector<std::size_t>& ContentBrowserCache::GetFilteredContentEntryIndices() const
    {
        return m_FilteredContentEntryIndices;
    }

    const std::string& ContentBrowserCache::GetSearchQuery() const
    {
        return m_SearchQuery;
    }

    void ContentBrowserCache::SetSearchQuery(std::string query)
    {
        if (m_SearchQuery == query)
        {
            return;
        }

        m_SearchQuery = std::move(query);
        InvalidateFilterCache();
    }

    int ContentBrowserCache::GetTypeFilterIndex() const
    {
        return m_TypeFilterIndex;
    }

    void ContentBrowserCache::SetTypeFilterIndex(const int filterIndex)
    {
        if (m_TypeFilterIndex == filterIndex)
        {
            return;
        }

        m_TypeFilterIndex = filterIndex;
        InvalidateFilterCache();
    }

    const std::filesystem::path& ContentBrowserCache::GetContentFolderTreeRootPath() const
    {
        return m_ContentFolderTreeRootPath;
    }

    bool ContentBrowserCache::IsContentFolderTreeDirty() const
    {
        return m_ContentFolderTreeDirty;
    }

    bool ContentBrowserCache::IsContentFolderTreeBuildInFlight() const
    {
        return m_ContentFolderTreeBuildInFlight;
    }

    bool ContentBrowserCache::IsContentEntriesRefreshInFlight() const
    {
        return m_ContentEntriesRefreshInFlight;
    }
}
