#include "Luma/Editor/Panels/Scene/EntityCreationMenu.h"

#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"

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
        bool MenuItemWithTooltip(const char* label, Args&&... args)
        {
            const bool activated = ImGui::MenuItem(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return activated;
        }
    }

    void EntityCreationMenu::Draw(const EntityCreationMenuContext& context)
    {
        if (!context.createTemplate)
        {
            return;
        }

        if (MenuItemWithTooltip("Empty Entity"))
        {
            context.createTemplate(EntityTemplateKind::Empty, context.parentEntity);
        }
        ShowItemTooltip("Create an empty entity with only transform and tag.");

        if (ImGui::BeginMenu("3D Object"))
        {
            ShowItemTooltip("Create primitive 3D entities.");
            if (MenuItemWithTooltip("Cube"))
            {
                context.createTemplate(EntityTemplateKind::Cube, context.parentEntity);
            }
            ShowItemTooltip("Create a cube mesh entity.");
            if (MenuItemWithTooltip("Plane"))
            {
                context.createTemplate(EntityTemplateKind::Plane, context.parentEntity);
            }
            ShowItemTooltip("Create a plane mesh entity.");
            if (MenuItemWithTooltip("Sphere"))
            {
                context.createTemplate(EntityTemplateKind::Sphere, context.parentEntity);
            }
            ShowItemTooltip("Create a sphere mesh entity.");
            if (MenuItemWithTooltip("Cylinder"))
            {
                context.createTemplate(EntityTemplateKind::Cylinder, context.parentEntity);
            }
            ShowItemTooltip("Create a cylinder mesh entity.");
            if (MenuItemWithTooltip("Capsule"))
            {
                context.createTemplate(EntityTemplateKind::Capsule, context.parentEntity);
            }
            ShowItemTooltip("Create a capsule mesh entity.");
            if (MenuItemWithTooltip("Cone"))
            {
                context.createTemplate(EntityTemplateKind::Cone, context.parentEntity);
            }
            ShowItemTooltip("Create a cone mesh entity.");
            if (MenuItemWithTooltip("Torus"))
            {
                context.createTemplate(EntityTemplateKind::Torus, context.parentEntity);
            }
            ShowItemTooltip("Create a torus mesh entity.");
            ImGui::EndMenu();
        }

        if (MenuItemWithTooltip("Camera"))
        {
            context.createTemplate(EntityTemplateKind::Camera, context.parentEntity);
        }
        ShowItemTooltip("Create a scene camera entity.");

        if (ImGui::BeginMenu("Light"))
        {
            ShowItemTooltip("Create light entities.");
            if (MenuItemWithTooltip("Directional Light"))
            {
                context.createTemplate(EntityTemplateKind::DirectionalLight, context.parentEntity);
            }
            ShowItemTooltip("Create a directional light entity.");
            if (MenuItemWithTooltip("Point Light"))
            {
                context.createTemplate(EntityTemplateKind::PointLight, context.parentEntity);
            }
            ShowItemTooltip("Create a point light entity.");
            if (MenuItemWithTooltip("Sky Light"))
            {
                context.createTemplate(EntityTemplateKind::SkyLight, context.parentEntity);
            }
            ShowItemTooltip("Create a sky light entity.");
            ImGui::EndMenu();
        }

        if (MenuItemWithTooltip("Player"))
        {
            context.createTemplate(EntityTemplateKind::Player, context.parentEntity);
        }
        ShowItemTooltip("Create a default player entity template.");

        if (MenuItemWithTooltip("Audio Source"))
        {
            context.createTemplate(EntityTemplateKind::AudioSource, context.parentEntity);
        }
        ShowItemTooltip("Create an audio source entity with an Audio Source component.");
    }
}
