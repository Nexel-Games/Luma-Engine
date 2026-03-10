#include "Luma/Editor/Panels/Inspector/InspectorMeshRendererPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

#include <imgui.h>

#include "Luma/Core/App/Project.h"
#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/MeshRendererComponent.h"

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
        bool CheckboxWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::Checkbox(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Toggle ");
            return changed;
        }

        template <typename... Args>
        bool ComboWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::Combo(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Choose ");
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

        template <typename... Args>
        bool InputIntWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::InputInt(label, std::forward<Args>(args)...);
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
        bool ColorEdit4WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::ColorEdit4(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }

        const char* PrimitiveTypeLabel(const PrimitiveType type)
        {
            switch (type)
            {
            case PrimitiveType::Cube:
                return "Cube";
            case PrimitiveType::Plane:
                return "Plane";
            case PrimitiveType::Sphere:
                return "Sphere";
            case PrimitiveType::Cylinder:
                return "Cylinder";
            case PrimitiveType::Capsule:
                return "Capsule";
            case PrimitiveType::Cone:
                return "Cone";
            case PrimitiveType::Torus:
                return "Torus";
            default:
                return "Primitive";
            }
        }
    }

    void InspectorMeshRendererPanel::Draw(const InspectorMeshRendererPanelContext& context)
    {
        if (context.scene == nullptr || context.selectedEntity == entt::null)
        {
            return;
        }

        auto& registry = context.scene->GetRegistry();
        if (!registry.valid(context.selectedEntity) || !registry.all_of<MeshRendererComponent>(context.selectedEntity))
        {
            return;
        }

        auto& meshRenderer = registry.get<MeshRendererComponent>(context.selectedEntity);
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ShowItemTooltip("Configure mesh visibility, primitive selection, streamed mesh assets, and vertex color.");
            ImGui::PushID("MeshRendererComponent");
            bool geometryDirty = false;
            bool materialsDirty = false;

            if (CheckboxWithTooltip("Visible", &meshRenderer.visible))
            {
                materialsDirty = true;
            }
            if (CheckboxWithTooltip("Use Primitive", &meshRenderer.usePrimitive))
            {
                geometryDirty = true;
            }

            int primitiveIndex = static_cast<int>(meshRenderer.primitive);
            const char* primitiveItems[] = { "Cube", "Plane", "Sphere", "Cylinder", "Capsule", "Cone", "Torus" };
            if (meshRenderer.usePrimitive)
            {
                if (ComboWithTooltip("Primitive", &primitiveIndex, primitiveItems, IM_ARRAYSIZE(primitiveItems)))
                {
                    primitiveIndex = std::clamp(primitiveIndex, 0, static_cast<int>(IM_ARRAYSIZE(primitiveItems)) - 1);
                    meshRenderer.primitive = static_cast<PrimitiveType>(primitiveIndex);
                    meshRenderer.usePrimitive = true;
                    if (context.ensurePrimitiveCollider)
                    {
                        context.ensurePrimitiveCollider(context.selectedEntity, meshRenderer.primitive);
                    }
                    geometryDirty = true;
                }
                ImGui::TextDisabled("Selected: %s", PrimitiveTypeLabel(meshRenderer.primitive));
            }
            else
            {
                std::vector<std::filesystem::path> rootPaths =
                    context.listContentRoots ? context.listContentRoots() : std::vector<std::filesystem::path> {};
                if (rootPaths.empty() && Project::IsLoaded())
                {
                    rootPaths.push_back(Project::GetAssetsPath());
                }
                const std::string previousMeshSource = meshRenderer.meshSource;
                m_MeshAssetPickerPanel.Draw(
                    {
                        &m_MeshAssetPickerService,
                        &rootPaths,
                        Project::IsLoaded(),
                        Project::IsLoaded() ? Project::GetAssetsPath() : std::filesystem::path {}
                    },
                    meshRenderer.meshSource);
                if (meshRenderer.meshSource != previousMeshSource)
                {
                    geometryDirty = true;
                }

                ImGui::TextDisabled(
                    "Selected Mesh: %s",
                    meshRenderer.meshSource.empty() ? "None" : meshRenderer.meshSource.c_str());

                ImGui::TextDisabled("Materials are assigned through Material Component.");

                if (CheckboxWithTooltip("Auto Stream LOD", &meshRenderer.autoStreamLod))
                {
                    geometryDirty = true;
                }
                if (meshRenderer.autoStreamLod)
                {
                    int maxAutoLod = static_cast<int>(meshRenderer.maxAutoLod);
                    if (InputIntWithTooltip("Max Auto LOD", &maxAutoLod))
                    {
                        meshRenderer.maxAutoLod = static_cast<std::uint32_t>(std::max(maxAutoLod, 1));
                        geometryDirty = true;
                    }
                    if (DragFloatWithTooltip("LOD Near Distance", &meshRenderer.lodNearDistance, 1.0f, 0.0f, 100000.0f, "%.1f"))
                    {
                        geometryDirty = true;
                    }
                    if (DragFloatWithTooltip("LOD Far Distance", &meshRenderer.lodFarDistance, 1.0f, 1.0f, 100000.0f, "%.1f"))
                    {
                        geometryDirty = true;
                    }
                }
                else
                {
                    int meshLod = static_cast<int>(meshRenderer.meshLod);
                    if (InputIntWithTooltip("Mesh LOD", &meshLod))
                    {
                        meshRenderer.meshLod = static_cast<std::uint32_t>(std::max(meshLod, 0));
                        geometryDirty = true;
                    }
                }

                if (CheckboxWithTooltip("Distance Sections", &meshRenderer.streamSectionsByDistance))
                {
                    geometryDirty = true;
                }
                if (meshRenderer.streamSectionsByDistance)
                {
                    if (DragFloatWithTooltip(
                            "Section Load Distance",
                            &meshRenderer.sectionLoadDistance,
                            1.0f,
                            1.0f,
                            100000.0f,
                            "%.1f"))
                    {
                        geometryDirty = true;
                    }
                }
            }
            if (ColorEdit4WithTooltip("Color", meshRenderer.color.data()))
            {
                materialsDirty = true;
            }
            if (geometryDirty && context.markSceneGeometryDirty)
            {
                context.markSceneGeometryDirty();
            }
            if (materialsDirty && context.markSceneMaterialsDirty)
            {
                context.markSceneMaterialsDirty();
            }
            ImGui::PopID();
        }

        if (ButtonWithTooltip("Remove Mesh Renderer Component"))
        {
            registry.remove<MeshRendererComponent>(context.selectedEntity);
            if (context.markSceneGeometryDirty)
            {
                context.markSceneGeometryDirty();
            }
        }
    }
}
