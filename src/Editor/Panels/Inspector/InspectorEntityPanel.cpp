#include "Luma/Editor/Panels/Inspector/InspectorEntityPanel.h"

#include <array>
#include <cstdio>
#include <utility>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/IDComponent.h"
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
        std::snprintf(nameBuffer.data(), nameBuffer.size(), "%s", tag.tag.c_str());
        if (InputTextWithTooltip("Name", nameBuffer.data(), nameBuffer.size()))
        {
            tag.tag = nameBuffer[0] != '\0' ? nameBuffer.data() : "Entity";
        }

        ImGui::TextDisabled("UUID: %llu", static_cast<unsigned long long>(id.id));

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
            ImGui::Text("Parent: %s", parentTag.tag.c_str());
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
    }
}
