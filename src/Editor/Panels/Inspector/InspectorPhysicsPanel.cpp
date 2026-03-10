#include "Luma/Editor/Panels/Inspector/InspectorPhysicsPanel.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Renderer/PrimitiveMeshFactory.h"
#include "Luma/Scene/ColliderComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/RigidBodyComponent.h"

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

        template <typename... Args>
        bool InputTextWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::InputText(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }

        const char* RigidBodyTypeLabel(const RigidBodyType bodyType)
        {
            switch (bodyType)
            {
            case RigidBodyType::Static:
                return "Static";
            case RigidBodyType::Dynamic:
                return "Dynamic";
            case RigidBodyType::Kinematic:
                return "Kinematic";
            default:
                return "Rigid Body";
            }
        }

        const char* ColliderShapeLabel(const ColliderShapeType shape)
        {
            switch (shape)
            {
            case ColliderShapeType::Box:
                return "Box";
            case ColliderShapeType::Sphere:
                return "Sphere";
            case ColliderShapeType::Capsule:
                return "Capsule";
            case ColliderShapeType::Cylinder:
                return "Cylinder";
            case ColliderShapeType::Mesh:
                return "Mesh";
            default:
                return "Collider";
            }
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

    void InspectorPhysicsPanel::Draw(const InspectorPhysicsPanelContext& context)
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

        if (registry.all_of<RigidBodyComponent>(context.selectedEntity))
        {
            auto& rigidBody = registry.get<RigidBodyComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Configure rigid body simulation properties.");
                ImGui::PushID("RigidBodyComponent");
                CheckboxWithTooltip("Active", &rigidBody.active);
                ShowItemTooltip("Enable or disable this rigid body in the simulation.");

                int bodyTypeIndex = static_cast<int>(rigidBody.bodyType);
                const char* bodyTypeItems[] = { "Static", "Dynamic", "Kinematic" };
                if (ComboWithTooltip("Body Type", &bodyTypeIndex, bodyTypeItems, IM_ARRAYSIZE(bodyTypeItems)))
                {
                    bodyTypeIndex = std::clamp(bodyTypeIndex, 0, 2);
                    rigidBody.bodyType = static_cast<RigidBodyType>(bodyTypeIndex);
                }
                ShowItemTooltip("Static: no movement. Dynamic: fully simulated. Kinematic: moved by code, affects others.");
                ImGui::TextDisabled("Resolved Type: %s", RigidBodyTypeLabel(rigidBody.bodyType));
                ShowItemTooltip("Current resolved rigid body mode.");

                if (rigidBody.bodyType == RigidBodyType::Dynamic)
                {
                    CheckboxWithTooltip("Enable Gravity", &rigidBody.enableGravity);
                    ShowItemTooltip("Apply scene gravity to this body.");
                    CheckboxWithTooltip("Enable CCD", &rigidBody.enableCCD);
                    ShowItemTooltip("Continuous Collision Detection. Prevents fast objects from tunneling through colliders.");
                    DragFloatWithTooltip("Mass", &rigidBody.mass, 0.05f, 0.001f, 100000.0f);
                    ShowItemTooltip("Body mass used for acceleration and collision response.");
                    rigidBody.mass = std::max(0.001f, rigidBody.mass);
                }

                DragFloatWithTooltip("Linear Damping", &rigidBody.linearDamping, 0.001f, 0.0f, 100.0f);
                ShowItemTooltip("Reduces translational velocity over time (air resistance style drag).");
                DragFloatWithTooltip("Angular Damping", &rigidBody.angularDamping, 0.001f, 0.0f, 100.0f);
                ShowItemTooltip("Reduces rotational velocity over time.");
                DragFloatWithTooltip("Max Linear Velocity", &rigidBody.maxLinearVelocity, 0.1f, 0.0f, 10000.0f);
                ShowItemTooltip("Clamp for maximum translational speed.");
                DragFloatWithTooltip("Max Angular Velocity", &rigidBody.maxAngularVelocity, 0.1f, 0.0f, 10000.0f);
                ShowItemTooltip("Clamp for maximum rotational speed.");
                rigidBody.maxLinearVelocity = std::max(0.0f, rigidBody.maxLinearVelocity);
                rigidBody.maxAngularVelocity = std::max(0.0f, rigidBody.maxAngularVelocity);

                DragFloat3WithTooltip("Linear Velocity", rigidBody.linearVelocity.data(), 0.05f);
                ShowItemTooltip("Initial/current world-space translational velocity.");
                DragFloat3WithTooltip("Angular Velocity", rigidBody.angularVelocity.data(), 0.05f);
                ShowItemTooltip("Initial/current rotational velocity (degrees/sec style control).");

                const char* axisLabels[] = { "X", "Y", "Z" };
                ImGui::TextDisabled("Lock Position");
                ShowItemTooltip("Lock movement per axis.");
                for (int axis = 0; axis < 3; ++axis)
                {
                    ImGui::PushID(axis);
                    CheckboxWithTooltip(axisLabels[axis], &rigidBody.lockLinearAxes[axis]);
                    ShowItemTooltip("Prevent movement along this axis.");
                    if (axis < 2)
                    {
                        ImGui::SameLine();
                    }
                    ImGui::PopID();
                }

                ImGui::TextDisabled("Lock Rotation");
                ShowItemTooltip("Lock rotation per axis.");
                for (int axis = 0; axis < 3; ++axis)
                {
                    ImGui::PushID(10 + axis);
                    CheckboxWithTooltip(axisLabels[axis], &rigidBody.lockAngularAxes[axis]);
                    ShowItemTooltip("Prevent rotation around this axis.");
                    if (axis < 2)
                    {
                        ImGui::SameLine();
                    }
                    ImGui::PopID();
                }

                ImGui::TextDisabled("Sleeping: %s", rigidBody.sleeping ? "Yes" : "No");
                ShowItemTooltip("Sleeping bodies are currently inactive to save simulation cost.");
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Rigid Body Component"))
            {
                registry.remove<RigidBodyComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove rigid body simulation from this entity.");
        }

        if (registry.all_of<ColliderComponent>(context.selectedEntity))
        {
            auto& collider = registry.get<ColliderComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Collider", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Configure collider shape, dimensions, and physical material.");
                ImGui::PushID("ColliderComponent");
                CheckboxWithTooltip("Active", &collider.active);
                ShowItemTooltip("Enable or disable this collider in collision queries and simulation.");
                CheckboxWithTooltip("Trigger", &collider.isTrigger);
                ShowItemTooltip("Trigger colliders detect overlap events but do not physically block bodies.");

                int shapeIndex = static_cast<int>(collider.shape);
                const char* shapeItems[] = { "Box", "Sphere", "Capsule", "Mesh", "Cylinder" };
                if (ComboWithTooltip("Shape", &shapeIndex, shapeItems, IM_ARRAYSIZE(shapeItems)))
                {
                    shapeIndex = std::clamp(shapeIndex, 0, 4);
                    collider.shape = static_cast<ColliderShapeType>(shapeIndex);
                }
                ShowItemTooltip("Choose the collider primitive used for this entity.");
                ImGui::TextDisabled("Resolved Shape: %s", ColliderShapeLabel(collider.shape));
                ShowItemTooltip("Currently active collider geometry type.");

                DragFloat3WithTooltip("Center", collider.center.data(), 0.01f);
                ShowItemTooltip("Local-space offset from entity origin to collider center.");
                if (collider.shape == ColliderShapeType::Box)
                {
                    DragFloat3WithTooltip("Half Extents", collider.boxHalfExtents.data(), 0.01f, 0.001f, 10000.0f);
                    ShowItemTooltip("Half-size of the box collider on each axis.");
                    for (float& value : collider.boxHalfExtents)
                    {
                        value = std::max(0.001f, value);
                    }
                }
                else if (collider.shape == ColliderShapeType::Sphere)
                {
                    DragFloatWithTooltip("Radius", &collider.sphereRadius, 0.01f, 0.001f, 10000.0f);
                    ShowItemTooltip("Radius of the sphere collider.");
                    collider.sphereRadius = std::max(0.001f, collider.sphereRadius);
                }
                else if (collider.shape == ColliderShapeType::Capsule || collider.shape == ColliderShapeType::Cylinder)
                {
                    DragFloatWithTooltip("Radius", &collider.capsuleRadius, 0.01f, 0.001f, 10000.0f);
                    ShowItemTooltip(
                        collider.shape == ColliderShapeType::Cylinder
                            ? "Cylinder radius."
                            : "Capsule radius.");
                    DragFloatWithTooltip("Half Height", &collider.capsuleHalfHeight, 0.01f, 0.001f, 10000.0f);
                    ShowItemTooltip(
                        collider.shape == ColliderShapeType::Cylinder
                            ? "Half height of the cylinder body."
                            : "Half height of the capsule center segment.");
                    collider.capsuleRadius = std::max(0.001f, collider.capsuleRadius);
                    collider.capsuleHalfHeight = std::max(0.001f, collider.capsuleHalfHeight);
                }
                else
                {
                    CheckboxWithTooltip("Convex", &collider.meshConvex);
                    ShowItemTooltip("Use convex mesh collision when supported; better for dynamic bodies.");

                    const MeshRendererComponent* selectedMeshRenderer = registry.try_get<MeshRendererComponent>(context.selectedEntity);
                    const bool usesPrimitiveMesh = selectedMeshRenderer != nullptr && selectedMeshRenderer->usePrimitive;
                    if (usesPrimitiveMesh)
                    {
                        ImGui::TextDisabled("Primitive Source: %s", PrimitiveTypeLabel(selectedMeshRenderer->primitive));
                        ShowItemTooltip("Mesh collider uses the selected primitive mesh. Mesh Source path is not required.");

                        const PrimitiveMeshData& primitiveMesh =
                            PrimitiveMeshFactory::GetPrimitive(selectedMeshRenderer->primitive);
                        std::array<float, 3> autoHalfExtents { 0.5f, 0.5f, 0.5f };
                        if (!primitiveMesh.vertices.empty())
                        {
                            std::array<float, 3> minBounds {
                                std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max(),
                                std::numeric_limits<float>::max()
                            };
                            std::array<float, 3> maxBounds {
                                std::numeric_limits<float>::lowest(),
                                std::numeric_limits<float>::lowest(),
                                std::numeric_limits<float>::lowest()
                            };
                            for (const PrimitiveVertex& vertex : primitiveMesh.vertices)
                            {
                                minBounds[0] = std::min(minBounds[0], vertex.position[0]);
                                minBounds[1] = std::min(minBounds[1], vertex.position[1]);
                                minBounds[2] = std::min(minBounds[2], vertex.position[2]);
                                maxBounds[0] = std::max(maxBounds[0], vertex.position[0]);
                                maxBounds[1] = std::max(maxBounds[1], vertex.position[1]);
                                maxBounds[2] = std::max(maxBounds[2], vertex.position[2]);
                            }

                            autoHalfExtents = {
                                std::max(0.001f, (maxBounds[0] - minBounds[0]) * 0.5f),
                                std::max(0.001f, (maxBounds[1] - minBounds[1]) * 0.5f),
                                std::max(0.001f, (maxBounds[2] - minBounds[2]) * 0.5f)
                            };
                        }

                        collider.boxHalfExtents = autoHalfExtents;
                        ImGui::TextDisabled(
                            "Auto Half Extents: %.3f, %.3f, %.3f",
                            autoHalfExtents[0],
                            autoHalfExtents[1],
                            autoHalfExtents[2]);
                        ShowItemTooltip("Auto-generated primitive mesh bounds used for fallback and debug.");
                    }
                    else
                    {
                        std::array<char, 512> meshPathBuffer {};
                        std::snprintf(meshPathBuffer.data(), meshPathBuffer.size(), "%s", collider.meshSource.c_str());
                        if (InputTextWithTooltip("Mesh Source", meshPathBuffer.data(), meshPathBuffer.size()))
                        {
                            collider.meshSource = meshPathBuffer.data();
                        }
                        ShowItemTooltip("Path to source mesh used to build mesh collider geometry.");
                        ImGui::TextDisabled("Fallback Extents");
                        ShowItemTooltip("Fallback box extents used when mesh collider data is unavailable.");
                        DragFloat3WithTooltip("Half Extents", collider.boxHalfExtents.data(), 0.01f, 0.001f, 10000.0f);
                        ShowItemTooltip("Fallback half-size for mesh-collider approximation.");
                        for (float& value : collider.boxHalfExtents)
                        {
                            value = std::max(0.001f, value);
                        }
                    }
                }

                ImGui::SeparatorText("Material");
                DragFloatWithTooltip("Static Friction", &collider.material.staticFriction, 0.01f, 0.0f, 10.0f);
                ShowItemTooltip("Resistance to start moving when in contact.");
                DragFloatWithTooltip("Dynamic Friction", &collider.material.dynamicFriction, 0.01f, 0.0f, 10.0f);
                ShowItemTooltip("Resistance while surfaces slide.");
                DragFloatWithTooltip("Restitution", &collider.material.restitution, 0.01f, 0.0f, 1.0f);
                ShowItemTooltip("Bounciness of collisions (0 = no bounce, 1 = fully elastic).");
                collider.material.staticFriction = std::clamp(collider.material.staticFriction, 0.0f, 10.0f);
                collider.material.dynamicFriction = std::clamp(collider.material.dynamicFriction, 0.0f, 10.0f);
                collider.material.restitution = std::clamp(collider.material.restitution, 0.0f, 1.0f);
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Collider Component"))
            {
                registry.remove<ColliderComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove collision shape from this entity.");
        }
    }
}
