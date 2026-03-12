#include "Luma/Editor/Panels/Content/ContentBrowserPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <functional>
#include <system_error>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"

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

        void ShowTooltip(const std::string_view tooltip)
        {
            UI::Tooltip::Show(tooltip);
        }

        bool ButtonWithTooltip(const char* label, const std::string_view tooltip, const ImVec2 size = ImVec2(0.0f, 0.0f))
        {
            const bool pressed = ImGui::Button(label, size);
            ShowTooltip(tooltip);
            return pressed;
        }

        bool InputTextWithHintWithTooltip(
            const char* label,
            const char* hint,
            char* buffer,
            const std::size_t bufferSize,
            const std::string_view tooltip)
        {
            const bool changed = ImGui::InputTextWithHint(label, hint, buffer, bufferSize);
            ShowTooltip(tooltip);
            return changed;
        }

        bool ComboWithTooltip(
            const char* label,
            int* currentItem,
            const char* const items[],
            const int itemCount,
            const std::string_view tooltip)
        {
            const bool changed = ImGui::Combo(label, currentItem, items, itemCount);
            ShowTooltip(tooltip);
            return changed;
        }

        bool MenuItemWithTooltip(
            const char* label,
            const std::string_view tooltip,
            const bool enabled = true)
        {
            const bool activated = ImGui::MenuItem(label, nullptr, false, enabled);
            ShowTooltip(tooltip);
            return activated;
        }

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

        std::string TrimString(std::string value)
        {
            auto isWhitespace = [](const unsigned char c)
            {
                return std::isspace(c) != 0;
            };

            value.erase(
                value.begin(),
                std::find_if(
                    value.begin(),
                    value.end(),
                    [&](const unsigned char c)
                    {
                        return !isWhitespace(c);
                    }));

            value.erase(
                std::find_if(
                    value.rbegin(),
                    value.rend(),
                    [&](const unsigned char c)
                    {
                        return !isWhitespace(c);
                    }).base(),
                value.end());

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

        bool IsMeshAssetPathCandidate(const std::filesystem::path& path)
        {
            const std::string extension = ToLowerString(path.extension().string());
            return extension == ".lumamesh" ||
                extension == ".obj" ||
                extension == ".fbx" ||
                extension == ".gltf" ||
                extension == ".glb";
        }

        const char* ContentTypeLabel(const ContentItemType type)
        {
            switch (type)
            {
            case ContentItemType::Folder:
                return "Folder";
            case ContentItemType::Scene:
                return "Scene";
            case ContentItemType::Script:
                return "Script";
            case ContentItemType::Image:
                return "Image";
            case ContentItemType::Mesh:
                return "Mesh";
            case ContentItemType::Material:
                return "Material";
            case ContentItemType::Shader:
                return "Shader";
            case ContentItemType::Audio:
                return "Audio";
            case ContentItemType::Procedural:
                return "Procedural";
            case ContentItemType::Package:
                return "Package";
            case ContentItemType::Other:
            default:
                return "Asset";
            }
        }

        ImU32 ContentTypeColor(const ContentItemType type)
        {
            switch (type)
            {
            case ContentItemType::Folder:
                return IM_COL32(232, 146, 36, 255);
            case ContentItemType::Scene:
                return IM_COL32(90, 149, 255, 255);
            case ContentItemType::Script:
                return IM_COL32(86, 198, 142, 255);
            case ContentItemType::Image:
                return IM_COL32(70, 120, 190, 255);
            case ContentItemType::Mesh:
                return IM_COL32(130, 90, 170, 255);
            case ContentItemType::Material:
                return IM_COL32(110, 150, 90, 255);
            case ContentItemType::Shader:
                return IM_COL32(150, 70, 70, 255);
            case ContentItemType::Audio:
                return IM_COL32(170, 150, 70, 255);
            case ContentItemType::Procedural:
                return IM_COL32(95, 135, 185, 255);
            case ContentItemType::Package:
                return IM_COL32(150, 110, 190, 255);
            case ContentItemType::Other:
            default:
                return IM_COL32(90, 90, 90, 255);
            }
        }
    }

    void ContentBrowserPanel::Draw(ContentBrowserPanelContext& context)
    {
        if (context.roots == nullptr ||
            context.activeRootIndex == nullptr ||
            context.activeRootPath == nullptr ||
            context.activeRootLabel == nullptr ||
            context.activeRootSource == nullptr ||
            context.activeRootBadge == nullptr ||
            context.activeRootReadOnly == nullptr ||
            context.currentRelativePath == nullptr ||
            context.currentRelativeString == nullptr ||
            context.selectedEntry == nullptr ||
            context.status == nullptr ||
            context.cache == nullptr)
        {
            ImGui::TextDisabled("Content Browser context is incomplete.");
            return;
        }

        const auto& roots = *context.roots;
        auto& activeRootIndex = *context.activeRootIndex;
        const auto& activeRootPath = *context.activeRootPath;
        const auto& activeRootLabel = *context.activeRootLabel;
        const auto& activeRootSource = *context.activeRootSource;
        const auto& activeRootBadge = *context.activeRootBadge;
        const bool activeRootReadOnly = *context.activeRootReadOnly;
        auto& currentRelativePath = *context.currentRelativePath;
        auto& currentRelativeString = *context.currentRelativeString;
        auto& selectedEntry = *context.selectedEntry;
        auto& status = *context.status;
        ContentBrowserCache& cache = *context.cache;
        bool requestCreateScriptPopup = false;

        constexpr float leftPaneWidth = 240.0f;
        ImGui::BeginChild("##ContentBrowserLeftPane", ImVec2(leftPaneWidth, 0.0f), true);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.12f, 0.14f, 1.0f));
        ImGui::BeginChild("##ContentBrowserSidebarHeader", ImVec2(0.0f, 32.0f), false);
        ImGui::TextUnformatted("Content Browser");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.74f, 0.80f, 1.0f));
        ImGui::TextUnformatted("Favorites");
        ImGui::PopStyleColor();

        bool drewFavoriteRoot = false;
        for (std::size_t rootIndex = 0; rootIndex < roots.size(); ++rootIndex)
        {
            const ContentBrowserRootView& root = roots[rootIndex];
            if (root.packageRoot)
            {
                continue;
            }

            drewFavoriteRoot = true;
            const std::string label =
                root.badge.empty()
                    ? root.label + "##Root" + root.id
                    : root.label + " [" + root.badge + "]##Root" + root.id;
            if (ImGui::Selectable(label.c_str(), static_cast<int>(rootIndex) == activeRootIndex))
            {
                if (context.activateRoot)
                {
                    context.activateRoot(static_cast<int>(rootIndex));
                }
            }
            ShowTooltip(
                "Open " + root.label + " (" + root.source + ", " +
                (root.readOnly ? "read-only" : "read/write") + ").");
        }
        if (!drewFavoriteRoot)
        {
            ImGui::TextDisabled("No writable project root.");
        }

        bool hasPackageRoots = false;
        for (const ContentBrowserRootView& root : roots)
        {
            if (root.packageRoot)
            {
                hasPackageRoots = true;
                break;
            }
        }

        if (hasPackageRoots)
        {
            ImGui::Separator();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.74f, 0.80f, 1.0f));
            ImGui::TextUnformatted("Packages");
            ImGui::PopStyleColor();

            for (std::size_t rootIndex = 0; rootIndex < roots.size(); ++rootIndex)
            {
                const ContentBrowserRootView& root = roots[rootIndex];
                if (!root.packageRoot)
                {
                    continue;
                }

                const std::string label =
                    root.badge.empty()
                        ? root.label + "##Root" + root.id
                        : root.label + " [" + root.badge + "]##Root" + root.id;
                if (ImGui::Selectable(label.c_str(), static_cast<int>(rootIndex) == activeRootIndex))
                {
                    if (context.activateRoot)
                    {
                        context.activateRoot(static_cast<int>(rootIndex));
                    }
                }
                ShowTooltip(
                    "Open mounted package root for " + root.source + " (" +
                    (root.readOnly ? "read-only" : "read/write") + ").");
            }
        }

        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.74f, 0.80f, 1.0f));
        ImGui::TextUnformatted("Folders");
        ImGui::PopStyleColor();

        if (cache.IsContentFolderTreeDirty() ||
            NormalizePathForComparison(cache.GetContentFolderTreeRootPath()) != NormalizePathForComparison(activeRootPath))
        {
            if (context.requestFolderTreeRebuild)
            {
                context.requestFolderTreeRebuild(activeRootPath);
            }
        }

        const std::vector<ContentFolderTreeNode>& contentFolderTreeNodes = cache.GetContentFolderTreeNodes();

        std::function<void(std::size_t)> drawFolderTree;
        drawFolderTree = [&](const std::size_t nodeIndex)
        {
            if (nodeIndex >= contentFolderTreeNodes.size())
            {
                return;
            }

            const ContentFolderTreeNode& node = contentFolderTreeNodes[nodeIndex];
            for (const std::size_t childIndex : node.children)
            {
                if (childIndex >= contentFolderTreeNodes.size())
                {
                    continue;
                }

                const ContentFolderTreeNode& childNode = contentFolderTreeNodes[childIndex];
                const std::filesystem::path childRelative = childNode.relativePath;
                const std::string& childRelativeString = childNode.relativePathString;
                const bool isCurrentDirectory = childRelativeString == currentRelativeString;
                const bool isCurrentOrAncestor =
                    isCurrentDirectory ||
                    (!childRelativeString.empty() &&
                        currentRelativeString.size() > childRelativeString.size() &&
                        currentRelativeString.compare(0, childRelativeString.size(), childRelativeString) == 0 &&
                        currentRelativeString[childRelativeString.size()] == '/');

                ImGuiTreeNodeFlags flags =
                    ImGuiTreeNodeFlags_OpenOnArrow |
                    ImGuiTreeNodeFlags_OpenOnDoubleClick |
                    ImGuiTreeNodeFlags_SpanAvailWidth;
                if (isCurrentDirectory)
                {
                    flags |= ImGuiTreeNodeFlags_Selected;
                }
                if (isCurrentOrAncestor)
                {
                    flags |= ImGuiTreeNodeFlags_DefaultOpen;
                }

                const bool opened = ImGui::TreeNodeEx(
                    childNode.treeNodeId.c_str(),
                    flags,
                    "%s",
                    childNode.name.c_str());
                ShowTooltip(
                    std::string("Open folder in ") + activeRootLabel +
                    (activeRootReadOnly ? " (read-only)." : "."));
                if (ImGui::IsItemClicked() && context.openDirectory)
                {
                    context.openDirectory(activeRootPath / childRelative);
                }

                if (opened)
                {
                    drawFolderTree(childIndex);
                    ImGui::TreePop();
                }
            }
        };

        if (cache.IsContentFolderTreeBuildInFlight() && contentFolderTreeNodes.empty())
        {
            ImGui::TextDisabled("Loading folders...");
        }
        else
        {
            drawFolderTree(0);
        }

        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70f, 0.74f, 0.80f, 1.0f));
        ImGui::TextUnformatted("Root");
        ImGui::PopStyleColor();
        ImGui::TextUnformatted(activeRootLabel.c_str());
        ImGui::TextWrapped("%s", activeRootPath.string().c_str());
        if (!activeRootSource.empty())
        {
            ImGui::TextDisabled("%s | %s", activeRootSource.c_str(), activeRootReadOnly ? "Read-only" : "Writable");
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##ContentBrowserRightPane", ImVec2(0.0f, 0.0f), true);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.11f, 0.12f, 0.14f, 1.0f));
        ImGui::BeginChild("##ContentBrowserToolbar", ImVec2(0.0f, 36.0f), false);

        auto breadcrumbButton = [](const char* label) -> bool
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0f, 1.0f, 1.0f, 0.10f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0f, 1.0f, 1.0f, 0.16f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 2.0f));
            const bool clicked = ButtonWithTooltip(label, "Open this breadcrumb folder.");
            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(3);
            return clicked;
        };

        bool hasPendingBreadcrumbOpen = false;
        std::filesystem::path pendingBreadcrumbOpenDirectory;
        if (breadcrumbButton(activeRootLabel.c_str()))
        {
            pendingBreadcrumbOpenDirectory = activeRootPath;
            hasPendingBreadcrumbOpen = true;
        }

        std::filesystem::path breadcrumbPath;
        int breadcrumbIndex = 0;
        for (const auto& part : currentRelativePath)
        {
            if (part.empty())
            {
                continue;
            }

            breadcrumbPath /= part;
            ImGui::SameLine();
            ImGui::TextDisabled(">");
            ImGui::SameLine();
            ImGui::PushID(breadcrumbIndex++);
            if (breadcrumbButton(part.string().c_str()))
            {
                pendingBreadcrumbOpenDirectory = activeRootPath / breadcrumbPath;
                hasPendingBreadcrumbOpen = true;
            }
            ImGui::PopID();
        }

        if (hasPendingBreadcrumbOpen && context.openDirectory)
        {
            context.openDirectory(pendingBreadcrumbOpenDirectory);
        }

        constexpr float filterWidth = 170.0f;
        constexpr float searchWidth = 280.0f;
        const float toolbarRemainingWidth = ImGui::GetContentRegionAvail().x;
        if (toolbarRemainingWidth > filterWidth + searchWidth + 20.0f)
        {
            ImGui::SameLine();
            ImGui::SetCursorPosX(
                ImGui::GetCursorPosX() +
                toolbarRemainingWidth -
                (filterWidth + searchWidth + 12.0f));
        }
        else
        {
            ImGui::SameLine();
        }

        std::array<char, 256> searchBuffer {};
        std::snprintf(searchBuffer.data(), searchBuffer.size(), "%s", cache.GetSearchQuery().c_str());
        ImGui::SetNextItemWidth(searchWidth);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.04f, 0.04f, 0.04f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 2.0f));
        if (InputTextWithHintWithTooltip(
                "##ContentSearch",
                "Search...",
                searchBuffer.data(),
                searchBuffer.size(),
                "Search assets in the current folder by file name."))
        {
            cache.SetSearchQuery(searchBuffer.data());
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        const char* typeFilters[] = {
            "All",
            "Folder",
            "Scene",
            "Script",
            "Image",
            "Mesh",
            "Material",
            "Shader",
            "Audio",
            "Procedural",
            "Package",
            "Other"
        };
        int contentTypeFilterIndex = std::clamp(
            cache.GetTypeFilterIndex(),
            0,
            static_cast<int>(IM_ARRAYSIZE(typeFilters) - 1));
        ImGui::SetNextItemWidth(filterWidth);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.04f, 0.04f, 0.04f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 14.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 2.0f));
        if (ComboWithTooltip(
                "##ContentTypeFilter",
                &contentTypeFilterIndex,
                typeFilters,
                IM_ARRAYSIZE(typeFilters),
                "Filter visible assets by content type."))
        {
            cache.SetTypeFilterIndex(contentTypeFilterIndex);
        }
        else
        {
            cache.SetTypeFilterIndex(contentTypeFilterIndex);
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::BeginChild("##ContentBrowserAssetList", ImVec2(0.0f, 0.0f), true);

        cache.RebuildFilteredContentEntries();
        cache.BeginThumbnailRequestFrame();
        const std::vector<std::size_t>& filteredContentEntryIndices = cache.GetFilteredContentEntryIndices();
        std::vector<ContentBrowserEntry>& contentEntryMetadata = cache.GetContentEntryMetadata();
        const std::vector<std::filesystem::path>& contentEntries = cache.GetContentEntries();

        std::filesystem::path pendingDirectory;
        std::filesystem::path pendingScenePath;
        constexpr float tileSize = 84.0f;
        constexpr float tileStride = tileSize + 22.0f;
        const float tileRowHeight = tileSize + std::max(28.0f, ImGui::GetTextLineHeightWithSpacing() * 2.2f);
        const float regionWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        int columns = static_cast<int>(regionWidth / tileStride);
        columns = std::max(columns, 1);

        int visibleIndex = 0;
        const int filteredCount = static_cast<int>(filteredContentEntryIndices.size());
        const int totalRows = filteredCount == 0 ? 0 : ((filteredCount + columns - 1) / columns);
        ImGuiListClipper clipper;
        clipper.Begin(totalRows, tileRowHeight);
        while (clipper.Step())
        {
            for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd; ++rowIndex)
            {
                const int rowStartIndex = rowIndex * columns;
                const int rowEndIndex = std::min(rowStartIndex + columns, filteredCount);
                for (int filteredIndex = rowStartIndex; filteredIndex < rowEndIndex; ++filteredIndex)
                {
                    const std::size_t metadataIndex = filteredContentEntryIndices[static_cast<std::size_t>(filteredIndex)];
                    if (metadataIndex >= contentEntryMetadata.size())
                    {
                        continue;
                    }

                    ContentBrowserEntry& entry = contentEntryMetadata[metadataIndex];
                    const std::filesystem::path& entryPath = entry.path;
                    const bool isDirectory = entry.isDirectory;
                    const ContentItemType itemType = static_cast<ContentItemType>(entry.type);
                    const std::string& entryName = entry.name;
                    const std::string relativeEntryPath =
                        currentRelativeString.empty() ? entryName : (currentRelativeString + "/" + entryName);

                    if (filteredIndex > rowStartIndex)
                    {
                        ImGui::SameLine();
                    }

                    const bool selected = selectedEntry == entryPath;

                    ImGui::PushID(entry.tileId.c_str());
                    ImGui::BeginGroup();

                    const ImVec2 tileStart = ImGui::GetCursorScreenPos();
                    const ImVec2 tileButtonSize(tileSize, tileSize);
                    const bool clicked = ImGui::InvisibleButton("##ContentTile", tileButtonSize);
                    const bool hovered = ImGui::IsItemHovered();
                    ShowTooltip("Click to select. Double-click a folder or scene to open.");

                    if (!isDirectory &&
                        ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                    {
                        ImGui::SetDragDropPayload(
                            "CONTENT_BROWSER_ASSET_PATH",
                            entry.genericPath.c_str(),
                            entry.genericPath.size() + 1);
                        ImGui::Text("%s", IsMeshAssetPathCandidate(entryPath) ? "Create / Assign Mesh" : "Assign Asset");
                        ImGui::TextDisabled("%s", entryName.c_str());
                        ImGui::EndDragDropSource();
                    }

                    if (clicked)
                    {
                        selectedEntry = entryPath;
                        status = (isDirectory ? "Selected folder: " : "Selected asset: ") + entryName;
                    }

                    if (isDirectory && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        pendingDirectory = entryPath;
                    }
                    else if (!isDirectory &&
                             itemType == ContentItemType::Scene &&
                             hovered &&
                             ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        pendingScenePath = entryPath;
                    }

                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    const ImU32 borderColor = selected ? IM_COL32(70, 160, 255, 255) : IM_COL32(80, 80, 80, 255);
                    drawList->AddRect(
                        tileStart,
                        ImVec2(tileStart.x + tileSize, tileStart.y + tileSize),
                        borderColor,
                        6.0f,
                        0,
                        selected ? 2.5f : 1.0f);

                    const ImVec2 innerMin(tileStart.x + 4.0f, tileStart.y + 4.0f);
                    const ImVec2 innerMax(tileStart.x + tileSize - 4.0f, tileStart.y + tileSize - 4.0f);
                    void* thumbnailTexture = cache.GetOrCreateThumbnail(context.thumbnailRenderer, entry);
                    if (isDirectory)
                    {
                        if (thumbnailTexture != nullptr)
                        {
                            drawList->AddImage(
                                reinterpret_cast<ImTextureID>(thumbnailTexture),
                                innerMin,
                                innerMax,
                                ImVec2(0.0f, 0.0f),
                                ImVec2(1.0f, 1.0f),
                                IM_COL32(96, 242, 255, 255));
                        }
                        else
                        {
                            drawList->AddRectFilled(
                                ImVec2(innerMin.x + 2.0f, innerMin.y + 18.0f),
                                innerMax,
                                IM_COL32(55, 180, 198, 255),
                                4.0f);
                            drawList->AddRectFilled(
                                innerMin,
                                ImVec2(innerMin.x + tileSize * 0.52f, innerMin.y + 28.0f),
                                IM_COL32(96, 242, 255, 255),
                                3.0f);
                        }
                    }
                    else if (thumbnailTexture != nullptr)
                    {
                        drawList->AddImage(
                            reinterpret_cast<ImTextureID>(thumbnailTexture),
                            innerMin,
                            innerMax,
                            ImVec2(0.0f, 0.0f),
                            ImVec2(1.0f, 1.0f),
                            IM_COL32(255, 255, 255, 255));
                    }
                    else
                    {
                        const ImU32 typeColor = ContentTypeColor(itemType);
                        drawList->AddRectFilled(innerMin, innerMax, typeColor, 4.0f);
                        const char* typeLabel = ContentTypeLabel(itemType);
                        const ImVec2 typeTextSize = ImGui::CalcTextSize(typeLabel);
                        drawList->AddText(
                            ImVec2(
                                tileStart.x + (tileSize - typeTextSize.x) * 0.5f,
                                tileStart.y + (tileSize - typeTextSize.y) * 0.5f),
                            IM_COL32(230, 230, 230, 255),
                            typeLabel);
                    }

                    if (hovered)
                    {
                        ImGui::BeginTooltip();
                        ImGui::Text("Type: %s", ContentTypeLabel(itemType));
                        ImGui::Text("Name: %s", entryName.c_str());
                        ImGui::Text("Root: %s", activeRootLabel.c_str());
                        ImGui::Text("Access: %s", activeRootReadOnly ? "Read-only" : "Writable");
                        if (!activeRootSource.empty())
                        {
                            ImGui::Text("Source: %s", activeRootSource.c_str());
                        }
                        if (!isDirectory && entry.hasByteSize)
                        {
                            ImGui::Text("Size: %llu bytes", static_cast<unsigned long long>(entry.byteSize));
                        }
                        ImGui::Text(
                            "Path: %s",
                            relativeEntryPath.empty() ? entry.genericPath.c_str() : relativeEntryPath.c_str());
                        ImGui::EndTooltip();
                    }

                    if (ImGui::BeginPopupContextItem("ContentBrowserItemMenu"))
                    {
                        if (MenuItemWithTooltip("Open", "Open this folder, scene, or asset."))
                        {
                            if (isDirectory)
                            {
                                pendingDirectory = entryPath;
                            }
                            else if (itemType == ContentItemType::Scene)
                            {
                                pendingScenePath = entryPath;
                            }
                            else if (context.openAsset)
                            {
                                context.openAsset(entryPath, entryName);
                            }
                            else
                            {
                                status = "Open asset: " + entryName;
                            }
                        }

                        if (MenuItemWithTooltip("Copy Relative Path", "Copy the asset path relative to the active root."))
                        {
                            const std::string& copyText = relativeEntryPath.empty() ? entry.genericPath : relativeEntryPath;
                            ImGui::SetClipboardText(copyText.c_str());
                        }

                        if (MenuItemWithTooltip("Refresh", "Refresh the current folder contents."))
                        {
                            if (context.refresh)
                            {
                                context.refresh();
                            }
                        }

                        ImGui::EndPopup();
                    }

                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tileSize);
                    ImGui::TextUnformatted(entryName.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::EndGroup();
                    ImGui::PopID();

                    ++visibleIndex;
                }
            }
        }
        clipper.End();

        if (!pendingDirectory.empty() && context.openDirectory)
        {
            context.openDirectory(pendingDirectory);
        }
        if (!pendingScenePath.empty() && context.requestLoadScene)
        {
            context.requestLoadScene(pendingScenePath);
        }

        if (visibleIndex == 0)
        {
            if (cache.IsContentEntriesRefreshInFlight() && contentEntries.empty())
            {
                ImGui::TextDisabled("Loading folder...");
            }
            else if (contentEntries.empty())
            {
                ImGui::TextDisabled("This folder is empty.");
            }
            else
            {
                ImGui::TextDisabled("No assets match the current filter.");
            }
        }

        if (ImGui::BeginPopupContextWindow(
                "ContentBrowserEmptySpaceMenu",
                ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (activeRootReadOnly)
            {
                ImGui::TextDisabled("Mounted package content is read-only.");
                ImGui::Separator();
            }
            else if (ImGui::BeginMenu("Create"))
            {
                if (MenuItemWithTooltip("New Folder", "Create a new folder in the current directory."))
                {
                    if (context.createFolder)
                    {
                        context.createFolder();
                    }
                }
                if (MenuItemWithTooltip("New Script", "Create a new Lua script in the current directory."))
                {
                    std::snprintf(
                        m_CreateScriptNameBuffer.data(),
                        m_CreateScriptNameBuffer.size(),
                        "%s",
                        "NewScript");
                    m_FocusCreateScriptName = true;
                    requestCreateScriptPopup = true;
                }
                ImGui::EndMenu();
            }

            if (MenuItemWithTooltip("Refresh", "Refresh the current folder contents."))
            {
                if (context.refresh)
                {
                    context.refresh();
                }
            }

            ImGui::EndPopup();
        }

        if (requestCreateScriptPopup)
        {
            ImGui::OpenPopup("Create New Script");
        }

        bool closeCreateScriptPopup = false;
        if (ImGui::BeginPopupModal("Create New Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Create a Lua script in the current folder.");
            std::string targetFolderLabel = activeRootLabel.empty() ? "Content" : activeRootLabel;
            if (!currentRelativeString.empty())
            {
                targetFolderLabel += "/";
                targetFolderLabel += currentRelativeString;
            }
            ImGui::TextDisabled("%s", targetFolderLabel.c_str());
            ImGui::TextDisabled(".lua will be added automatically.");
            ImGui::Spacing();

            if (m_FocusCreateScriptName)
            {
                ImGui::SetKeyboardFocusHere();
                m_FocusCreateScriptName = false;
            }

            const bool submitted = ImGui::InputTextWithHint(
                "##NewScriptName",
                "Script Name",
                m_CreateScriptNameBuffer.data(),
                m_CreateScriptNameBuffer.size(),
                ImGuiInputTextFlags_EnterReturnsTrue);
            ShowTooltip("Enter the new script file name. The engine will create a .lua file.");

            ImGui::Spacing();

            if (ImGui::Button("Create Script", ImVec2(120.0f, 0.0f)) || submitted)
            {
                const std::string requestedName = TrimString(m_CreateScriptNameBuffer.data());
                if (context.createScript)
                {
                    context.createScript(requestedName);
                }
                closeCreateScriptPopup = true;
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
            {
                closeCreateScriptPopup = true;
            }

            if (closeCreateScriptPopup)
            {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        if (!status.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", status.c_str());
        }

        ImGui::Spacing();
        std::string shownPath = activeRootLabel.empty() ? "Content" : activeRootLabel;
        if (!currentRelativeString.empty())
        {
            shownPath += "/";
            shownPath += currentRelativeString;
        }
        if (!activeRootBadge.empty())
        {
            shownPath += " [";
            shownPath += activeRootBadge;
            shownPath += "]";
        }
        ImGui::TextDisabled("%s", shownPath.c_str());

        ImGui::EndChild();
        ImGui::EndChild();
    }
}
