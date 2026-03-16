#include "Luma/Editor/Panels/Inspector/InspectorScriptPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>

#include "Luma/Asset/Core/AssetMetaIO.h"
#include "Luma/Core/App/Project.h"
#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/IDComponent.h"
#include "Luma/Scene/LuaScriptComponent.h"
#include "Luma/Scene/TagComponent.h"
#include "Luma/Scripting/ScriptEngine.h"
#include "Luma/Scripting/LuaScriptRuntime.h"

namespace Luma::Editor
{
    namespace
    {
        void ShowItemTooltip(const std::string_view tooltip)
        {
            UI::Tooltip::Show(tooltip);
        }

        void ShowItemTooltipFromLabel(const char* label, const char* prefix = nullptr)
        {
            UI::Tooltip::ShowForItemLabel(label, prefix == nullptr ? std::string_view {} : std::string_view(prefix));
        }

        template <typename... Args>
        bool CheckboxWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::Checkbox(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Toggle ");
            return changed;
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
        bool InputTextWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::InputText(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }

        template <typename... Args>
        bool DragFloatWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragFloat(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool DragIntWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragInt(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool DragFloat2WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragFloat2(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool DragFloat3WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragFloat3(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool DragFloat4WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragFloat4(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool SliderFloatWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::SliderFloat(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool SliderIntWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::SliderInt(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool ColorEdit3WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::ColorEdit3(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }

        template <typename... Args>
        bool ColorEdit4WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::ColorEdit4(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }

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

        bool IsScriptSelection(const std::filesystem::path& path)
        {
            const std::string extension = ToLowerString(path.extension().string());
            return extension == ".lua" || extension == ".lumascript";
        }

        std::string NormalizeReferenceString(std::string value)
        {
            std::replace(value.begin(), value.end(), '\\', '/');
            return value;
        }

        bool PathStartsWith(const std::filesystem::path& path, const std::filesystem::path& root)
        {
            const std::string normalizedPath = path.lexically_normal().generic_string();
            const std::string normalizedRoot = root.lexically_normal().generic_string();
#if defined(_WIN32)
            const std::string loweredPath = ToLowerString(normalizedPath);
            const std::string loweredRoot = ToLowerString(normalizedRoot);
            if (loweredPath.size() < loweredRoot.size() || loweredPath.compare(0, loweredRoot.size(), loweredRoot) != 0)
            {
                return false;
            }

            return loweredPath.size() == loweredRoot.size() || loweredPath[loweredRoot.size()] == '/';
#else
            if (normalizedPath.size() < normalizedRoot.size() || normalizedPath.compare(0, normalizedRoot.size(), normalizedRoot) != 0)
            {
                return false;
            }

            return normalizedPath.size() == normalizedRoot.size() || normalizedPath[normalizedRoot.size()] == '/';
#endif
        }

        std::string MakeDisplayPath(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return {};
            }

            if (Project::IsLoaded())
            {
                const std::filesystem::path& projectRoot = Project::GetProjectRoot();
                if (!projectRoot.empty() && PathStartsWith(path, projectRoot))
                {
                    std::error_code relativeEc;
                    const std::filesystem::path relative = std::filesystem::relative(path, projectRoot, relativeEc);
                    if (!relativeEc)
                    {
                        return relative.generic_string();
                    }
                }
            }

            return path.lexically_normal().generic_string();
        }

        void DrawSectionLabel(const char* label, const char* tooltip)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", label);
            if (tooltip != nullptr)
            {
                ShowItemTooltip(tooltip);
            }
        }

        void DrawStatusText(const char* prefix, const char* value, const ImVec4& color, const char* tooltip = nullptr)
        {
            ImGui::TextDisabled("%s", prefix);
            ImGui::SameLine();
            ImGui::TextColored(color, "%s", value);
            if (tooltip != nullptr)
            {
                ShowItemTooltip(tooltip);
            }
        }

        void DrawReadOnlyPathField(const char* label, const std::string& value, const char* emptyText, const char* tooltip)
        {
            std::array<char, 512> buffer {};
            const std::string displayValue = value.empty() ? std::string(emptyText) : value;
            std::snprintf(buffer.data(), buffer.size(), "%s", displayValue.c_str());
            ImGui::InputText(label, buffer.data(), buffer.size(), ImGuiInputTextFlags_ReadOnly);
            if (tooltip != nullptr)
            {
                ShowItemTooltip(tooltip);
            }
        }

        bool ResolveScriptReference(
            const std::string& scriptReference,
            std::filesystem::path& outAssetPath,
            std::filesystem::path& outSourcePath,
            std::string& outError)
        {
            outAssetPath.clear();
            outSourcePath.clear();
            outError.clear();

            if (scriptReference.empty())
            {
                outError = "No script is assigned.";
                return false;
            }

            std::filesystem::path referencePath = std::filesystem::path(scriptReference).lexically_normal();
            if (!referencePath.is_absolute())
            {
                referencePath = Project::IsLoaded()
                    ? (Project::GetProjectRoot() / referencePath).lexically_normal()
                    : (std::filesystem::current_path() / referencePath).lexically_normal();
            }

            outAssetPath = referencePath;

            const std::string extension = ToLowerString(referencePath.extension().string());
            if (extension == ".lua")
            {
                if (!std::filesystem::exists(referencePath))
                {
                    outError = "Script file is missing on disk.";
                    return false;
                }

                outSourcePath = referencePath;
                return true;
            }

            if (extension == ".lumascript")
            {
                Assets::AssetMeta meta {};
                std::string metaError;
                if (!Assets::ReadMetaFile(referencePath.string() + ".meta", meta, metaError))
                {
                    outError = "Failed to read .lumascript metadata: " + metaError;
                    return false;
                }

                if (meta.sourcePaths.empty())
                {
                    outError = "Lua script asset metadata has no source file.";
                    return false;
                }

                std::filesystem::path sourcePath = meta.sourcePaths.front();
                if (!sourcePath.is_absolute())
                {
                    sourcePath = Project::IsLoaded()
                        ? (Project::GetProjectRoot() / sourcePath).lexically_normal()
                        : (std::filesystem::current_path() / sourcePath).lexically_normal();
                }

                if (!std::filesystem::exists(sourcePath))
                {
                    outError = "Resolved Lua source file is missing on disk.";
                    return false;
                }

                outSourcePath = sourcePath;
                return true;
            }

            outError = "Only .lua and .lumascript references are supported.";
            return false;
        }

        std::string BuildScriptSlotLabel(const LuaScriptComponent& component)
        {
            if (component.scriptAsset.empty())
            {
                return "Drop Lua Script Here";
            }

            const std::filesystem::path scriptPath(component.scriptAsset);
            const std::string fileName = scriptPath.filename().string();
            return fileName.empty() ? component.scriptAsset : fileName;
        }

        std::string BuildScriptComponentHeaderLabel(const LuaScriptComponent& component)
        {
            if (component.scriptAsset.empty())
            {
                return "Lua Script";
            }

            const std::filesystem::path scriptPath(component.scriptAsset);
            const std::string stem = scriptPath.stem().string();
            return (stem.empty() ? BuildScriptSlotLabel(component) : stem) + " (Script)";
        }

        std::string BuildScriptObjectFieldLabel(const LuaScriptComponent& component)
        {
            if (component.scriptAsset.empty())
            {
                return "None (Lua Script)";
            }

            const std::filesystem::path scriptPath(component.scriptAsset);
            const std::string stem = scriptPath.stem().string();
            return stem.empty() ? BuildScriptSlotLabel(component) : stem;
        }

        bool PropertyLooksLikeColor(const ScriptPropertyInfo& property)
        {
            if (property.editorHint == ScriptPropertyEditorHint::Color3 ||
                property.editorHint == ScriptPropertyEditorHint::Color4)
            {
                return true;
            }

            const std::string lowered = ToLowerString(property.name);
            return lowered.find("color") != std::string::npos || lowered.find("tint") != std::string::npos;
        }

        ScriptValue ResolvePropertyValue(
            const LuaScriptComponent& component,
            const ScriptPropertyInfo& property,
            bool& outHasOverride,
            bool& outTypeMismatch)
        {
            outHasOverride = false;
            outTypeMismatch = false;

            if (const auto overrideIt = component.propertyOverrides.find(property.name);
                overrideIt != component.propertyOverrides.end())
            {
                if (ScriptValueTypeMatches(overrideIt->second, property.defaultValue))
                {
                    outHasOverride = true;
                    return overrideIt->second;
                }

                outTypeMismatch = true;
            }

            return property.defaultValue;
        }

        struct ScriptPropertyTreeNode
        {
            std::string label;
            const ScriptPropertyInfo* property = nullptr;
            std::vector<ScriptPropertyTreeNode> children;
        };

        ScriptPropertyTreeNode& FindOrAddPropertyChild(
            std::vector<ScriptPropertyTreeNode>& nodes,
            const std::string_view label)
        {
            const auto existingIt = std::find_if(
                nodes.begin(),
                nodes.end(),
                [&](const ScriptPropertyTreeNode& node)
                {
                    return node.label == label;
                });
            if (existingIt != nodes.end())
            {
                return *existingIt;
            }

            nodes.push_back({ std::string(label) });
            return nodes.back();
        }

        void InsertPropertyNode(
            std::vector<ScriptPropertyTreeNode>& nodes,
            const ScriptPropertyInfo& property)
        {
            std::size_t segmentStart = 0;
            std::vector<ScriptPropertyTreeNode>* currentNodes = &nodes;
            ScriptPropertyTreeNode* currentNode = nullptr;

            while (segmentStart < property.name.size())
            {
                const std::size_t dotIndex = property.name.find('.', segmentStart);
                const bool lastSegment = dotIndex == std::string::npos;
                const std::string_view segment = std::string_view(property.name).substr(
                    segmentStart,
                    lastSegment ? std::string_view::npos : dotIndex - segmentStart);
                if (segment.empty())
                {
                    break;
                }

                currentNode = &FindOrAddPropertyChild(*currentNodes, segment);
                if (lastSegment)
                {
                    currentNode->property = &property;
                    return;
                }

                currentNodes = &currentNode->children;
                segmentStart = dotIndex + 1;
            }
        }

        std::vector<ScriptPropertyTreeNode> BuildPropertyTree(const LuaScriptAssetMetadata& metadata)
        {
            std::vector<ScriptPropertyTreeNode> nodes;
            nodes.reserve(metadata.properties.size());
            for (const ScriptPropertyInfo& property : metadata.properties)
            {
                InsertPropertyNode(nodes, property);
            }

            return nodes;
        }

        bool DrawScriptValueControl(
            const char* controlLabel,
            const ScriptPropertyInfo& property,
            ScriptValue& value,
            const InspectorScriptPanelContext& context)
        {
            switch (value.type)
            {
            case ScriptValueType::Bool:
                return ImGui::Checkbox(controlLabel, &value.boolValue);

            case ScriptValueType::Int:
                if (property.hasMinValue && property.hasMaxValue)
                {
                    return ImGui::SliderInt(
                        controlLabel,
                        &value.intValue,
                        static_cast<int>(property.minValue),
                        static_cast<int>(property.maxValue));
                }
                return ImGui::DragInt(controlLabel, &value.intValue, 1.0f, -1000000, 1000000);

            case ScriptValueType::Float:
                if (property.hasMinValue && property.hasMaxValue)
                {
                    return ImGui::SliderFloat(
                        controlLabel,
                        &value.floatValue,
                        property.minValue,
                        property.maxValue);
                }
                return ImGui::DragFloat(controlLabel, &value.floatValue, 0.1f, -1000000.0f, 1000000.0f);

            case ScriptValueType::String:
            {
                if (property.editorHint == ScriptPropertyEditorHint::Enum && !property.options.empty())
                {
                    int currentIndex = 0;
                    for (std::size_t optionIndex = 0; optionIndex < property.options.size(); ++optionIndex)
                    {
                        if (property.options[optionIndex] == value.stringValue)
                        {
                            currentIndex = static_cast<int>(optionIndex);
                            break;
                        }
                    }

                    bool changed = false;
                    if (ImGui::BeginCombo(controlLabel, property.options[currentIndex].c_str()))
                    {
                        for (std::size_t optionIndex = 0; optionIndex < property.options.size(); ++optionIndex)
                        {
                            const bool selected = static_cast<int>(optionIndex) == currentIndex;
                            if (ImGui::Selectable(property.options[optionIndex].c_str(), selected))
                            {
                                value.stringValue = property.options[optionIndex];
                                changed = true;
                            }
                            if (selected)
                            {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }
                    return changed;
                }

                if (property.editorHint == ScriptPropertyEditorHint::AssetReference)
                {
                    bool changed = false;
                    std::array<char, 256> buffer {};
                    std::snprintf(buffer.data(), buffer.size(), "%s", value.stringValue.c_str());

                    const bool showUseSelectedButton =
                        context.selectedContentEntry != nullptr && !context.selectedContentEntry->empty();
                    const bool showClearButton = !value.stringValue.empty();
                    const float useButtonWidth = showUseSelectedButton ? 88.0f : 0.0f;
                    const float clearButtonWidth = showClearButton ? 24.0f : 0.0f;
                    const float spacingWidth =
                        (showUseSelectedButton && showClearButton) ? ImGui::GetStyle().ItemSpacing.x : 0.0f;
                    const float totalButtonWidth = useButtonWidth + clearButtonWidth + spacingWidth;

                    ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - totalButtonWidth));
                    if (ImGui::InputText(controlLabel, buffer.data(), buffer.size()))
                    {
                        value.stringValue = buffer.data();
                        changed = true;
                    }

                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ASSET_PATH"))
                        {
                            const char* payloadPath = static_cast<const char*>(payload->Data);
                            value.stringValue = NormalizeReferenceString(std::filesystem::path(payloadPath).generic_string());
                            changed = true;
                        }
                        ImGui::EndDragDropTarget();
                    }

                    if (showUseSelectedButton)
                    {
                        ImGui::SameLine();
                        if (ButtonWithTooltip("Use Selected"))
                        {
                            value.stringValue = NormalizeReferenceString(context.selectedContentEntry->generic_string());
                            changed = true;
                        }
                    }

                    if (showClearButton)
                    {
                        ImGui::SameLine();
                        if (SmallButtonWithTooltip("X"))
                        {
                            value.stringValue.clear();
                            changed = true;
                        }
                    }

                    return changed;
                }

                std::array<char, 256> buffer {};
                std::snprintf(buffer.data(), buffer.size(), "%s", value.stringValue.c_str());
                if (ImGui::InputText(controlLabel, buffer.data(), buffer.size()))
                {
                    value.stringValue = buffer.data();
                    return true;
                }
                return false;
            }

            case ScriptValueType::Entity:
            {
                if (context.scene == nullptr)
                {
                    ImGui::TextDisabled("No Scene");
                    return false;
                }

                auto& registry = context.scene->GetRegistry();
                std::string preview = "None";
                if (value.entityValue != 0)
                {
                    const EntityID entity = context.scene->FindByUUID(value.entityValue);
                    if (entity != entt::null && registry.valid(entity) && registry.all_of<TagComponent>(entity))
                    {
                        preview = registry.get<TagComponent>(entity).name;
                    }
                }

                bool changed = false;
                if (ImGui::BeginCombo(controlLabel, preview.c_str()))
                {
                    const bool selectedNone = value.entityValue == 0;
                    if (ImGui::Selectable("None", selectedNone))
                    {
                        value.entityValue = 0;
                        changed = true;
                    }
                    if (selectedNone)
                    {
                        ImGui::SetItemDefaultFocus();
                    }

                    const auto view = registry.view<IDComponent, TagComponent>();
                    for (const EntityID entity : view)
                    {
                        const auto& id = view.get<IDComponent>(entity);
                        const auto& tag = view.get<TagComponent>(entity);
                        const bool selected = id.id == value.entityValue;
                        if (ImGui::Selectable(tag.name.c_str(), selected))
                        {
                            value.entityValue = id.id;
                            changed = true;
                        }
                        if (selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }

                    ImGui::EndCombo();
                }
                return changed;
            }

            case ScriptValueType::Vec2:
                return ImGui::DragFloat2(controlLabel, value.vec2Value.data(), 0.1f);

            case ScriptValueType::Vec3:
                if (PropertyLooksLikeColor(property))
                {
                    return ImGui::ColorEdit3(controlLabel, value.vec3Value.data());
                }
                return ImGui::DragFloat3(controlLabel, value.vec3Value.data(), 0.1f);

            case ScriptValueType::Vec4:
                if (PropertyLooksLikeColor(property))
                {
                    return ImGui::ColorEdit4(controlLabel, value.vec4Value.data());
                }
                return ImGui::DragFloat4(controlLabel, value.vec4Value.data(), 0.1f);

            case ScriptValueType::None:
            default:
                ImGui::TextDisabled("Unsupported");
                return false;
            }
        }
    }

    void InspectorScriptPanel::Draw(const InspectorScriptPanelContext& context)
    {
        if (context.scene == nullptr || context.selectedEntity == entt::null)
        {
            return;
        }

        auto& registry = context.scene->GetRegistry();
        if (!registry.valid(context.selectedEntity) || !registry.all_of<LuaScriptComponent>(context.selectedEntity))
        {
            return;
        }

        auto& component = registry.get<LuaScriptComponent>(context.selectedEntity);

        ImGui::Separator();
        const std::string componentHeader = BuildScriptComponentHeaderLabel(component);
        if (!ImGui::CollapsingHeader(componentHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        ShowItemTooltip("Assign a Lua behaviour script and edit serialized Script Properties for this entity.");
        ImGui::PushID("LuaScriptComponent");

        std::filesystem::path resolvedAssetPath;
        std::filesystem::path resolvedSourcePath;
        std::string resolveError;
        const bool scriptResolved = ResolveScriptReference(component.scriptAsset, resolvedAssetPath, resolvedSourcePath, resolveError);

        LuaScriptRuntime* runtime = context.luaScriptRuntime;
        const bool runtimeRunning = context.playModeActive && runtime != nullptr && runtime->IsRunning();
        std::string metadataError;
        const LuaScriptAssetMetadata* metadata =
            component.scriptAsset.empty() ? nullptr : ScriptEngine::GetScriptMetadata(component.scriptAsset, &metadataError);

        bool selectedEntryIsScript = false;
        if (context.selectedContentEntry != nullptr && !context.selectedContentEntry->empty())
        {
            selectedEntryIsScript = IsScriptSelection(*context.selectedContentEntry);
        }

        auto assignScriptReference = [&](const std::string& scriptReference, const std::string& statusMessage)
        {
            if (component.scriptAsset != scriptReference)
            {
                component.scriptAsset = scriptReference;
                component.propertyOverrides.clear();
            }
            else
            {
                component.scriptAsset = scriptReference;
            }

            if (context.setContentStatus)
            {
                context.setContentStatus(statusMessage);
            }
        };

        if (ImGui::BeginTable("##LuaScriptSummaryTable", 2, ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 92.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Enabled");
            ImGui::TableNextColumn();
            CheckboxWithTooltip("##LuaScriptEnabled", &component.enabled);

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Script");
            ShowItemTooltip("Assigned Lua behaviour asset for this entity.");
            ImGui::TableNextColumn();

            const std::string objectFieldLabel = BuildScriptObjectFieldLabel(component);
            std::array<char, 256> scriptBuffer {};
            std::snprintf(scriptBuffer.data(), scriptBuffer.size(), "%s", objectFieldLabel.c_str());

            const bool showUseSelectedButton = selectedEntryIsScript;
            const bool showClearButton = !component.scriptAsset.empty();
            const float useButtonWidth = showUseSelectedButton ? 88.0f : 0.0f;
            const float clearButtonWidth = showClearButton ? 24.0f : 0.0f;
            const float spacingWidth = (showUseSelectedButton && showClearButton) ? ImGui::GetStyle().ItemSpacing.x : 0.0f;
            const float totalButtonWidth = useButtonWidth + clearButtonWidth + spacingWidth;

            ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - totalButtonWidth));
            ImGui::InputText("##LuaScriptAssetDisplay", scriptBuffer.data(), scriptBuffer.size(), ImGuiInputTextFlags_ReadOnly);
            ShowItemTooltip(
                component.scriptAsset.empty()
                    ? "Drop a .lua or .lumascript asset here to assign a behaviour."
                    : "Current Lua script assigned to this component.");

            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ASSET_PATH"))
                {
                    const char* payloadPath = static_cast<const char*>(payload->Data);
                    const std::filesystem::path droppedPath = std::filesystem::path(payloadPath);
                    if (IsScriptSelection(droppedPath))
                    {
                        const std::string normalizedReference = NormalizeReferenceString(droppedPath.generic_string());
                        assignScriptReference(
                            normalizedReference,
                            "Assigned Lua script: " + std::filesystem::path(normalizedReference).filename().string());
                    }
                    else if (context.setContentStatus)
                    {
                        context.setContentStatus("Only .lua and .lumascript assets can be assigned to a Lua Script component.");
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (showUseSelectedButton)
            {
                ImGui::SameLine();
                if (ButtonWithTooltip("Use Selected"))
                {
                    const std::string normalizedReference =
                        NormalizeReferenceString(context.selectedContentEntry->generic_string());
                    assignScriptReference(
                        normalizedReference,
                        "Assigned Lua script: " + std::filesystem::path(normalizedReference).filename().string());
                }
            }

            if (showClearButton)
            {
                ImGui::SameLine();
                if (SmallButtonWithTooltip("X"))
                {
                    component.scriptAsset.clear();
                    component.propertyOverrides.clear();
                    if (context.setContentStatus)
                    {
                        context.setContentStatus("Cleared Lua script assignment.");
                    }
                }
            }

            ImGui::EndTable();
        }

        if (!scriptResolved && !component.scriptAsset.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(232, 108, 108, 255));
            ImGui::TextWrapped("%s", resolveError.c_str());
            ImGui::PopStyleColor();
        }

        int staleOverrideCount = 0;
        int mismatchedOverrideCount = 0;
        if (metadata != nullptr)
        {
            for (const auto& [overrideName, overrideValue] : component.propertyOverrides)
            {
                const auto propertyIt = std::find_if(
                    metadata->properties.begin(),
                    metadata->properties.end(),
                    [&](const ScriptPropertyInfo& property)
                    {
                        return property.name == overrideName;
                    });

                if (propertyIt == metadata->properties.end())
                {
                    ++staleOverrideCount;
                    continue;
                }

                if (!ScriptValueTypeMatches(overrideValue, propertyIt->defaultValue))
                {
                    ++mismatchedOverrideCount;
                }
            }
        }

        const bool openProperties = ImGui::TreeNodeEx("Script Properties", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
        if (!component.scriptAsset.empty())
        {
            ImGui::SameLine();
            if (SmallButtonWithTooltip("Reload"))
            {
                ScriptEngine::InvalidateScriptMetadata(component.scriptAsset);
                metadataError.clear();
                metadata = ScriptEngine::GetScriptMetadata(component.scriptAsset, &metadataError);
                if (context.setContentStatus)
                {
                    context.setContentStatus("Reloaded Lua script property metadata.");
                }
            }
        }

        if (openProperties)
        {
            if (component.scriptAsset.empty())
            {
                ImGui::TextDisabled("Assign a script to expose editable properties.");
            }
            else if (metadata == nullptr)
            {
                ImGui::TextDisabled("No property metadata could be loaded for this script.");
                if (!metadataError.empty())
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(232, 108, 108, 255));
                    ImGui::TextWrapped("%s", metadataError.c_str());
                    ImGui::PopStyleColor();
                }
            }
            else if (metadata->properties.empty())
            {
                ImGui::TextDisabled("This script does not expose any Script Properties yet.");
            }
            else
            {
                if (staleOverrideCount > 0 || mismatchedOverrideCount > 0)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(224, 186, 88, 255));
                    ImGui::TextWrapped(
                        "Saved overrides need attention. Missing: %d | Type mismatch: %d",
                        staleOverrideCount,
                        mismatchedOverrideCount);
                    ImGui::PopStyleColor();

                    if (SmallButtonWithTooltip("Prune Invalid"))
                    {
                        for (auto it = component.propertyOverrides.begin(); it != component.propertyOverrides.end();)
                        {
                            const auto propertyIt = std::find_if(
                                metadata->properties.begin(),
                                metadata->properties.end(),
                                [&](const ScriptPropertyInfo& property)
                                {
                                    return property.name == it->first;
                                });

                            if (propertyIt == metadata->properties.end() ||
                                !ScriptValueTypeMatches(it->second, propertyIt->defaultValue))
                            {
                                it = component.propertyOverrides.erase(it);
                            }
                            else
                            {
                                ++it;
                            }
                        }
                    }

                    ImGui::SameLine();
                    if (SmallButtonWithTooltip("Reset All"))
                    {
                        component.propertyOverrides.clear();
                    }
                }
                else if (!component.propertyOverrides.empty())
                {
                    if (SmallButtonWithTooltip("Reset All"))
                    {
                        component.propertyOverrides.clear();
                    }
                }

                const std::vector<ScriptPropertyTreeNode> propertyTree = BuildPropertyTree(*metadata);
                const auto drawPropertyNodes =
                    [&](auto&& self, const std::vector<ScriptPropertyTreeNode>& nodes, const std::string& scopeId) -> void
                {
                    bool hasLeafNodes = false;
                    for (const ScriptPropertyTreeNode& node : nodes)
                    {
                        hasLeafNodes |= node.property != nullptr;
                    }

                    if (hasLeafNodes &&
                        ImGui::BeginTable(scopeId.c_str(), 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV))
                    {
                        ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, 96.0f);
                        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

                        for (const ScriptPropertyTreeNode& node : nodes)
                        {
                            if (node.property == nullptr)
                            {
                                continue;
                            }

                            bool usingOverride = false;
                            bool typeMismatch = false;
                            ScriptValue currentValue = ResolvePropertyValue(component, *node.property, usingOverride, typeMismatch);

                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::AlignTextToFramePadding();
                            ImGui::TextUnformatted(node.label.c_str());
                            if (!node.property->tooltip.empty())
                            {
                                ShowItemTooltip(node.property->tooltip);
                            }

                            ImGui::TableNextColumn();
                            ImGui::PushID(node.property->name.c_str());
                            const bool canReset = usingOverride || typeMismatch;
                            const float resetWidth = canReset ? 40.0f + ImGui::GetStyle().ItemSpacing.x : 0.0f;
                            ImGui::SetNextItemWidth(std::max(92.0f, ImGui::GetContentRegionAvail().x - resetWidth));
                            const bool changed = DrawScriptValueControl("##Value", *node.property, currentValue, context);

                            bool resetPressed = false;
                            if (canReset)
                            {
                                ImGui::SameLine();
                                if (SmallButtonWithTooltip("Reset"))
                                {
                                    resetPressed = true;
                                }
                            }

                            if (changed)
                            {
                                if (ScriptValuesEqual(currentValue, node.property->defaultValue))
                                {
                                    component.propertyOverrides.erase(node.property->name);
                                }
                                else
                                {
                                    component.propertyOverrides[node.property->name] = currentValue;
                                }

                                if (runtimeRunning)
                                {
                                    const auto overrideIt = component.propertyOverrides.find(node.property->name);
                                    runtime->SetProperty(
                                        context.selectedEntity,
                                        node.property->name,
                                        overrideIt != component.propertyOverrides.end() ? overrideIt->second : node.property->defaultValue);
                                }
                            }

                            if (resetPressed)
                            {
                                component.propertyOverrides.erase(node.property->name);
                                if (runtimeRunning)
                                {
                                    runtime->SetProperty(context.selectedEntity, node.property->name, node.property->defaultValue);
                                }
                            }

                            ImGui::PopID();
                        }

                        ImGui::EndTable();
                    }

                    for (const ScriptPropertyTreeNode& node : nodes)
                    {
                        if (node.children.empty())
                        {
                            continue;
                        }

                        if (ImGui::TreeNodeEx(node.label.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
                        {
                            self(self, node.children, scopeId + "/" + node.label);
                            ImGui::TreePop();
                        }
                    }
                };

                drawPropertyNodes(drawPropertyNodes, propertyTree, "##LuaScriptPropertiesRoot");
            }

            ImGui::TreePop();
        }

        ImGui::Spacing();
        if (ButtonWithTooltip("Remove Lua Script Component"))
        {
            registry.remove<LuaScriptComponent>(context.selectedEntity);
        }

        ImGui::PopID();
    }
}
