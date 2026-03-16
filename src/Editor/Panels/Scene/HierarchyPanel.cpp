#include "Luma/Editor/Panels/Scene/HierarchyPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/CameraComponent.h"
#include "Luma/Scene/DirectionalLightComponent.h"
#include "Luma/Scene/IDComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/PointLightComponent.h"
#include "Luma/Scene/RelationshipComponent.h"
#include "Luma/Scene/RigidBodyComponent.h"
#include "Luma/Scene/SkyLightComponent.h"
#include "Luma/Scene/TagComponent.h"

namespace Luma::Editor
{
    namespace
    {
        struct HierarchyIconStyle
        {
            const char* label = "[E]";
            ImVec4 color = ImVec4(0.62f, 0.64f, 0.70f, 1.0f);
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
        bool MenuItemWithTooltip(const char* label, Args&&... args)
        {
            const bool activated = ImGui::MenuItem(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return activated;
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

        std::vector<EntityID> BuildVisibleEntities(
            const HierarchyPanelContext& context,
            const std::vector<EntityID>& sourceEntities)
        {
            std::vector<EntityID> entities;
            entities.reserve(sourceEntities.size());
            if (context.scene == nullptr)
            {
                return sourceEntities;
            }

            const auto& registry = context.scene->GetRegistry();
            for (const EntityID entity : sourceEntities)
            {
                if (!registry.valid(entity) || !registry.all_of<TagComponent>(entity))
                {
                    continue;
                }
                if (context.isEntityHidden && context.isEntityHidden(entity))
                {
                    continue;
                }
                entities.push_back(entity);
            }
            return entities;
        }

        HierarchyIconStyle ResolveHierarchyIconStyle(const entt::registry& registry, const EntityID entity)
        {
            if (registry.all_of<CameraComponent>(entity))
            {
                return { "[C]", ImVec4(0.42f, 0.76f, 0.96f, 1.0f) };
            }
            if (registry.all_of<DirectionalLightComponent>(entity))
            {
                return { "[D]", ImVec4(0.95f, 0.86f, 0.46f, 1.0f) };
            }
            if (registry.all_of<PointLightComponent>(entity))
            {
                return { "[L]", ImVec4(1.0f, 0.90f, 0.68f, 1.0f) };
            }
            if (registry.all_of<SkyLightComponent>(entity))
            {
                return { "[S]", ImVec4(0.56f, 0.70f, 0.98f, 1.0f) };
            }
            if (registry.all_of<RigidBodyComponent>(entity))
            {
                return { "[R]", ImVec4(0.86f, 0.64f, 0.42f, 1.0f) };
            }
            if (registry.all_of<TagComponent>(entity))
            {
                const std::string lowerTag = ToLowerString(registry.get<TagComponent>(entity).tag);
                if (lowerTag.find("player") != std::string::npos)
                {
                    return { "[P]", ImVec4(0.60f, 0.86f, 0.60f, 1.0f) };
                }
            }
            if (registry.all_of<MeshRendererComponent>(entity))
            {
                const auto& mesh = registry.get<MeshRendererComponent>(entity);
                switch (mesh.primitive)
                {
                case PrimitiveType::Cube:
                    return { "[B]", ImVec4(0.64f, 0.84f, 0.58f, 1.0f) };
                case PrimitiveType::Plane:
                    return { "[F]", ImVec4(0.58f, 0.78f, 0.84f, 1.0f) };
                case PrimitiveType::Sphere:
                    return { "[O]", ImVec4(0.84f, 0.66f, 0.58f, 1.0f) };
                case PrimitiveType::Cylinder:
                    return { "[Y]", ImVec4(0.66f, 0.86f, 0.92f, 1.0f) };
                case PrimitiveType::Capsule:
                    return { "[U]", ImVec4(0.72f, 0.90f, 0.76f, 1.0f) };
                case PrimitiveType::Cone:
                    return { "[N]", ImVec4(0.90f, 0.80f, 0.56f, 1.0f) };
                case PrimitiveType::Torus:
                    return { "[T]", ImVec4(0.84f, 0.72f, 0.96f, 1.0f) };
                default:
                    return { "[M]", ImVec4(0.70f, 0.70f, 0.70f, 1.0f) };
                }
            }
            return {};
        }

        void* ResolveHierarchyEntityIconTexture(
            const HierarchyPanelContext& context,
            const entt::registry& registry,
            const EntityID entity)
        {
            if (registry.all_of<CameraComponent>(entity))
            {
                return context.cameraIconTexture;
            }
            if (registry.all_of<PointLightComponent>(entity))
            {
                return context.pointLightIconTexture;
            }

            const MeshRendererComponent* meshRenderer = registry.try_get<MeshRendererComponent>(entity);
            if (meshRenderer == nullptr || !meshRenderer->usePrimitive)
            {
                return nullptr;
            }

            switch (meshRenderer->primitive)
            {
            case PrimitiveType::Cube:
                return context.cubeIconTexture;
            case PrimitiveType::Plane:
                return context.planeIconTexture;
            case PrimitiveType::Sphere:
                return context.sphereIconTexture;
            case PrimitiveType::Cylinder:
                return context.cylinderIconTexture;
            default:
                return nullptr;
            }
        }
    }

    void HierarchyPanel::Draw(bool* open, const HierarchyPanelContext& context)
    {
        if (context.scene == nullptr)
        {
            return;
        }

        if (!ImGui::Begin("Hierarchy", open))
        {
            ImGui::End();
            return;
        }

        bool openCreatePopup = false;
        if (context.panelIconTexture != nullptr)
        {
            ImGui::Image(
                reinterpret_cast<ImTextureID>(context.panelIconTexture),
                ImVec2(16.0f, 16.0f),
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f));
            ShowItemTooltip("Scene hierarchy: entities and parent/child relationships.");
            ImGui::SameLine(0.0f, 6.0f);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.06f, 0.07f, 0.09f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.15f, 0.19f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.20f, 0.26f, 1.0f));
        if (context.createIconTexture != nullptr)
        {
            openCreatePopup = ImGui::ImageButton(
                "##HierarchyCreateEntity",
                reinterpret_cast<ImTextureID>(context.createIconTexture),
                ImVec2(12.0f, 12.0f),
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f));
        }
        else
        {
            openCreatePopup = ButtonWithTooltip("+");
        }
        ShowItemTooltip("Create a new entity.");
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);

        if (openCreatePopup)
        {
            ImGui::OpenPopup("EntityCreationPopup");
        }

        ImGui::SameLine(0.0f, 6.0f);
        std::array<char, 256> hierarchySearchBuffer {};
        std::snprintf(hierarchySearchBuffer.data(), hierarchySearchBuffer.size(), "%s", m_SearchQuery.c_str());
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.04f, 0.04f, 0.04f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 13.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 4.0f));
        if (InputTextWithHintWithTooltip(
                "##HierarchySearch",
                "Search entities...",
                hierarchySearchBuffer.data(),
                hierarchySearchBuffer.size()))
        {
            m_SearchQuery = hierarchySearchBuffer.data();
        }
        ShowItemTooltip("Filter hierarchy entities by name.");
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);

        if (ImGui::BeginPopup("EntityCreationPopup"))
        {
            if (context.drawEntityCreationMenu)
            {
                context.drawEntityCreationMenu(entt::null);
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupContextWindow(
                "SceneHierarchyContext",
                ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
        {
            if (context.drawEntityCreationMenu)
            {
                context.drawEntityCreationMenu(entt::null);
            }
            ImGui::EndPopup();
        }

        ImGui::Separator();

        std::string hierarchyFilterLower = ToLowerString(m_SearchQuery);
        const std::vector<EntityID> rootEntities = BuildVisibleEntities(context, context.scene->GetRootEntities());
        EntityID pendingDelete = entt::null;
        for (const EntityID rootEntity : rootEntities)
        {
            DrawEntityNode(context, rootEntity, pendingDelete, hierarchyFilterLower);
        }

        if (pendingDelete != entt::null)
        {
            context.scene->DestroyEntity(pendingDelete);
            if (context.pruneSelection)
            {
                context.pruneSelection();
            }
            if (context.markSceneRenderCacheDirty)
            {
                context.markSceneRenderCacheDirty();
            }
        }

        if (rootEntities.empty())
        {
            ImGui::TextDisabled("No entities in scene.");
        }
        else if (!hierarchyFilterLower.empty() && pendingDelete == entt::null)
        {
            bool hasVisibleEntities = false;
            for (const EntityID rootEntity : rootEntities)
            {
                if (EntityMatchesFilter(context, rootEntity, hierarchyFilterLower))
                {
                    hasVisibleEntities = true;
                    break;
                }
            }
            if (!hasVisibleEntities)
            {
                ImGui::TextDisabled("No entities match search.");
            }
        }

        const ImVec2 hierarchyDropTargetSize(
            std::max(ImGui::GetContentRegionAvail().x, 1.0f),
            std::max(ImGui::GetContentRegionAvail().y, 28.0f));
        ImGui::InvisibleButton("##HierarchyRootMeshDropTarget", hierarchyDropTargetSize);
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ASSET_PATH"))
            {
                if (payload->Data != nullptr && payload->DataSize > 1)
                {
                    const std::filesystem::path assetPath(static_cast<const char*>(payload->Data));
                    const bool candidate = context.isMeshAssetPathCandidate
                        ? context.isMeshAssetPathCandidate(assetPath)
                        : IsMeshAssetPathCandidate(assetPath);
                    if (candidate &&
                        context.createEntityFromMeshAsset &&
                        context.createEntityFromMeshAsset(assetPath, entt::null) != entt::null &&
                        context.setContentStatus)
                    {
                        context.setContentStatus("Created mesh entity from " + assetPath.filename().string() + ".");
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::End();
    }

    bool HierarchyPanel::EntityMatchesFilter(
        const HierarchyPanelContext& context,
        const EntityID entity,
        const std::string& filterLower) const
    {
        if (context.scene == nullptr || filterLower.empty())
        {
            return true;
        }

        const auto& registry = context.scene->GetRegistry();
        if (!registry.valid(entity) || !registry.all_of<TagComponent, RelationshipComponent>(entity))
        {
            return false;
        }
        if (context.isEntityHidden && context.isEntityHidden(entity))
        {
            return false;
        }

        const auto& tag = registry.get<TagComponent>(entity);
        std::string label = ToLowerString(tag.name.empty() ? "Entity" : tag.name);
        if (label.find(filterLower) != std::string::npos)
        {
            return true;
        }

        const auto& relationship = registry.get<RelationshipComponent>(entity);
        for (const EntityID childEntity : relationship.children)
        {
            if (EntityMatchesFilter(context, childEntity, filterLower))
            {
                return true;
            }
        }

        return false;
    }

    void HierarchyPanel::DrawEntityNode(
        const HierarchyPanelContext& context,
        const EntityID entity,
        EntityID& pendingDeleteEntity,
        const std::string& filterLower)
    {
        if (context.scene == nullptr)
        {
            return;
        }

        auto& registry = context.scene->GetRegistry();
        if (!registry.valid(entity) || !registry.all_of<TagComponent, RelationshipComponent>(entity))
        {
            return;
        }
        if (context.isEntityHidden && context.isEntityHidden(entity))
        {
            return;
        }
        if (!EntityMatchesFilter(context, entity, filterLower))
        {
            return;
        }

        const auto& tag = registry.get<TagComponent>(entity);
        const auto& relationship = registry.get<RelationshipComponent>(entity);
        const std::string label = tag.name.empty() ? "Entity" : tag.name;
        const HierarchyIconStyle iconStyle = ResolveHierarchyIconStyle(registry, entity);

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
        if (relationship.children.empty())
        {
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }
        if (context.isEntitySelected && context.isEntitySelected(entity))
        {
            flags |= ImGuiTreeNodeFlags_Selected;
        }

        ImGui::PushID(static_cast<int>(entt::to_integral(entity)));
        const bool opened = ImGui::TreeNodeEx("##EntityNode", flags);
        const bool treeItemClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);

        ImGui::SameLine();
        bool drewIconTexture = false;
        if (void* iconTexture = ResolveHierarchyEntityIconTexture(context, registry, entity); iconTexture != nullptr)
        {
            ImGui::Image(
                reinterpret_cast<ImTextureID>(iconTexture),
                ImVec2(16.0f, 16.0f),
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f));
            drewIconTexture = true;
        }
        if (!drewIconTexture)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, iconStyle.color);
            ImGui::TextUnformatted(iconStyle.label);
            ImGui::PopStyleColor();
        }
        ShowItemTooltip("Entity type tag.");
        ImGui::SameLine();
        ImGui::TextUnformatted(label.c_str());
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::SetTooltip(
                "Entity: %s\nLeft Click: Select\nCtrl+Click: Multi-select",
                label.c_str());
        }

        if (treeItemClicked || ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            const bool additiveSelection = ImGui::GetIO().KeyCtrl;
            if (additiveSelection)
            {
                if (context.toggleEntitySelection)
                {
                    context.toggleEntitySelection(entity);
                }
            }
            else
            {
                if (context.selectSingleEntity)
                {
                    context.selectSingleEntity(entity);
                }
            }
        }

        if (ImGui::BeginPopupContextItem("HierarchyEntityContextMenu"))
        {
            if (ImGui::BeginMenu("Create Child"))
            {
                if (context.drawEntityCreationMenu)
                {
                    context.drawEntityCreationMenu(entity);
                }
                ImGui::EndMenu();
            }

            if (context.createPrefabFromEntity && MenuItemWithTooltip("Create Prefab"))
            {
                context.createPrefabFromEntity(entity);
            }
            ShowItemTooltip("Create a prefab asset from this entity hierarchy.");

            if (relationship.parent != entt::null && MenuItemWithTooltip("Unparent"))
            {
                context.scene->Unparent(entity);
            }
            ShowItemTooltip("Detach this entity from its parent.");

            if (MenuItemWithTooltip("Delete"))
            {
                pendingDeleteEntity = entity;
            }
            ShowItemTooltip("Delete this entity from the scene.");
            ImGui::EndPopup();
        }

        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ASSET_PATH"))
            {
                if (payload->Data != nullptr && payload->DataSize > 1)
                {
                    const std::filesystem::path assetPath(static_cast<const char*>(payload->Data));
                    const bool candidate = context.isMeshAssetPathCandidate
                        ? context.isMeshAssetPathCandidate(assetPath)
                        : IsMeshAssetPathCandidate(assetPath);
                    if (candidate && context.createEntityFromMeshAsset)
                    {
                        context.createEntityFromMeshAsset(assetPath, entity);
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }

        if (!relationship.children.empty() && opened)
        {
            const std::vector<EntityID> visibleChildren = BuildVisibleEntities(context, relationship.children);
            for (const EntityID childEntity : visibleChildren)
            {
                DrawEntityNode(context, childEntity, pendingDeleteEntity, filterLower);
            }
            ImGui::TreePop();
        }

        ImGui::PopID();
    }
}
