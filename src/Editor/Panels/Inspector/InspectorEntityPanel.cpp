#include "Luma/Editor/Panels/Inspector/InspectorEntityPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/IDComponent.h"
#include "Luma/Scene/PrefabInstanceComponent.h"
#include "Luma/Scene/RelationshipComponent.h"
#include "Luma/Scene/TagComponent.h"
#include "Luma/Scene/TransformComponent.h"

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
        bool InputTextWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::InputText(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
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
        bool ComboWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::Combo(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Select ");
            return changed;
        }

        EntityID FindPrefabRootEntity(const Scene& scene, EntityID entity)
        {
            const auto& registry = scene.GetRegistry();
            while (entity != entt::null && registry.valid(entity))
            {
                if (const auto* prefabInstance = registry.try_get<PrefabInstanceComponent>(entity);
                    prefabInstance != nullptr && prefabInstance->isRoot)
                {
                    return entity;
                }

                const auto* relationship = registry.try_get<RelationshipComponent>(entity);
                if (relationship == nullptr)
                {
                    break;
                }
                entity = relationship->parent;
            }

            return entt::null;
        }

        std::string ToTitleCase(std::string value)
        {
            bool capitalizeNext = true;
            for (char& character : value)
            {
                if (character == '_' || character == '-')
                {
                    character = ' ';
                    capitalizeNext = true;
                    continue;
                }

                if (capitalizeNext && std::isalpha(static_cast<unsigned char>(character)) != 0)
                {
                    character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
                    capitalizeNext = false;
                }
                else
                {
                    capitalizeNext = character == ' ';
                }
            }

            return value;
        }

        std::string FriendlyComponentName(const std::string_view key)
        {
            if (key == "transform") return "Transform";
            if (key == "meshRenderer") return "Mesh Renderer";
            if (key == "material") return "Material";
            if (key == "camera") return "Camera";
            if (key == "luaScript") return "Lua Script";
            if (key == "directionalLight") return "Directional Light";
            if (key == "pointLight") return "Point Light";
            if (key == "spotLight") return "Spot Light";
            if (key == "skyLight") return "Sky Light";
            if (key == "postProcess") return "Post Process";
            if (key == "rigidBody") return "Rigid Body";
            if (key == "collider") return "Collider";
            if (key == "physicsEvents") return "Physics Events";
            if (key == "characterController") return "Character Controller";
            if (key == "vehicle") return "Vehicle";
            if (key == "vehicleInput") return "Vehicle Input";
            if (key == "wheelCollider") return "Wheel Collider";
            if (key == "destructible") return "Destructible";
            if (key == "tag") return "Tag";
            return ToTitleCase(std::string(key));
        }

        std::string FriendlyFieldPath(std::string path)
        {
            const auto replaceAll = [](std::string& value, const std::string_view from, const std::string_view to)
            {
                std::size_t position = 0;
                while ((position = value.find(from, position)) != std::string::npos)
                {
                    value.replace(position, from.size(), to);
                    position += to.size();
                }
            };

            replaceAll(path, "propertyOverrides.", "Properties.");
            replaceAll(path, "boxHalfExtents", "Box Half Extents");
            replaceAll(path, "linearVelocity", "Linear Velocity");
            replaceAll(path, "angularVelocity", "Angular Velocity");
            replaceAll(path, "meshSource", "Mesh Source");
            replaceAll(path, "scriptAsset", "Script");

            std::string formatted;
            formatted.reserve(path.size() + 8);
            for (std::size_t index = 0; index < path.size(); ++index)
            {
                const char character = path[index];
                const char previous = index > 0 ? path[index - 1] : '\0';
                if (character == '.')
                {
                    formatted += ".";
                    continue;
                }

                if (character == '[')
                {
                    formatted += character;
                    continue;
                }

                if (character == '_' || character == '-')
                {
                    formatted += ' ';
                    continue;
                }

                if (index > 0 &&
                    std::isupper(static_cast<unsigned char>(character)) != 0 &&
                    (std::islower(static_cast<unsigned char>(previous)) != 0 ||
                     std::isdigit(static_cast<unsigned char>(previous)) != 0))
                {
                    formatted += ' ';
                }

                formatted += character;
            }

            if (formatted == "position[0]") return "Position.X";
            if (formatted == "position[1]") return "Position.Y";
            if (formatted == "position[2]") return "Position.Z";
            if (formatted == "rotation[0]") return "Rotation.X";
            if (formatted == "rotation[1]") return "Rotation.Y";
            if (formatted == "rotation[2]") return "Rotation.Z";
            if (formatted == "scale[0]") return "Scale.X";
            if (formatted == "scale[1]") return "Scale.Y";
            if (formatted == "scale[2]") return "Scale.Z";

            return ToTitleCase(formatted);
        }

        enum class PrefabOverrideKind : std::uint8_t
        {
            Modified = 0,
            Added,
            Removed
        };

        struct GroupedPrefabOverride
        {
            std::string rawPath;
            std::string label;
            PrefabOverrideKind kind = PrefabOverrideKind::Modified;
        };

        struct GroupedPrefabComponentOverrides
        {
            std::string componentPath;
            std::string componentLabel;
            PrefabOverrideKind componentKind = PrefabOverrideKind::Modified;
            std::vector<GroupedPrefabOverride> overrides;
        };

        std::pair<PrefabOverrideKind, std::string> DecodePrefabOverridePath(std::string path)
        {
            if (!path.empty() && path.front() == '+')
            {
                path.erase(path.begin());
                return { PrefabOverrideKind::Added, std::move(path) };
            }
            if (!path.empty() && path.front() == '-')
            {
                path.erase(path.begin());
                return { PrefabOverrideKind::Removed, std::move(path) };
            }
            return { PrefabOverrideKind::Modified, std::move(path) };
        }

        std::vector<GroupedPrefabComponentOverrides> GroupPrefabOverridePaths(const std::vector<std::string>& overridePaths)
        {
            std::map<std::string, GroupedPrefabComponentOverrides> grouped;
            for (const std::string& encodedPath : overridePaths)
            {
                auto [overrideKind, overridePath] = DecodePrefabOverridePath(encodedPath);
                const std::size_t dotIndex = overridePath.find('.');
                const std::string componentKey =
                    dotIndex == std::string::npos ? overridePath : overridePath.substr(0, dotIndex);
                const std::string fieldPath =
                    dotIndex == std::string::npos ? std::string {} : overridePath.substr(dotIndex + 1);

                GroupedPrefabComponentOverrides& group = grouped[componentKey];
                group.componentPath = componentKey;
                group.componentLabel = FriendlyComponentName(componentKey);
                if (fieldPath.empty())
                {
                    group.componentKind = overrideKind;
                }

                group.overrides.push_back({
                    overridePath,
                    fieldPath.empty() ? "Value" : FriendlyFieldPath(fieldPath),
                    overrideKind
                });
            }

            std::vector<GroupedPrefabComponentOverrides> result;
            result.reserve(grouped.size());
            for (auto& [componentName, group] : grouped)
            {
                std::sort(group.overrides.begin(), group.overrides.end(), [](const GroupedPrefabOverride& lhs, const GroupedPrefabOverride& rhs)
                {
                    return lhs.label < rhs.label;
                });
                group.overrides.erase(
                    std::unique(
                        group.overrides.begin(),
                        group.overrides.end(),
                        [](const GroupedPrefabOverride& lhs, const GroupedPrefabOverride& rhs)
                        {
                            return lhs.rawPath == rhs.rawPath;
                        }),
                    group.overrides.end());
                result.push_back(std::move(group));
            }

            return result;
        }
    }

    void InspectorEntityPanel::Draw(const InspectorEntityPanelContext& context)
    {
        if (context.scene == nullptr || context.selectedEntity == entt::null)
        {
            return;
        }

        auto& registry = context.scene->GetRegistry();
        if (!registry.valid(context.selectedEntity) ||
            !registry.all_of<TagComponent, TransformComponent, RelationshipComponent, IDComponent>(context.selectedEntity))
        {
            return;
        }

        auto& tag = registry.get<TagComponent>(context.selectedEntity);
        auto& transform = registry.get<TransformComponent>(context.selectedEntity);
        auto& relationship = registry.get<RelationshipComponent>(context.selectedEntity);
        const auto& id = registry.get<IDComponent>(context.selectedEntity);

        std::array<char, 128> nameBuffer {};
        std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", tag.name.c_str());
        if (InputTextWithTooltip("Name", nameBuffer.data(), nameBuffer.size()))
        {
            tag.name = nameBuffer[0] != '\0' ? nameBuffer.data() : "Entity";
        }

        ImGui::TextDisabled("UUID: %llu", static_cast<unsigned long long>(id.id));

        std::vector<const char*> tagLabels;
        int selectedTagIndex = 0;
        if (context.availableTags != nullptr && !context.availableTags->empty())
        {
            tagLabels.reserve(context.availableTags->size() + 1);
            bool foundCurrentTag = false;
            for (std::size_t i = 0; i < context.availableTags->size(); ++i)
            {
                tagLabels.push_back((*context.availableTags)[i].c_str());
                if ((*context.availableTags)[i] == tag.tag)
                {
                    selectedTagIndex = static_cast<int>(i);
                    foundCurrentTag = true;
                }
            }

            if (!foundCurrentTag)
            {
                tagLabels.push_back(tag.tag.c_str());
                selectedTagIndex = static_cast<int>(tagLabels.size() - 1);
            }

            if (ComboWithTooltip("Tag", &selectedTagIndex, tagLabels.data(), static_cast<int>(tagLabels.size())))
            {
                selectedTagIndex = std::clamp(selectedTagIndex, 0, static_cast<int>(tagLabels.size()) - 1);
                tag.tag = tagLabels[static_cast<std::size_t>(selectedTagIndex)];
            }
        }
        else
        {
            std::array<char, 128> tagBuffer {};
            std::snprintf(tagBuffer.data(), tagBuffer.size(), "%s", tag.tag.c_str());
            if (InputTextWithTooltip("Tag", tagBuffer.data(), tagBuffer.size()))
            {
                tag.tag = tagBuffer[0] != '\0' ? tagBuffer.data() : "Untagged";
            }
        }

        ImGui::Separator();
        bool transformChanged = false;
        transformChanged |= DragFloat3WithTooltip("Position", transform.position.data(), 0.05f);
        transformChanged |= DragFloat3WithTooltip("Rotation", transform.rotation.data(), 0.1f);
        transformChanged |= DragFloat3WithTooltip("Scale", transform.scale.data(), 0.05f, 0.01f, 100.0f);
        if (transformChanged)
        {
            transform.dirty = true;
        }

        ImGui::Spacing();
        ImGui::TextDisabled(
            "World Position: %.2f, %.2f, %.2f",
            transform.worldPosition[0],
            transform.worldPosition[1],
            transform.worldPosition[2]);

        ImGui::TextDisabled("Physics Backend: %.*s", static_cast<int>(context.physicsBackendName.size()), context.physicsBackendName.data());
        ImGui::SameLine();
        if (context.physicsSimulationEnabled != nullptr)
        {
            CheckboxWithTooltip("Simulate Physics", context.physicsSimulationEnabled);
            ShowItemTooltip("Enable or pause runtime physics simulation for the current scene.");
        }

        ImGui::Separator();
        if (relationship.parent != entt::null && registry.valid(relationship.parent) &&
            registry.all_of<TagComponent>(relationship.parent))
        {
            const auto& parentTag = registry.get<TagComponent>(relationship.parent);
            ImGui::Text("Parent: %s", parentTag.name.c_str());
            ImGui::SameLine();
            if (ButtonWithTooltip("Unparent"))
            {
                context.scene->Unparent(context.selectedEntity);
                transform.dirty = true;
            }
        }
        else
        {
            ImGui::TextDisabled("Parent: None");
        }

        ImGui::Text("Children: %d", static_cast<int>(relationship.children.size()));

        const EntityID prefabRootEntity = FindPrefabRootEntity(*context.scene, context.selectedEntity);
        const auto* prefabInstance =
            prefabRootEntity != entt::null ? registry.try_get<PrefabInstanceComponent>(prefabRootEntity) : nullptr;

        ImGui::Separator();
        if (prefabInstance != nullptr)
        {
            std::filesystem::path prefabAssetPath(prefabInstance->prefabAsset);
            std::string prefabLabel = prefabAssetPath.filename().string();
            if (prefabLabel.empty())
            {
                prefabLabel = prefabInstance->prefabAsset;
            }

            if (ImGui::CollapsingHeader("Prefab Instance", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Text("Asset: %s", prefabLabel.c_str());
                if (context.getPrefabInstanceStatus)
                {
                    const std::string status = context.getPrefabInstanceStatus(context.selectedEntity);
                    if (!status.empty())
                    {
                        ImGui::Text("Status: %s", status.c_str());
                    }
                }

                if (prefabRootEntity != context.selectedEntity && registry.valid(prefabRootEntity) &&
                    registry.all_of<TagComponent>(prefabRootEntity))
                {
                    const auto& prefabRootTag = registry.get<TagComponent>(prefabRootEntity);
                    ImGui::TextDisabled("Instance Root: %s", prefabRootTag.name.c_str());
                }

                if (context.getPrefabOverridePaths)
                {
                    const std::vector<std::string> overridePaths = context.getPrefabOverridePaths(context.selectedEntity);
                    if (!overridePaths.empty())
                    {
                        ImGui::Spacing();
                        ImGui::TextUnformatted("Overrides");
                        const auto groupedOverrides = GroupPrefabOverridePaths(overridePaths);
                        for (const GroupedPrefabComponentOverrides& componentGroup : groupedOverrides)
                        {
                            const std::string componentHeader =
                                componentGroup.componentKind == PrefabOverrideKind::Added ? (componentGroup.componentLabel + " (Added)") :
                                componentGroup.componentKind == PrefabOverrideKind::Removed ? (componentGroup.componentLabel + " (Removed)") :
                                componentGroup.componentLabel;

                            ImGui::PushID(componentGroup.componentPath.c_str());
                            const bool opened = ImGui::TreeNodeEx(componentHeader.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Revert Component"))
                            {
                                if (context.revertPrefabComponent)
                                {
                                    context.revertPrefabComponent(context.selectedEntity, componentGroup.componentPath);
                                }
                            }

                            if (opened)
                            {
                                for (const GroupedPrefabOverride& overrideEntry : componentGroup.overrides)
                                {
                                    if (overrideEntry.label == "Value")
                                    {
                                        continue;
                                    }

                                    const std::string fieldLabel =
                                        overrideEntry.kind == PrefabOverrideKind::Added ? (overrideEntry.label + " (Added)") :
                                        overrideEntry.kind == PrefabOverrideKind::Removed ? (overrideEntry.label + " (Removed)") :
                                        overrideEntry.label;

                                    ImGui::Bullet();
                                    ImGui::SameLine();
                                    ImGui::TextUnformatted(fieldLabel.c_str());
                                    ImGui::SameLine();
                                    ImGui::PushID(overrideEntry.rawPath.c_str());
                                    if (ImGui::SmallButton("Revert"))
                                    {
                                        if (context.revertPrefabOverridePath)
                                        {
                                            context.revertPrefabOverridePath(context.selectedEntity, overrideEntry.rawPath);
                                        }
                                    }
                                    ImGui::PopID();
                                }
                                ImGui::TreePop();
                            }
                            ImGui::PopID();
                        }
                    }
                }

                if (ButtonWithTooltip("Select Prefab Asset"))
                {
                    if (context.selectPrefabAsset)
                    {
                        context.selectPrefabAsset(context.selectedEntity);
                    }
                }
                ImGui::SameLine();
                if (ButtonWithTooltip("Apply Prefab"))
                {
                    if (context.applyPrefabInstance)
                    {
                        context.applyPrefabInstance(context.selectedEntity);
                    }
                }
                ImGui::SameLine();
                if (ButtonWithTooltip("Revert Prefab"))
                {
                    if (context.revertPrefabInstance)
                    {
                        context.revertPrefabInstance(context.selectedEntity);
                    }
                }
            }
        }
        else if (ButtonWithTooltip("Create Prefab"))
        {
            if (context.createPrefabFromEntity)
            {
                context.createPrefabFromEntity(context.selectedEntity);
            }
        }
    }
}
