#include "Luma/Editor/Panels/Inspector/InspectorFieldBuoyancyPanel.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/BuoyancyComponent.h"
#include "Luma/Scene/ForceFieldComponent.h"

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
        bool ComboWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::Combo(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Choose ");
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
        bool DragFloat3WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragFloat3(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        void DrawUUIDField(const char* label, UUID& value, const char* tooltip)
        {
            unsigned long long rawValue = static_cast<unsigned long long>(value);
            if (ImGui::InputScalar(label, ImGuiDataType_U64, &rawValue))
            {
                value = static_cast<UUID>(rawValue);
            }
            ShowItemTooltip(tooltip);
        }

        const char* ForceFieldShapeLabel(const ForceFieldShape shape)
        {
            switch (shape)
            {
            case ForceFieldShape::Box:
                return "Box";
            case ForceFieldShape::Sphere:
                return "Sphere";
            case ForceFieldShape::Capsule:
                return "Capsule";
            default:
                return "Unknown";
            }
        }

        const char* ForceFieldTypeLabel(const ForceFieldType type)
        {
            switch (type)
            {
            case ForceFieldType::Directional:
                return "Directional";
            case ForceFieldType::Radial:
                return "Radial";
            case ForceFieldType::Custom:
                return "Custom";
            default:
                return "Unknown";
            }
        }
    }

    void InspectorFieldBuoyancyPanel::Draw(const InspectorFieldBuoyancyPanelContext& context)
    {
        if (context.scene == nullptr || context.selectedEntity == entt::null)
        {
            return;
        }

        auto& registry = context.scene->GetRegistry();
        if (!registry.valid(context.selectedEntity))
        {
            return;
        }

        if (registry.all_of<ForceFieldComponent>(context.selectedEntity))
        {
            auto& field = registry.get<ForceFieldComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Force Field", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Apply directional/radial/custom force inside a box, sphere, or capsule volume.");
                ImGui::PushID("ForceFieldComponent");
                CheckboxWithTooltip("Active", &field.active);

                int shapeIndex = static_cast<int>(field.shape);
                const char* shapeItems[] = { "Box", "Sphere", "Capsule" };
                if (ComboWithTooltip("Shape", &shapeIndex, shapeItems, IM_ARRAYSIZE(shapeItems)))
                {
                    shapeIndex = std::clamp(shapeIndex, 0, 2);
                    field.shape = static_cast<ForceFieldShape>(shapeIndex);
                }
                ImGui::TextDisabled("Resolved Shape: %s", ForceFieldShapeLabel(field.shape));

                int typeIndex = static_cast<int>(field.type);
                const char* typeItems[] = { "Directional", "Radial", "Custom" };
                if (ComboWithTooltip("Type", &typeIndex, typeItems, IM_ARRAYSIZE(typeItems)))
                {
                    typeIndex = std::clamp(typeIndex, 0, 2);
                    field.type = static_cast<ForceFieldType>(typeIndex);
                }
                ImGui::TextDisabled("Resolved Type: %s", ForceFieldTypeLabel(field.type));

                DragFloat3WithTooltip("Center", field.center.data(), 0.01f);
                if (field.shape == ForceFieldShape::Box)
                {
                    DragFloat3WithTooltip("Half Extents", field.boxHalfExtents.data(), 0.01f, 0.001f, 100000.0f);
                    for (float& extent : field.boxHalfExtents)
                    {
                        extent = std::max(0.001f, extent);
                    }
                }
                else if (field.shape == ForceFieldShape::Sphere)
                {
                    DragFloatWithTooltip("Radius", &field.sphereRadius, 0.01f, 0.001f, 100000.0f);
                    field.sphereRadius = std::max(0.001f, field.sphereRadius);
                }
                else
                {
                    DragFloatWithTooltip("Radius", &field.capsuleRadius, 0.01f, 0.001f, 100000.0f);
                    DragFloatWithTooltip("Half Height", &field.capsuleHalfHeight, 0.01f, 0.001f, 100000.0f);
                    field.capsuleRadius = std::max(0.001f, field.capsuleRadius);
                    field.capsuleHalfHeight = std::max(0.001f, field.capsuleHalfHeight);
                }

                DragFloat3WithTooltip("Direction", field.direction.data(), 0.01f, -1.0f, 1.0f);
                DragFloatWithTooltip("Strength", &field.strength, 0.1f, -100000.0f, 100000.0f);
                DragFloatWithTooltip("Falloff", &field.falloff, 0.01f, 0.0f, 1000.0f);
                CheckboxWithTooltip("Affect Dynamic Bodies Only", &field.affectDynamicBodiesOnly);
                CheckboxWithTooltip("Affect Characters", &field.affectCharacters);
                ShowItemTooltip("Configure magnitude and filtering of field influence.");
                field.falloff = std::max(0.0f, field.falloff);
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Force Field Component"))
            {
                registry.remove<ForceFieldComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove force-field behavior from this entity.");
        }

        if (registry.all_of<BuoyancyComponent>(context.selectedEntity))
        {
            auto& buoyancy = registry.get<BuoyancyComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Buoyancy", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Floating simulation controls using water level and float points.");
                ImGui::PushID("BuoyancyComponent");
                CheckboxWithTooltip("Active", &buoyancy.active);
                DragFloatWithTooltip("Water Level", &buoyancy.waterLevel, 0.01f, -100000.0f, 100000.0f);
                DrawUUIDField("Water Volume UUID", buoyancy.waterVolumeEntity, "Entity UUID for water volume reference.");
                DragFloatWithTooltip("Density", &buoyancy.density, 0.01f, 0.0f, 1000.0f);
                DragFloatWithTooltip("Drag", &buoyancy.drag, 0.01f, 0.0f, 1000.0f);
                DragFloatWithTooltip("Angular Drag", &buoyancy.angularDrag, 0.01f, 0.0f, 1000.0f);
                ShowItemTooltip("Tune buoyant force and damping.");
                buoyancy.density = std::max(0.0f, buoyancy.density);
                buoyancy.drag = std::max(0.0f, buoyancy.drag);
                buoyancy.angularDrag = std::max(0.0f, buoyancy.angularDrag);

                for (std::size_t index = 0; index < buoyancy.floatPoints.size(); ++index)
                {
                    std::array<char, 32> label {};
                    std::snprintf(label.data(), label.size(), "Float Point %u", static_cast<unsigned int>(index));
                    DragFloat3WithTooltip(label.data(), buoyancy.floatPoints[index].data(), 0.01f);
                    ShowItemTooltip("Local sample point used to compute buoyancy forces.");
                }
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Buoyancy Component"))
            {
                registry.remove<BuoyancyComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove buoyancy behavior from this entity.");
        }
    }
}
