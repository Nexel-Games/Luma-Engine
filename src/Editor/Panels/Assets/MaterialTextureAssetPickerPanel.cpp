#include "Luma/Editor/Panels/Assets/MaterialTextureAssetPickerPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

#include <imgui.h>
#include <GLFW/glfw3.h>

#include "Luma/Editor/Assets/MaterialTextureAssetPickerService.h"
#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Input/Input.h"

namespace Luma::Editor
{
    namespace
    {
        enum class AssetPickerKind
        {
            Material,
            Texture
        };

        std::string ToLowerString(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }

        std::string TrimCopy(std::string value)
        {
            const auto notWhitespace = [](const unsigned char character)
            {
                return !std::isspace(character);
            };

            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notWhitespace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notWhitespace).base(), value.end());
            return value;
        }

        std::string TruncateMiddle(const std::string_view value, const std::size_t maxLength)
        {
            if (value.size() <= maxLength)
            {
                return std::string(value);
            }
            if (maxLength <= 3)
            {
                return std::string(value.substr(0, maxLength));
            }

            const std::size_t prefixLength = (maxLength - 3) / 2;
            const std::size_t suffixLength = maxLength - 3 - prefixLength;
            return std::string(value.substr(0, prefixLength)) + "..." +
                std::string(value.substr(value.size() - suffixLength));
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

        void ShowItemTooltip(const std::string_view tooltip)
        {
            UI::Tooltip::Show(tooltip);
        }

        bool ButtonWithTooltip(const char* label, const ImVec2 size = ImVec2(0.0f, 0.0f))
        {
            const bool pressed = ImGui::Button(label, size);
            UI::Tooltip::ShowForItemLabel(label);
            return pressed;
        }

        bool SmallButtonWithTooltip(const char* label)
        {
            const bool pressed = ImGui::SmallButton(label);
            UI::Tooltip::ShowForItemLabel(label);
            return pressed;
        }

        bool InputTextWithHintWithTooltip(
            const char* label,
            const char* hint,
            char* buffer,
            const std::size_t bufferSize)
        {
            const bool changed = ImGui::InputTextWithHint(label, hint, buffer, bufferSize);
            const std::string visibleLabel = UI::Tooltip::VisibleLabel(label);
            if (!visibleLabel.empty())
            {
                ShowItemTooltip("Edit " + visibleLabel);
            }
            else if (hint != nullptr && hint[0] != '\0')
            {
                ShowItemTooltip(hint);
            }
            return changed;
        }

        void DrawAssetPickerGrid(
            const MaterialTextureAssetPickerPanelContext& context,
            const char* childId,
            const std::vector<std::filesystem::path>& entries,
            const std::string& query,
            const char* emptyText,
            std::string& selection,
            std::string& value)
        {
            ImGui::BeginChild(childId, ImVec2(520.0f, 300.0f), true);
            const std::string loweredQuery = ToLowerString(query);
            const float tileWidth = 96.0f;
            const float tileSpacing = 10.0f;
            const float thumbnailSize = 64.0f;
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const int columnCount =
                std::max(1, static_cast<int>((availableWidth + tileSpacing) / (tileWidth + tileSpacing)));

            bool anyMatch = false;
            if (ImGui::BeginTable("##AssetPickerGrid", columnCount, ImGuiTableFlags_SizingFixedFit))
            {
                for (const std::filesystem::path& entryPath : entries)
                {
                    std::filesystem::path displayPath = entryPath;
                    if (context.projectLoaded && !context.projectAssetsPath.empty())
                    {
                        std::error_code relError;
                        const std::filesystem::path relativePath =
                            std::filesystem::relative(entryPath, context.projectAssetsPath, relError);
                        if (!relError && !relativePath.empty())
                        {
                            displayPath = relativePath;
                        }
                    }

                    const std::string displayText = displayPath.generic_string();
                    const std::string searchText =
                        ToLowerString(entryPath.filename().string() + " " + displayText);
                    if (!loweredQuery.empty() && searchText.find(loweredQuery) == std::string::npos)
                    {
                        continue;
                    }

                    anyMatch = true;
                    ImGui::TableNextColumn();
                    ImGui::PushID(displayText.c_str());
                    const bool isAssigned =
                        value == displayPath.generic_string() ||
                        NormalizePathForComparison(context.resolveAssetPath ? context.resolveAssetPath(value) : std::filesystem::path {})
                            == NormalizePathForComparison(entryPath);
                    const bool isSelected =
                        selection == displayPath.generic_string() ||
                        NormalizePathForComparison(context.resolveAssetPath ? context.resolveAssetPath(selection) : std::filesystem::path {})
                            == NormalizePathForComparison(entryPath) ||
                        (selection.empty() && isAssigned);

                    const float cellStartX = ImGui::GetCursorPosX();
                    const ImVec2 tileMin = ImGui::GetCursorScreenPos();
                    void* thumbnailTexture = context.getThumbnail ? context.getThumbnail(entryPath, false) : nullptr;
                    bool assignAsset = false;
                    if (thumbnailTexture != nullptr)
                    {
                        const bool pressed = ImGui::ImageButton(
                            "##Thumb",
                            reinterpret_cast<ImTextureID>(thumbnailTexture),
                            ImVec2(thumbnailSize, thumbnailSize),
                            ImVec2(0.0f, 1.0f),
                            ImVec2(1.0f, 0.0f));
                        if (pressed)
                        {
                            selection = displayPath.generic_string();
                        }
                    }
                    else
                    {
                        const bool pressed = ImGui::Button("##Thumb", ImVec2(thumbnailSize, thumbnailSize));
                        if (pressed)
                        {
                            selection = displayPath.generic_string();
                        }
                    }
                    const ImVec2 thumbMin = ImGui::GetItemRectMin();
                    const ImVec2 thumbMax = ImGui::GetItemRectMax();
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        selection = displayPath.generic_string();
                        assignAsset = true;
                    }
                    if (ImGui::IsItemHovered())
                    {
                        UI::Tooltip::Show(displayText);
                    }

                    const std::string fileName = TruncateMiddle(entryPath.filename().string(), 18);
                    const float textWidth = ImGui::CalcTextSize(fileName.c_str()).x;
                    const float textOffset = std::max(0.0f, (thumbnailSize - textWidth) * 0.5f);
                    ImGui::SetCursorPosX(cellStartX + textOffset);
                    if (ImGui::Selectable(
                            fileName.c_str(),
                            isSelected,
                            ImGuiSelectableFlags_AllowDoubleClick,
                            ImVec2(thumbnailSize, 0.0f)))
                    {
                        selection = displayPath.generic_string();
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        {
                            assignAsset = true;
                        }
                    }
                    const ImVec2 tileMax = ImGui::GetItemRectMax();
                    if (ImGui::IsItemHovered())
                    {
                        UI::Tooltip::Show(displayText);
                    }

                    if (isSelected)
                    {
                        ImDrawList* drawList = ImGui::GetWindowDrawList();
                        const ImU32 borderColor = ImGui::GetColorU32(ImGuiCol_ButtonActive);
                        const ImU32 fillColor = ImGui::GetColorU32(ImVec4(0.20f, 0.36f, 0.62f, 0.12f));
                        drawList->AddRectFilled(tileMin, tileMax, fillColor, 6.0f);
                        drawList->AddRect(tileMin, tileMax, borderColor, 6.0f, 0, 2.0f);
                        drawList->AddRect(thumbMin, thumbMax, borderColor, 4.0f, 0, 2.0f);
                    }

                    if (assignAsset)
                    {
                        value = displayPath.generic_string();
                        selection = value;
                        if (context.markSceneRenderCacheDirty)
                        {
                            context.markSceneRenderCacheDirty();
                        }
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::PopID();
                }

                ImGui::EndTable();
            }

            if (!anyMatch)
            {
                ImGui::TextDisabled("%s", emptyText);
            }

            ImGui::EndChild();
        }

        void DrawSelector(
            const MaterialTextureAssetPickerPanelContext& context,
            const AssetPickerKind kind,
            const char* label,
            std::string& value,
            const char* popupId,
            const std::string_view tooltip)
        {
            if (context.pickerService == nullptr)
            {
                return;
            }

            AssetPickerState& pickerState =
                kind == AssetPickerKind::Material ? context.pickerService->Material() : context.pickerService->Texture();
            const bool showVisibleLabel = !(label[0] == '#' && label[1] == '#');
            if (showVisibleLabel)
            {
                ImGui::TextUnformatted(label);
                ShowItemTooltip(tooltip);
                ImGui::SameLine(150.0f);
            }

            const std::filesystem::path resolvedPath =
                context.resolveAssetPath ? context.resolveAssetPath(value) : std::filesystem::path {};
            void* thumbnailTexture = nullptr;
            if (!value.empty() && !resolvedPath.empty() && context.getThumbnail)
            {
                thumbnailTexture = context.getThumbnail(resolvedPath, false);
            }

            const float previewSize = ImGui::GetFrameHeight();
            if (thumbnailTexture != nullptr)
            {
                ImGui::Image(
                    reinterpret_cast<ImTextureID>(thumbnailTexture),
                    ImVec2(previewSize, previewSize),
                    ImVec2(0.0f, 1.0f),
                    ImVec2(1.0f, 0.0f));
            }
            else
            {
                const std::string previewId =
                    kind == AssetPickerKind::Material
                        ? "##MaterialPreview" + std::string(popupId)
                        : "##TexturePreview" + std::string(popupId);
                ImGui::Button(previewId.c_str(), ImVec2(previewSize, previewSize));
            }
            ShowItemTooltip(tooltip);
            ImGui::SameLine();

            const std::string buttonText =
                value.empty()
                    ? std::string("None")
                    : TruncateMiddle(std::filesystem::path(value).filename().string(), 24);
            const float clearButtonWidth = value.empty() ? 0.0f : 26.0f;
            const float pickerButtonWidth = 24.0f;
            const float objectFieldWidth = std::max(80.0f, ImGui::GetContentRegionAvail().x - pickerButtonWidth - clearButtonWidth - 8.0f);

            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgActive));
            const std::string buttonLabel = buttonText + "##ObjectField" + popupId;
            if (ButtonWithTooltip(buttonLabel.c_str(), ImVec2(objectFieldWidth, 0.0f)))
            {
                pickerState.query.clear();
                pickerState.selection = value;
                if (context.roots != nullptr)
                {
                    if (kind == AssetPickerKind::Material)
                    {
                        context.pickerService->RefreshMaterialEntries(*context.roots);
                    }
                    else
                    {
                        context.pickerService->RefreshTextureEntries(*context.roots);
                    }
                }
                ImGui::OpenPopup(popupId);
            }
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            ShowItemTooltip(tooltip);

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ASSET_PATH"))
                {
                    if (payload->Data != nullptr && payload->DataSize > 1)
                    {
                        const std::filesystem::path assetPath(static_cast<const char*>(payload->Data));
                        const bool validAsset =
                            kind == AssetPickerKind::Material
                                ? MaterialTextureAssetPickerService::IsMaterialAssetPathCandidate(assetPath)
                                : MaterialTextureAssetPickerService::IsTextureAssetPathCandidate(assetPath);
                        if (validAsset)
                        {
                            value = assetPath.generic_string();
                            if (context.markSceneRenderCacheDirty)
                            {
                                context.markSceneRenderCacheDirty();
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::SameLine();
            if (SmallButtonWithTooltip(("o##Pick" + std::string(popupId)).c_str()))
            {
                pickerState.query.clear();
                pickerState.selection = value;
                if (context.roots != nullptr)
                {
                    if (kind == AssetPickerKind::Material)
                    {
                        context.pickerService->RefreshMaterialEntries(*context.roots);
                    }
                    else
                    {
                        context.pickerService->RefreshTextureEntries(*context.roots);
                    }
                }
                ImGui::OpenPopup(popupId);
            }
            ShowItemTooltip(kind == AssetPickerKind::Material ? "Open the material picker." : "Open the texture picker.");

            if (!value.empty())
            {
                ImGui::SameLine();
                if (SmallButtonWithTooltip(("Clear##" + std::string(popupId)).c_str()))
                {
                    value.clear();
                    if (context.markSceneRenderCacheDirty)
                    {
                        context.markSceneRenderCacheDirty();
                    }
                }
            }

            if (ImGui::BeginPopup(popupId))
            {
                std::array<char, 256> searchBuffer {};
                std::snprintf(searchBuffer.data(), searchBuffer.size(), "%s", pickerState.query.c_str());
                if (InputTextWithHintWithTooltip(
                        kind == AssetPickerKind::Material ? "##MaterialAssetPickerSearch" : "##TextureAssetPickerSearch",
                        kind == AssetPickerKind::Material ? "Search materials..." : "Search textures...",
                        searchBuffer.data(),
                        searchBuffer.size()))
                {
                    pickerState.query = searchBuffer.data();
                }

                DrawAssetPickerGrid(
                    context,
                    kind == AssetPickerKind::Material ? "##MaterialAssetPickerGrid" : "##TextureAssetPickerGrid",
                    pickerState.entries,
                    pickerState.query,
                    kind == AssetPickerKind::Material
                        ? "No material assets match the current filter."
                        : "No texture assets match the current filter.",
                    pickerState.selection,
                    value);
                const bool enterPressed =
                    Input::WasKeyPressed(KeyCode::Enter) || Input::WasRawKeyPressed(GLFW_KEY_KP_ENTER);
                const bool escapePressed = Input::WasKeyPressed(KeyCode::Escape);
                ImGui::Separator();
                const bool canSelect = !TrimCopy(pickerState.selection).empty();
                if (!canSelect)
                {
                    ImGui::BeginDisabled();
                }
                const std::string confirmLabel =
                    kind == AssetPickerKind::Material
                        ? "Select##MaterialAssetPickerConfirm"
                        : "Select##TextureAssetPickerConfirm";
                if (ButtonWithTooltip(confirmLabel.c_str()) || (canSelect && enterPressed))
                {
                    value = pickerState.selection;
                    if (context.markSceneRenderCacheDirty)
                    {
                        context.markSceneRenderCacheDirty();
                    }
                    ImGui::CloseCurrentPopup();
                }
                if (!canSelect)
                {
                    ImGui::EndDisabled();
                }
                ImGui::SameLine();
                const std::string cancelLabel =
                    kind == AssetPickerKind::Material
                        ? "Cancel##MaterialAssetPickerCancel"
                        : "Cancel##TextureAssetPickerCancel";
                if (ButtonWithTooltip(cancelLabel.c_str()) || escapePressed)
                {
                    pickerState.selection.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
    }

    void MaterialTextureAssetPickerPanel::DrawMaterialSelector(
        const MaterialTextureAssetPickerPanelContext& context,
        const char* label,
        std::string& value,
        const char* popupId,
        const std::string_view tooltip)
    {
        DrawSelector(context, AssetPickerKind::Material, label, value, popupId, tooltip);
    }

    void MaterialTextureAssetPickerPanel::DrawTextureSelector(
        const MaterialTextureAssetPickerPanelContext& context,
        const char* label,
        std::string& value,
        const char* popupId,
        const std::string_view tooltip)
    {
        DrawSelector(context, AssetPickerKind::Texture, label, value, popupId, tooltip);
    }
}
