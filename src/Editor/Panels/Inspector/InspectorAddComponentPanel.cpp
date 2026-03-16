#include "Luma/Editor/Panels/Inspector/InspectorAddComponentPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/AudioListenerComponent.h"
#include "Luma/Scene/AudioSourceComponent.h"
#include "Luma/Scene/BuoyancyComponent.h"
#include "Luma/Scene/CameraComponent.h"
#include "Luma/Scene/CharacterControllerComponent.h"
#include "Luma/Scene/ColliderComponent.h"
#include "Luma/Scene/D6JointComponent.h"
#include "Luma/Scene/DestructibleComponent.h"
#include "Luma/Scene/DirectionalLightComponent.h"
#include "Luma/Scene/FixedJointComponent.h"
#include "Luma/Scene/ForceFieldComponent.h"
#include "Luma/Scene/HingeJointComponent.h"
#include "Luma/Scene/JointComponent.h"
#include "Luma/Scene/LuaScriptComponent.h"
#include "Luma/Scene/MaterialComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/PointLightComponent.h"
#include "Luma/Scene/PhysicsEventsComponent.h"
#include "Luma/Scene/PostProcessComponent.h"
#include "Luma/Scene/RagdollComponent.h"
#include "Luma/Scene/RigidBodyComponent.h"
#include "Luma/Scene/SliderJointComponent.h"
#include "Luma/Scene/SkyLightComponent.h"
#include "Luma/Scene/SpotLightComponent.h"
#include "Luma/Scene/VehicleComponent.h"
#include "Luma/Scene/VehicleInputComponent.h"
#include "Luma/Scene/WheelColliderComponent.h"

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
    }

    void InspectorAddComponentPanel::Draw(const InspectorAddComponentPanelContext& context)
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

        ImGui::Separator();
        if (ButtonWithTooltip("Add Component"))
        {
            ImGui::OpenPopup("AddComponentPopup");
        }
        if (!ImGui::BeginPopup("AddComponentPopup"))
        {
            return;
        }

        static std::array<char, 128> componentSearchBuffer {};
        if (ImGui::IsWindowAppearing())
        {
            componentSearchBuffer[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }

        ImGui::SetNextItemWidth(240.0f);
        InputTextWithHintWithTooltip(
            "##AddComponentSearch",
            "Search components...",
            componentSearchBuffer.data(),
            componentSearchBuffer.size());

        const std::string filter = ToLowerString(componentSearchBuffer.data());
        const auto matchesFilter = [&](const char* name) -> bool
        {
            if (filter.empty())
            {
                return true;
            }
            return ToLowerString(name).find(filter) != std::string::npos;
        };

        ImGui::Separator();

        const bool canAddMesh = !registry.all_of<MeshRendererComponent>(context.selectedEntity);
        const bool canAddMaterial = !registry.all_of<MaterialComponent>(context.selectedEntity);
        const bool canAddCamera = !registry.all_of<CameraComponent>(context.selectedEntity);
        const bool canAddAudioSource = !registry.all_of<AudioSourceComponent>(context.selectedEntity);
        const bool canAddAudioListener = !registry.all_of<AudioListenerComponent>(context.selectedEntity);
        const bool canAddLuaScript = !registry.all_of<LuaScriptComponent>(context.selectedEntity);
        const bool canAddDirectional = !registry.all_of<DirectionalLightComponent>(context.selectedEntity);
        const bool canAddPoint = !registry.all_of<PointLightComponent>(context.selectedEntity);
        const bool canAddSpot = !registry.all_of<SpotLightComponent>(context.selectedEntity);
        const bool canAddSky = !registry.all_of<SkyLightComponent>(context.selectedEntity);
        const bool canAddPostProcess = !registry.all_of<PostProcessComponent>(context.selectedEntity);
        const bool canAddRigidBody = !registry.all_of<RigidBodyComponent>(context.selectedEntity);
        const bool canAddCollider = !registry.all_of<ColliderComponent>(context.selectedEntity);
        const bool canAddJoint = !registry.all_of<JointComponent>(context.selectedEntity);
        const bool canAddFixedJoint = !registry.all_of<FixedJointComponent>(context.selectedEntity);
        const bool canAddHingeJoint = !registry.all_of<HingeJointComponent>(context.selectedEntity);
        const bool canAddSliderJoint = !registry.all_of<SliderJointComponent>(context.selectedEntity);
        const bool canAddD6Joint = !registry.all_of<D6JointComponent>(context.selectedEntity);
        const bool canAddCharacterController = !registry.all_of<CharacterControllerComponent>(context.selectedEntity);
        const bool canAddWheelCollider = !registry.all_of<WheelColliderComponent>(context.selectedEntity);
        const bool canAddVehicle = !registry.all_of<VehicleComponent>(context.selectedEntity);
        const bool canAddVehicleInput = !registry.all_of<VehicleInputComponent>(context.selectedEntity);
        const bool canAddForceField = !registry.all_of<ForceFieldComponent>(context.selectedEntity);
        const bool canAddBuoyancy = !registry.all_of<BuoyancyComponent>(context.selectedEntity);
        const bool canAddPhysicsEvents = !registry.all_of<PhysicsEventsComponent>(context.selectedEntity);
        const bool canAddRagdoll = !registry.all_of<RagdollComponent>(context.selectedEntity);
        const bool canAddDestructible = !registry.all_of<DestructibleComponent>(context.selectedEntity);
        const bool canAddAny =
            canAddMesh || canAddMaterial || canAddCamera || canAddAudioSource || canAddAudioListener || canAddLuaScript || canAddDirectional || canAddPoint || canAddSpot || canAddSky || canAddPostProcess || canAddRigidBody || canAddCollider ||
            canAddJoint || canAddFixedJoint || canAddHingeJoint || canAddSliderJoint || canAddD6Joint ||
            canAddCharacterController || canAddWheelCollider || canAddVehicle || canAddVehicleInput || canAddForceField || canAddBuoyancy ||
            canAddPhysicsEvents || canAddRagdoll || canAddDestructible;
        bool displayedAny = false;

        const bool hasRenderingEntries =
            (canAddMesh && matchesFilter("Mesh Renderer")) ||
            (canAddMaterial && matchesFilter("Material"));
        const bool hasCameraEntries = canAddCamera && matchesFilter("Camera");
        const bool hasAudioEntries =
            (canAddAudioSource && matchesFilter("Audio Source")) ||
            (canAddAudioListener && matchesFilter("Audio Listener"));
        const bool hasScriptingEntries = canAddLuaScript && matchesFilter("Lua Script");
        const bool hasLightingEntries =
            (canAddDirectional && matchesFilter("Directional Light")) ||
            (canAddPoint && matchesFilter("Point Light")) ||
            (canAddSpot && matchesFilter("Spot Light")) ||
            (canAddSky && matchesFilter("Sky Light"));
        const bool hasEffectsEntries = canAddPostProcess && matchesFilter("Post Process");
        const bool hasPhysicsEntries =
            (canAddRigidBody && matchesFilter("Rigid Body")) ||
            (canAddCollider && matchesFilter("Collider")) ||
            (canAddJoint && matchesFilter("Joint")) ||
            (canAddFixedJoint && matchesFilter("Fixed Joint")) ||
            (canAddHingeJoint && matchesFilter("Hinge Joint")) ||
            (canAddSliderJoint && matchesFilter("Slider Joint")) ||
            (canAddD6Joint && matchesFilter("D6 Joint")) ||
            (canAddCharacterController && matchesFilter("Character Controller")) ||
            (canAddWheelCollider && matchesFilter("Wheel Collider")) ||
            (canAddVehicle && matchesFilter("Vehicle")) ||
            (canAddForceField && matchesFilter("Force Field")) ||
            (canAddBuoyancy && matchesFilter("Buoyancy")) ||
            (canAddPhysicsEvents && matchesFilter("Physics Events")) ||
            (canAddRagdoll && matchesFilter("Ragdoll")) ||
            (canAddDestructible && matchesFilter("Destruction"));

        if (hasRenderingEntries && ImGui::BeginMenu("Rendering"))
        {
            displayedAny = true;
            ShowItemTooltip("Rendering components: meshes and materials.");
            if (canAddMesh && matchesFilter("Mesh Renderer"))
            {
                if (MenuItemWithTooltip("Mesh Renderer"))
                {
                    auto& meshRenderer = registry.emplace<MeshRendererComponent>(context.selectedEntity);
                    meshRenderer.visible = true;
                    meshRenderer.usePrimitive = true;
                    meshRenderer.primitive = PrimitiveType::Cube;
                    meshRenderer.color = { 0.72f, 0.82f, 0.95f, 1.0f };
                    if (!registry.all_of<MaterialComponent>(context.selectedEntity))
                    {
                        auto& material = registry.emplace<MaterialComponent>(context.selectedEntity);
                        if (context.initializeDefaultMaterial)
                        {
                            context.initializeDefaultMaterial(material);
                        }
                    }
                    if (context.ensurePrimitiveCollider)
                    {
                        context.ensurePrimitiveCollider(context.selectedEntity, meshRenderer.primitive);
                    }
                    if (context.markSceneGeometryDirty)
                    {
                        context.markSceneGeometryDirty();
                    }
                    ImGui::CloseCurrentPopup();
                }
                ShowItemTooltip("Adds renderable mesh properties to this entity.");
            }
            if (canAddMaterial && matchesFilter("Material"))
            {
                if (MenuItemWithTooltip("Material"))
                {
                    auto& material = registry.emplace<MaterialComponent>(context.selectedEntity);
                    if (context.initializeDefaultMaterial)
                    {
                        context.initializeDefaultMaterial(material);
                    }
                    if (context.markSceneMaterialsDirty)
                    {
                        context.markSceneMaterialsDirty();
                    }
                    ImGui::CloseCurrentPopup();
                }
                ShowItemTooltip("Adds Unity-style material properties as pure scene data.");
            }
            ImGui::EndMenu();
        }

        if (hasCameraEntries && ImGui::BeginMenu("Camera"))
        {
            displayedAny = true;
            ShowItemTooltip("Camera and lens components.");
            if (MenuItemWithTooltip("Camera"))
            {
                registry.emplace<CameraComponent>(context.selectedEntity);
                ImGui::CloseCurrentPopup();
            }
            ShowItemTooltip("Adds a scene camera component.");
            ImGui::EndMenu();
        }

        if (hasAudioEntries && ImGui::BeginMenu("Audio"))
        {
            displayedAny = true;
            ShowItemTooltip("Audio playback and listener components.");
            if (canAddAudioSource && matchesFilter("Audio Source"))
            {
                if (MenuItemWithTooltip("Audio Source"))
                {
                    registry.emplace<AudioSourceComponent>(context.selectedEntity);
                    ImGui::CloseCurrentPopup();
                }
                ShowItemTooltip("Adds an audio source for clip playback on this entity.");
            }
            if (canAddAudioListener && matchesFilter("Audio Listener"))
            {
                if (MenuItemWithTooltip("Audio Listener"))
                {
                    registry.emplace<AudioListenerComponent>(context.selectedEntity);
                    ImGui::CloseCurrentPopup();
                }
                ShowItemTooltip("Adds an audio listener used as the active 3D listener in Play mode.");
            }
            ImGui::EndMenu();
        }

        if (hasScriptingEntries && ImGui::BeginMenu("Scripting"))
        {
            displayedAny = true;
            ShowItemTooltip("Gameplay scripting components.");
            if (MenuItemWithTooltip("Lua Script"))
            {
                registry.emplace<LuaScriptComponent>(context.selectedEntity);
                ImGui::CloseCurrentPopup();
            }
            ShowItemTooltip("Adds a Lua script component to this entity.");
            ImGui::EndMenu();
        }

        if (hasLightingEntries && ImGui::BeginMenu("Lighting"))
        {
            displayedAny = true;
            ShowItemTooltip("Lighting and environment components.");
            if (canAddDirectional && matchesFilter("Directional Light"))
            {
                if (MenuItemWithTooltip("Directional Light"))
                {
                    registry.emplace<DirectionalLightComponent>(context.selectedEntity);
                    ImGui::CloseCurrentPopup();
                }
                ShowItemTooltip("Adds directional sunlight-style lighting.");
            }

            if (canAddPoint && matchesFilter("Point Light"))
            {
                if (MenuItemWithTooltip("Point Light"))
                {
                    registry.emplace<PointLightComponent>(context.selectedEntity);
                    ImGui::CloseCurrentPopup();
                }
                ShowItemTooltip("Adds omnidirectional local lighting.");
            }

            if (canAddSpot && matchesFilter("Spot Light"))
            {
                if (MenuItemWithTooltip("Spot Light"))
                {
                    registry.emplace<SpotLightComponent>(context.selectedEntity);
                    ImGui::CloseCurrentPopup();
                }
                ShowItemTooltip("Adds directional cone lighting.");
            }

            if (canAddSky && matchesFilter("Sky Light"))
            {
                if (MenuItemWithTooltip("Sky Light"))
                {
                    auto& skyLight = registry.emplace<SkyLightComponent>(context.selectedEntity);
                    if (context.initializeSkyLightDefaults)
                    {
                        context.initializeSkyLightDefaults(skyLight);
                    }
                    if (context.markSceneEnvironmentDirty)
                    {
                        context.markSceneEnvironmentDirty();
                    }
                    ImGui::CloseCurrentPopup();
                }
                ShowItemTooltip("Adds ambient sky/environment lighting.");
            }
            ImGui::EndMenu();
        }

        if (hasEffectsEntries && ImGui::BeginMenu("Effects"))
        {
            displayedAny = true;
            ShowItemTooltip("Post-process settings that can be attached to any entity volume.");
            if (canAddPostProcess && matchesFilter("Post Process"))
            {
                if (MenuItemWithTooltip("Post Process"))
                {
                    auto& postProcess = registry.emplace<PostProcessComponent>(context.selectedEntity);
                    if (context.initializePostProcessDefaults)
                    {
                        context.initializePostProcessDefaults(postProcess);
                    }
                    if (context.markSceneEnvironmentDirty)
                    {
                        context.markSceneEnvironmentDirty();
                    }
                    ImGui::CloseCurrentPopup();
                }
                ShowItemTooltip("Adds scene post-process settings to this entity.");
            }
            ImGui::EndMenu();
        }

        if (hasPhysicsEntries && ImGui::BeginMenu("Physics"))
        {
            displayedAny = true;
            ShowItemTooltip("Physics simulation and collision components.");
            const auto ensureJointBase = [&]()
            {
                if (!registry.all_of<JointComponent>(context.selectedEntity))
                {
                    auto& joint = registry.emplace<JointComponent>(context.selectedEntity);
                    joint.active = true;
                }
            };

            const bool hasDynamicsEntries =
                (canAddRigidBody && matchesFilter("Rigid Body")) ||
                (canAddCharacterController && matchesFilter("Character Controller"));
            if (hasDynamicsEntries && ImGui::BeginMenu("Dynamics"))
            {
                ShowItemTooltip("Dynamic simulation components.");
                if (canAddRigidBody && matchesFilter("Rigid Body"))
                {
                    if (MenuItemWithTooltip("Rigid Body"))
                    {
                        auto& rigidBody = registry.emplace<RigidBodyComponent>(context.selectedEntity);
                        rigidBody.bodyType = RigidBodyType::Dynamic;
                        rigidBody.mass = 1.0f;
                        rigidBody.enableGravity = true;
                        rigidBody.active = true;
                        ImGui::CloseCurrentPopup();
                    }
                    ShowItemTooltip("Add a rigid body for physical simulation (mass, forces, velocity).");
                }

                if (canAddCharacterController && matchesFilter("Character Controller"))
                {
                    if (MenuItemWithTooltip("Character Controller"))
                    {
                        auto& controller = registry.emplace<CharacterControllerComponent>(context.selectedEntity);
                        controller.active = true;
                        controller.movementMode = CharacterMovementMode::Walk;
                        ImGui::CloseCurrentPopup();
                    }
                    ShowItemTooltip("Add a character controller capsule for locomotion and grounding.");
                }
                ImGui::EndMenu();
            }

            const bool hasCollisionEntries =
                (canAddCollider && matchesFilter("Collider")) ||
                (canAddWheelCollider && matchesFilter("Wheel Collider"));
            if (hasCollisionEntries && ImGui::BeginMenu("Collision"))
            {
                ShowItemTooltip("Collision shape and wheel collision components.");
                if (canAddCollider && matchesFilter("Collider"))
                {
                    if (MenuItemWithTooltip("Collider"))
                    {
                        if (const MeshRendererComponent* meshRenderer = registry.try_get<MeshRendererComponent>(context.selectedEntity);
                            meshRenderer != nullptr && meshRenderer->usePrimitive)
                        {
                            if (context.ensurePrimitiveCollider)
                            {
                                context.ensurePrimitiveCollider(context.selectedEntity, meshRenderer->primitive);
                            }
                        }
                        else
                        {
                            auto& collider = registry.emplace<ColliderComponent>(context.selectedEntity);
                            collider.shape = ColliderShapeType::Box;
                            collider.boxHalfExtents = { 0.5f, 0.5f, 0.5f };
                            collider.active = true;
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ShowItemTooltip("Add a collision shape used for blocking, overlap, or trigger events.");
                }

                if (canAddWheelCollider && matchesFilter("Wheel Collider"))
                {
                    if (MenuItemWithTooltip("Wheel Collider"))
                    {
                        auto& wheel = registry.emplace<WheelColliderComponent>(context.selectedEntity);
                        wheel.active = true;
                        wheel.steerable = false;
                        wheel.driven = false;
                        wheel.handbrakeAffected = false;
                        wheel.axleType = VehicleAxleType::Front;
                        wheel.suspensionRestLength = wheel.suspensionTravel;
                        wheel.suspensionMaxCompression = wheel.suspensionTravel * 0.5f;
                        wheel.suspensionMaxDroop = wheel.suspensionTravel * 0.5f;
                        ImGui::CloseCurrentPopup();
                    }
                    ShowItemTooltip("Add vehicle wheel collision and suspension settings.");
                }
                ImGui::EndMenu();
            }

            const bool hasJointEntries =
                (canAddJoint && matchesFilter("Joint")) ||
                (canAddFixedJoint && matchesFilter("Fixed Joint")) ||
                (canAddHingeJoint && matchesFilter("Hinge Joint")) ||
                (canAddSliderJoint && matchesFilter("Slider Joint")) ||
                (canAddD6Joint && matchesFilter("D6 Joint"));
            if (hasJointEntries && ImGui::BeginMenu("Joints"))
            {
                ShowItemTooltip("Constraint and joint components.");
                if (canAddJoint && matchesFilter("Joint"))
                {
                    if (MenuItemWithTooltip("Joint (Base)"))
                    {
                        auto& joint = registry.emplace<JointComponent>(context.selectedEntity);
                        joint.active = true;
                        ImGui::CloseCurrentPopup();
                    }
                    ShowItemTooltip("Add shared joint settings used by all specific constraints.");
                }

                if (canAddFixedJoint && matchesFilter("Fixed Joint"))
                {
                    if (MenuItemWithTooltip("Fixed Joint"))
                    {
                        ensureJointBase();
                        registry.emplace<FixedJointComponent>(context.selectedEntity);
                        ImGui::CloseCurrentPopup();
                    }
                    ShowItemTooltip("Lock all relative motion between two connected rigid bodies.");
                }

                if (canAddHingeJoint && matchesFilter("Hinge Joint"))
                {
                    if (MenuItemWithTooltip("Hinge Joint"))
                    {
                        ensureJointBase();
                        registry.emplace<HingeJointComponent>(context.selectedEntity);
                        ImGui::CloseCurrentPopup();
                    }
                    ShowItemTooltip("Revolute joint with a single rotation axis, limits, and motor.");
                }

                if (canAddSliderJoint && matchesFilter("Slider Joint"))
                {
                    if (MenuItemWithTooltip("Slider Joint"))
                    {
                        ensureJointBase();
                        registry.emplace<SliderJointComponent>(context.selectedEntity);
                        ImGui::CloseCurrentPopup();
                    }
                    ShowItemTooltip("Prismatic joint with one linear axis, limits, and motor.");
                }

                if (canAddD6Joint && matchesFilter("D6 Joint"))
                {
                    if (MenuItemWithTooltip("D6 Joint"))
                    {
                        ensureJointBase();
                        registry.emplace<D6JointComponent>(context.selectedEntity);
                        ImGui::CloseCurrentPopup();
                    }
                    ShowItemTooltip("General 6-DOF constraint for locked/limited/free linear and angular motion.");
                }
                ImGui::EndMenu();
            }

            const bool hasVehicleEntries =
                (canAddVehicle && matchesFilter("Vehicle")) ||
                (canAddVehicleInput && matchesFilter("Vehicle Input"));
            if (hasVehicleEntries && ImGui::BeginMenu("Vehicles"))
            {
                ShowItemTooltip("Vehicle simulation components.");
                if (canAddVehicle && matchesFilter("Vehicle") && MenuItemWithTooltip("Vehicle"))
                {
                    auto& vehicle = registry.emplace<VehicleComponent>(context.selectedEntity);
                    vehicle.active = true;
                    vehicle.simulationEnabled = true;
                    vehicle.inputSource = VehicleInputSource::Player;
                    vehicle.inputMap = "Vehicle.Default";
                    ImGui::CloseCurrentPopup();
                }
                if (canAddVehicle && matchesFilter("Vehicle"))
                {
                    ShowItemTooltip("Add high-level chassis and drivetrain settings for vehicle simulation.");
                }

                if (canAddVehicleInput && matchesFilter("Vehicle Input"))
                {
                    if (MenuItemWithTooltip("Vehicle Input"))
                    {
                        auto& input = registry.emplace<VehicleInputComponent>(context.selectedEntity);
                        input.active = true;
                        ImGui::CloseCurrentPopup();
                    }
                    ShowItemTooltip("Add a dedicated runtime input block for throttle, brake, steering, and handbrake.");
                }
                ImGui::EndMenu();
            }

            const bool hasForceEntries =
                (canAddForceField && matchesFilter("Force Field")) ||
                (canAddBuoyancy && matchesFilter("Buoyancy"));
            if (hasForceEntries && ImGui::BeginMenu("Forces"))
            {
                ShowItemTooltip("Force volumes and buoyancy influences.");
                if (canAddForceField && matchesFilter("Force Field"))
                {
                    if (MenuItemWithTooltip("Force Field"))
                    {
                        auto& field = registry.emplace<ForceFieldComponent>(context.selectedEntity);
                        field.active = true;
                        ImGui::CloseCurrentPopup();
                    }
                    ShowItemTooltip("Apply directional or radial force inside a configurable volume.");
                }

                if (canAddBuoyancy && matchesFilter("Buoyancy"))
                {
                    if (MenuItemWithTooltip("Buoyancy"))
                    {
                        auto& buoyancy = registry.emplace<BuoyancyComponent>(context.selectedEntity);
                        buoyancy.active = true;
                        ImGui::CloseCurrentPopup();
                    }
                    ShowItemTooltip("Simulate floating behavior against water level/volume.");
                }
                ImGui::EndMenu();
            }

            if (canAddPhysicsEvents && matchesFilter("Physics Events") && ImGui::BeginMenu("Events"))
            {
                ShowItemTooltip("Collision and trigger event routing.");
                if (MenuItemWithTooltip("Physics Events"))
                {
                    registry.emplace<PhysicsEventsComponent>(context.selectedEntity);
                    ImGui::CloseCurrentPopup();
                }
                ShowItemTooltip("Enable per-entity collision and trigger callbacks.");
                ImGui::EndMenu();
            }

            if (canAddRagdoll && matchesFilter("Ragdoll") && ImGui::BeginMenu("Characters"))
            {
                ShowItemTooltip("Character physics extensions.");
                if (MenuItemWithTooltip("Ragdoll"))
                {
                    auto& ragdoll = registry.emplace<RagdollComponent>(context.selectedEntity);
                    ragdoll.active = true;
                    ImGui::CloseCurrentPopup();
                }
                ShowItemTooltip("Bind skeleton/physics assets for animation-to-physics blending.");
                ImGui::EndMenu();
            }

            if (canAddDestructible && matchesFilter("Destruction") && ImGui::BeginMenu("Destruction"))
            {
                ShowItemTooltip("Destruction and fracture components.");
                if (MenuItemWithTooltip("Destructible"))
                {
                    auto& destructible = registry.emplace<DestructibleComponent>(context.selectedEntity);
                    destructible.active = true;
                    destructible.visibleIntactMesh = true;
                    destructible.fractureOnImpact = true;
                    destructible.accumulateDamage = true;
                    destructible.worldSupport = true;
                    destructible.activationMode = DestructionActivationMode::StartIntact;
                    ImGui::CloseCurrentPopup();
                }
                ShowItemTooltip("Adds Blast-ready destruction settings for fracture, chunk, and damage behavior.");
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (!canAddAny)
        {
            ImGui::TextDisabled("All available components are already added.");
        }
        else if (!displayedAny)
        {
            ImGui::TextDisabled("No components match \"%s\".", componentSearchBuffer.data());
        }

        ImGui::EndPopup();
    }
}
