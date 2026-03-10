#include "Luma/Editor/Panels/Assets/MeshAssetPickerPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

#include <imgui.h>

#include "Luma/Editor/Assets/MeshAssetPickerService.h"
#include "Luma/Editor/UI/TooltipAPI.h"

namespace Luma::Editor
{
    namespace
    {
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

        void ShowItemTooltip(const std::string_view tooltip)
        {
            UI::Tooltip::Show(tooltip);
        }

        void ShowItemTooltipFromLabel(const char* label, const char* prefix = nullptr)
        {
            UI::Tooltip::ShowForItemLabel(label, prefix == nullptr ? std::string_view {} : std::string_view(prefix));
        }

        template <typename... Args>
        bool ButtonWithTooltip(const char* label, Args&&... args)
        {
            const bool pressed = ImGui::Button(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return pressed;
        }

        template <typename... Args>
        bool SmallButtonWithTooltip(const char* label, Args&&... args)
        {
            const bool pressed = ImGui::SmallButton(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return pressed;
        }

        template <typename... Args>
        bool InputTextWithHintWithTooltip(const char* label, const char* hint, Args&&... args)
        {
            const bool changed = ImGui::InputTextWithHint(label, hint, std::forward<Args>(args)...);
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

        template <typename... Args>
        bool SelectableWithTooltip(const char* label, Args&&... args)
        {
            const bool selected = ImGui::Selectable(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return selected;
        }
    }

    void MeshAssetPickerPanel::Draw(const MeshAssetPickerPanelContext& context, std::string& meshSource)
    {
        if (context.pickerService == nullptr)
        {
            return;
        }

        const std::string meshAssetLabel =
            meshSource.empty()
                ? std::string("Select Mesh Asset...##MeshAssetPickerButton")
                : (std::filesystem::path(meshSource).filename().string() + "##MeshAssetPickerButton");
        if (ButtonWithTooltip(meshAssetLabel.c_str(), ImVec2(-80.0f, 0.0f)))
        {
            context.pickerService->State().query.clear();
            if (context.rootPaths != nullptr)
            {
                context.pickerService->RefreshEntries(*context.rootPaths);
            }
            ImGui::OpenPopup("MeshAssetPickerPopup");
        }
        ShowItemTooltip(
            "Pick a project mesh asset. Supports cooked .lumamesh plus .obj, .fbx, .gltf, and .glb sources.");

        if (!meshSource.empty())
        {
            ImGui::SameLine();
            if (SmallButtonWithTooltip("Clear##MeshAssetPickerClear"))
            {
                meshSource.clear();
            }
        }

        if (ImGui::BeginPopup("MeshAssetPickerPopup"))
        {
            MeshAssetPickerState& state = context.pickerService->State();
            std::array<char, 256> searchBuffer {};
            std::snprintf(searchBuffer.data(), searchBuffer.size(), "%s", state.query.c_str());
            if (InputTextWithHintWithTooltip(
                    "##MeshAssetPickerSearch",
                    "Search mesh assets...",
                    searchBuffer.data(),
                    searchBuffer.size()))
            {
                state.query = searchBuffer.data();
            }

            ImGui::BeginChild("##MeshAssetPickerList", ImVec2(520.0f, 260.0f), true);
            const std::string loweredQuery = ToLowerString(state.query);
            bool anyMatch = false;
            for (const std::filesystem::path& entryPath : state.entries)
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
                if (!loweredQuery.empty() &&
                    searchText.find(loweredQuery) == std::string::npos)
                {
                    continue;
                }

                anyMatch = true;
                if (SelectableWithTooltip(displayText.c_str(), false))
                {
                    meshSource = displayPath.generic_string();
                    ImGui::CloseCurrentPopup();
                }
            }

            if (!anyMatch)
            {
                ImGui::TextDisabled("No mesh assets match the current filter.");
            }
            ImGui::EndChild();
            ImGui::EndPopup();
        }
    }
}
