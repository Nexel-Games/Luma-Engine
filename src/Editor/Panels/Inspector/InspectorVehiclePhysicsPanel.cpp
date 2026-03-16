#include "Luma/Editor/Panels/Inspector/InspectorVehiclePhysicsPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string_view>
#include <utility>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "Luma/Core/App/Project.h"
#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Scene/TagComponent.h"
#include "Luma/Scene/VehicleComponent.h"
#include "Luma/Scene/VehicleInputComponent.h"
#include "Luma/Scene/WheelColliderComponent.h"

namespace Luma::Editor
{
    namespace
    {
        using json = nlohmann::json;

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
        bool DragFloatWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragFloat(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool InputIntWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::InputInt(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }

        template <typename... Args>
        bool InputTextWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::InputText(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
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

        std::string SanitizeAssetName(std::string value)
        {
            for (char& character : value)
            {
                const unsigned char code = static_cast<unsigned char>(character);
                if (std::isalnum(code))
                {
                    character = static_cast<char>(std::tolower(code));
                }
                else
                {
                    character = '_';
                }
            }

            value.erase(
                std::unique(value.begin(), value.end(), [](char lhs, char rhs)
                    {
                        return lhs == '_' && rhs == '_';
                    }),
                value.end());

            while (!value.empty() && value.front() == '_')
            {
                value.erase(value.begin());
            }
            while (!value.empty() && value.back() == '_')
            {
                value.pop_back();
            }

            if (value.empty())
            {
                value = "vehicle_tuning";
            }
            return value;
        }

        json BuildVehicleTuningJson(const VehicleComponent& vehicle)
        {
            return json {
                { "vehicleTuning", {
                    { "engineTorque", vehicle.engineTorque },
                    { "idleRPM", vehicle.idleRPM },
                    { "maxRPM", vehicle.maxRPM },
                    { "reverseGearRatio", vehicle.reverseGearRatio },
                    { "gearRatios", vehicle.gearRatios },
                    { "differentialRatio", vehicle.differentialRatio },
                    { "brakeForce", vehicle.brakeForce },
                    { "handbrakeForce", vehicle.handbrakeForce },
                    { "frontBrakeBias", vehicle.frontBrakeBias },
                    { "frontDriveBias", vehicle.frontDriveBias },
                    { "tireFrictionScale", vehicle.tireFrictionScale },
                    { "suspensionStiffness", vehicle.suspensionStiffness },
                    { "suspensionDamping", vehicle.suspensionDamping },
                    { "suspensionTravel", vehicle.suspensionTravel },
                    { "maxSteerAngleDegrees", vehicle.maxSteerAngleDegrees },
                    { "steerSensitivity", vehicle.steerSensitivity },
                    { "shiftUpRPM", vehicle.shiftUpRPM },
                    { "shiftDownRPM", vehicle.shiftDownRPM },
                    { "automaticTransmission", vehicle.automaticTransmission },
                    { "enableABS", vehicle.enableABS },
                    { "enableTCS", vehicle.enableTCS },
                    { "ackermannSteering", vehicle.ackermannSteering }
                } }
            };
        }

        bool SaveVehicleTuningAsset(const std::filesystem::path& assetPath, const VehicleComponent& vehicle)
        {
            if (assetPath.empty())
            {
                return false;
            }

            std::error_code errorCode;
            std::filesystem::create_directories(assetPath.parent_path(), errorCode);
            if (errorCode)
            {
                return false;
            }

            std::ofstream output(assetPath, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                return false;
            }

            output << BuildVehicleTuningJson(vehicle).dump(2);
            return output.good();
        }

        std::filesystem::path ResolveVehicleTuningAssetPath(const std::string& assetPath)
        {
            if (assetPath.empty())
            {
                return {};
            }

            std::error_code errorCode;
            const auto tryPath = [&](const std::filesystem::path& candidate) -> std::filesystem::path
            {
                if (candidate.empty())
                {
                    return {};
                }
                if (std::filesystem::exists(candidate, errorCode) && !errorCode)
                {
                    return std::filesystem::weakly_canonical(candidate, errorCode);
                }
                errorCode.clear();
                return {};
            };

            std::filesystem::path path(assetPath);
            if (path.is_absolute())
            {
                return tryPath(path);
            }

            if (Project::IsLoaded())
            {
                if (std::filesystem::path resolved = tryPath(Project::GetAssetsPath() / path); !resolved.empty())
                {
                    return resolved;
                }
                if (std::filesystem::path resolved = tryPath(Project::GetProjectRoot() / path); !resolved.empty())
                {
                    return resolved;
                }
            }

            return tryPath(std::filesystem::current_path() / path);
        }

        std::vector<std::filesystem::path> GatherVehicleTuningAssets()
        {
            std::vector<std::filesystem::path> assets;
            if (!Project::IsLoaded())
            {
                return assets;
            }

            std::error_code errorCode;
            const std::filesystem::path assetsRoot = Project::GetAssetsPath();
            if (!std::filesystem::exists(assetsRoot, errorCode) || errorCode)
            {
                return assets;
            }

            for (std::filesystem::recursive_directory_iterator it(assetsRoot, errorCode), end; it != end && !errorCode; it.increment(errorCode))
            {
                if (!it->is_regular_file())
                {
                    continue;
                }

                const std::filesystem::path path = it->path();
                if (path.extension() == ".lumatune")
                {
                    assets.push_back(path);
                }
            }

            std::sort(assets.begin(), assets.end());
            return assets;
        }

        std::string MakeRelativeTuningAssetPath(const std::filesystem::path& assetPath)
        {
            if (assetPath.empty())
            {
                return {};
            }

            if (Project::IsLoaded())
            {
                std::error_code errorCode;
                const std::filesystem::path relativePath = std::filesystem::relative(assetPath, Project::GetAssetsPath(), errorCode);
                if (!errorCode && !relativePath.empty())
                {
                    return relativePath.generic_string();
                }
            }
            return assetPath.generic_string();
        }

        std::filesystem::path BuildDefaultVehicleTuningAssetPath(entt::registry& registry, const EntityID entity)
        {
            std::string baseName = "vehicle_tuning";
            if (const auto* tag = registry.try_get<TagComponent>(entity); tag != nullptr && !tag->name.empty())
            {
                baseName = SanitizeAssetName(tag->name);
            }

            std::filesystem::path basePath = Project::IsLoaded()
                ? (Project::GetAssetsPath() / "VehicleTunings")
                : (std::filesystem::current_path() / "VehicleTunings");
            std::filesystem::path candidate = basePath / (baseName + ".lumatune");

            std::error_code errorCode;
            int suffix = 1;
            while (std::filesystem::exists(candidate, errorCode) && !errorCode)
            {
                candidate = basePath / (baseName + "_" + std::to_string(suffix++) + ".lumatune");
            }
            return candidate;
        }

        const char* AxleTypeLabel(const VehicleAxleType axleType)
        {
            switch (axleType)
            {
            case VehicleAxleType::Front:
                return "Front";
            case VehicleAxleType::Rear:
                return "Rear";
            case VehicleAxleType::Custom:
            default:
                return "Custom";
            }
        }

        const char* VehicleTypeLabel(const VehicleType vehicleType)
        {
            switch (vehicleType)
            {
            case VehicleType::Truck:
                return "Truck";
            case VehicleType::Bike:
                return "Bike";
            case VehicleType::Car:
            default:
                return "Car";
            }
        }

        const char* VehicleInputSourceLabel(const VehicleInputSource inputSource)
        {
            switch (inputSource)
            {
            case VehicleInputSource::AI:
                return "AI";
            case VehicleInputSource::Script:
                return "Script";
            case VehicleInputSource::Player:
            default:
                return "Player";
            }
        }
    }

    void InspectorVehiclePhysicsPanel::Draw(const InspectorVehiclePhysicsPanelContext& context)
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

        if (registry.all_of<WheelColliderComponent>(context.selectedEntity))
        {
            auto& wheel = registry.get<WheelColliderComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Wheel Collider", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Wheel contact, tire friction, and suspension settings.");
                ImGui::PushID("WheelColliderComponent");
                CheckboxWithTooltip("Active", &wheel.active);
                DragFloatWithTooltip("Radius", &wheel.radius, 0.001f, 0.01f, 100.0f);
                DragFloatWithTooltip("Width", &wheel.width, 0.001f, 0.01f, 100.0f);
                DragFloatWithTooltip("Wheel Mass", &wheel.wheelMass, 0.1f, 0.01f, 10000.0f);
                DragFloatWithTooltip("Suspension Rest Length", &wheel.suspensionRestLength, 0.001f, 0.0f, 10.0f);
                DragFloatWithTooltip("Suspension Max Compression", &wheel.suspensionMaxCompression, 0.001f, 0.0f, 10.0f);
                DragFloatWithTooltip("Suspension Max Droop", &wheel.suspensionMaxDroop, 0.001f, 0.0f, 10.0f);
                DragFloatWithTooltip("Suspension Stiffness", &wheel.suspensionStiffness, 1.0f, 0.0f, 1000000.0f);
                DragFloatWithTooltip("Suspension Damping", &wheel.suspensionDamping, 1.0f, 0.0f, 1000000.0f);
                DragFloatWithTooltip("Suspension Travel", &wheel.suspensionTravel, 0.001f, 0.0f, 100.0f);
                DragFloatWithTooltip("Tire Friction", &wheel.tireFriction, 0.01f, 0.0f, 100.0f);
                DragFloatWithTooltip("Tire Friction Scale", &wheel.tireFrictionScale, 0.01f, 0.0f, 100.0f);
                CheckboxWithTooltip("Steerable", &wheel.steerable);
                CheckboxWithTooltip("Driven", &wheel.driven);
                CheckboxWithTooltip("Handbrake Affected", &wheel.handbrakeAffected);
                int axleTypeIndex = static_cast<int>(wheel.axleType);
                const char* axleTypeItems[] = { "Front", "Rear", "Custom" };
                if (ImGui::Combo("Axle Type", &axleTypeIndex, axleTypeItems, IM_ARRAYSIZE(axleTypeItems)))
                {
                    axleTypeIndex = std::clamp(axleTypeIndex, 0, 2);
                    wheel.axleType = static_cast<VehicleAxleType>(axleTypeIndex);
                }
                ShowItemTooltip("Classify the wheel as front, rear, or custom.");
                DrawUUIDField("Visual Wheel Entity", wheel.visualWheelEntity, "Optional visual wheel entity to update from suspension and spin.");
                ImGui::DragFloat3("Suspension Attach Point", wheel.suspensionAttachPoint.data(), 0.01f);
                ShowItemTooltip("Local wheel attach point on the chassis.");
                ImGui::DragFloat3("Wheel Rotation Axis", wheel.wheelRotationAxis.data(), 0.01f);
                ShowItemTooltip("Local axis used for wheel spin visuals.");
                ImGui::DragFloat3("Suspension Axis", wheel.suspensionAxis.data(), 0.01f);
                ShowItemTooltip("Local axis used for suspension raycast direction.");
                ShowItemTooltip("Tune tire grip and spring-damper response.");
                wheel.radius = std::max(0.01f, wheel.radius);
                wheel.width = std::max(0.01f, wheel.width);
                wheel.wheelMass = std::max(0.01f, wheel.wheelMass);
                wheel.suspensionRestLength = std::max(0.0f, wheel.suspensionRestLength);
                wheel.suspensionMaxCompression = std::max(0.0f, wheel.suspensionMaxCompression);
                wheel.suspensionMaxDroop = std::max(0.0f, wheel.suspensionMaxDroop);
                wheel.suspensionStiffness = std::max(0.0f, wheel.suspensionStiffness);
                wheel.suspensionDamping = std::max(0.0f, wheel.suspensionDamping);
                wheel.suspensionTravel = std::max(0.0f, wheel.suspensionTravel);
                wheel.tireFriction = std::max(0.0f, wheel.tireFriction);
                wheel.tireFrictionScale = std::max(0.0f, wheel.tireFrictionScale);
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Wheel Collider Component"))
            {
                registry.remove<WheelColliderComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove wheel-collider behavior from this entity.");
        }

        if (registry.all_of<VehicleComponent>(context.selectedEntity))
        {
            auto& vehicle = registry.get<VehicleComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Vehicle", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Vehicle chassis, drivetrain, steering, and wheel setup.");
                ImGui::PushID("VehicleComponent");
                CheckboxWithTooltip("Active", &vehicle.active);
                CheckboxWithTooltip("Simulation Enabled", &vehicle.simulationEnabled);
                int vehicleTypeIndex = static_cast<int>(vehicle.vehicleType);
                const char* vehicleTypeItems[] = { "Car", "Truck", "Bike" };
                if (ImGui::Combo("Vehicle Type", &vehicleTypeIndex, vehicleTypeItems, IM_ARRAYSIZE(vehicleTypeItems)))
                {
                    vehicleTypeIndex = std::clamp(vehicleTypeIndex, 0, 2);
                    vehicle.vehicleType = static_cast<VehicleType>(vehicleTypeIndex);
                }
                ShowItemTooltip("Vehicle simulation archetype.");
                int inputSourceIndex = static_cast<int>(vehicle.inputSource);
                const char* inputSourceItems[] = { "Player", "AI", "Script" };
                if (ImGui::Combo("Input Source", &inputSourceIndex, inputSourceItems, IM_ARRAYSIZE(inputSourceItems)))
                {
                    inputSourceIndex = std::clamp(inputSourceIndex, 0, 2);
                    vehicle.inputSource = static_cast<VehicleInputSource>(inputSourceIndex);
                }
                ShowItemTooltip("Choose whether this vehicle reads player, AI, or scripted input.");
                CheckboxWithTooltip("Use COM Override", &vehicle.useCenterOfMassOverride);
                ImGui::DragFloat3("Center Of Mass Offset", vehicle.centerOfMassOffset.data(), 0.01f);
                ShowItemTooltip("Optional center-of-mass offset relative to the chassis rigid body.");
                DrawUUIDField("Chassis Rigidbody UUID", vehicle.chassisRigidBody, "Entity UUID for vehicle chassis rigid body.");

                int wheelCount = static_cast<int>(vehicle.wheelEntities.size());
                if (InputIntWithTooltip("Wheel Count", &wheelCount))
                {
                    wheelCount = std::clamp(wheelCount, 0, 32);
                    vehicle.wheelEntities.resize(static_cast<std::size_t>(wheelCount), 0);
                }
                ShowItemTooltip("Number of wheel entity UUID references.");
                for (std::size_t index = 0; index < vehicle.wheelEntities.size(); ++index)
                {
                    std::array<char, 48> label {};
                    std::snprintf(label.data(), label.size(), "Wheel %u UUID", static_cast<unsigned int>(index));
                    DrawUUIDField(label.data(), vehicle.wheelEntities[index], "Entity UUID for this wheel.");
                }

                DragFloatWithTooltip("Engine Torque", &vehicle.engineTorque, 1.0f, 0.0f, 1000000.0f);
                DragFloatWithTooltip("Idle RPM", &vehicle.idleRPM, 1.0f, 100.0f, 100000.0f);
                DragFloatWithTooltip("Max RPM", &vehicle.maxRPM, 1.0f, 1.0f, 100000.0f);
                DragFloatWithTooltip("Reverse Gear Ratio", &vehicle.reverseGearRatio, 0.01f, 0.01f, 100.0f);
                DragFloatWithTooltip("Differential Ratio", &vehicle.differentialRatio, 0.01f, 0.01f, 100.0f);
                DragFloatWithTooltip("Brake Force", &vehicle.brakeForce, 1.0f, 0.0f, 1000000.0f);
                DragFloatWithTooltip("Handbrake Force", &vehicle.handbrakeForce, 1.0f, 0.0f, 1000000.0f);
                DragFloatWithTooltip("Front Brake Bias", &vehicle.frontBrakeBias, 0.01f, 0.0f, 1.0f);
                DragFloatWithTooltip("Front Drive Bias", &vehicle.frontDriveBias, 0.01f, 0.0f, 1.0f);
                DragFloatWithTooltip("Tire Friction Scale", &vehicle.tireFrictionScale, 0.01f, 0.0f, 100.0f);
                ShowItemTooltip("Powertrain and tire grip scaling.");

                int gearCount = static_cast<int>(vehicle.gearRatios.size());
                if (InputIntWithTooltip("Forward Gear Count", &gearCount))
                {
                    gearCount = std::clamp(gearCount, 1, 8);
                    vehicle.gearRatios.resize(static_cast<std::size_t>(gearCount), 1.0f);
                }
                ShowItemTooltip("Number of forward gears available for the automatic/manual drivetrain.");
                for (std::size_t gearIndex = 0; gearIndex < vehicle.gearRatios.size(); ++gearIndex)
                {
                    std::array<char, 48> label {};
                    std::snprintf(label.data(), label.size(), "Gear %u Ratio", static_cast<unsigned int>(gearIndex + 1u));
                    DragFloatWithTooltip(label.data(), &vehicle.gearRatios[gearIndex], 0.01f, 0.01f, 100.0f);
                }

                DragFloatWithTooltip("Suspension Stiffness", &vehicle.suspensionStiffness, 1.0f, 0.0f, 1000000.0f);
                DragFloatWithTooltip("Suspension Damping", &vehicle.suspensionDamping, 1.0f, 0.0f, 1000000.0f);
                DragFloatWithTooltip("Suspension Travel", &vehicle.suspensionTravel, 0.001f, 0.0f, 100.0f);
                DragFloatWithTooltip("Drag Coefficient", &vehicle.dragCoefficient, 0.001f, 0.0f, 100.0f);
                DragFloatWithTooltip("Rolling Resistance", &vehicle.rollingResistance, 0.01f, 0.0f, 1000.0f);
                DragFloatWithTooltip("Aero Downforce", &vehicle.aeroDownforce, 0.1f, 0.0f, 100000.0f);

                DragFloatWithTooltip("Max Steer Angle (deg)", &vehicle.maxSteerAngleDegrees, 0.1f, 0.0f, 90.0f);
                DragFloatWithTooltip("Steer Sensitivity", &vehicle.steerSensitivity, 0.01f, 0.0f, 10.0f);
                DragFloatWithTooltip("Shift Up RPM", &vehicle.shiftUpRPM, 1.0f, 100.0f, 100000.0f);
                DragFloatWithTooltip("Shift Down RPM", &vehicle.shiftDownRPM, 1.0f, 100.0f, 100000.0f);

                CheckboxWithTooltip("Automatic Transmission", &vehicle.automaticTransmission);
                CheckboxWithTooltip("ABS", &vehicle.enableABS);
                CheckboxWithTooltip("TCS", &vehicle.enableTCS);
                CheckboxWithTooltip("Ackermann Steering", &vehicle.ackermannSteering);
                CheckboxWithTooltip("Auto Flip", &vehicle.autoFlip);
                CheckboxWithTooltip("Use Substepping", &vehicle.useSubstepping);
                CheckboxWithTooltip("Sleep When Inactive", &vehicle.sleepWhenInactive);
                ShowItemTooltip("Enable anti-lock braking and traction control toggles.");

                vehicle.idleRPM = std::max(100.0f, vehicle.idleRPM);
                vehicle.maxRPM = std::max(vehicle.idleRPM + 100.0f, vehicle.maxRPM);
                vehicle.reverseGearRatio = std::max(0.01f, vehicle.reverseGearRatio);
                vehicle.differentialRatio = std::max(0.01f, vehicle.differentialRatio);
                vehicle.frontBrakeBias = std::clamp(vehicle.frontBrakeBias, 0.0f, 1.0f);
                vehicle.frontDriveBias = std::clamp(vehicle.frontDriveBias, 0.0f, 1.0f);
                vehicle.shiftDownRPM = std::clamp(vehicle.shiftDownRPM, vehicle.idleRPM, vehicle.maxRPM);
                vehicle.shiftUpRPM = std::clamp(vehicle.shiftUpRPM, vehicle.shiftDownRPM, vehicle.maxRPM);
                for (float& gearRatio : vehicle.gearRatios)
                {
                    gearRatio = std::max(0.01f, gearRatio);
                }

                std::array<char, 128> inputMapBuffer {};
                std::snprintf(inputMapBuffer.data(), inputMapBuffer.size(), "%s", vehicle.inputMap.c_str());
                if (InputTextWithTooltip("Input Map", inputMapBuffer.data(), inputMapBuffer.size()))
                {
                    vehicle.inputMap = inputMapBuffer.data();
                }
                ShowItemTooltip("Input action-map name used for vehicle controls.");
                std::array<char, 128> tuningAssetBuffer {};
                std::snprintf(tuningAssetBuffer.data(), tuningAssetBuffer.size(), "%s", vehicle.tuningAsset.c_str());
                if (InputTextWithTooltip("Tuning Asset", tuningAssetBuffer.data(), tuningAssetBuffer.size()))
                {
                    vehicle.tuningAsset = tuningAssetBuffer.data();
                }
                ShowItemTooltip("Optional tuning asset reference for shared vehicle setup.");

                const std::filesystem::path resolvedTuningAssetPath = ResolveVehicleTuningAssetPath(vehicle.tuningAsset);
                const bool hasTuningAsset = !vehicle.tuningAsset.empty();
                const bool tuningAssetExists = !resolvedTuningAssetPath.empty();
                if (hasTuningAsset)
                {
                    ImGui::TextDisabled(
                        "%s",
                        tuningAssetExists ? resolvedTuningAssetPath.generic_string().c_str() : "Tuning asset not found");
                }

                if (ButtonWithTooltip("Pick Tuning Asset"))
                {
                    ImGui::OpenPopup("VehicleTuningAssetPicker");
                }
                ShowItemTooltip("Pick a reusable .lumatune asset from the project.");
                ImGui::SameLine();
                if (ButtonWithTooltip("New Tuning Asset"))
                {
                    const std::filesystem::path newAssetPath = BuildDefaultVehicleTuningAssetPath(registry, context.selectedEntity);
                    if (SaveVehicleTuningAsset(newAssetPath, vehicle))
                    {
                        vehicle.tuningAsset = MakeRelativeTuningAssetPath(newAssetPath);
                    }
                }
                ShowItemTooltip("Create a new .lumatune asset from the current vehicle settings.");
                ImGui::SameLine();
                if (ButtonWithTooltip("Save To Tuning Asset"))
                {
                    if (tuningAssetExists)
                    {
                        SaveVehicleTuningAsset(resolvedTuningAssetPath, vehicle);
                    }
                }
                ShowItemTooltip("Write the current vehicle tuning values into the selected asset.");
                ImGui::SameLine();
                if (ButtonWithTooltip("Clear Tuning Asset"))
                {
                    vehicle.tuningAsset.clear();
                }
                ShowItemTooltip("Stop using a shared tuning asset for this vehicle.");

                if (ImGui::BeginPopup("VehicleTuningAssetPicker"))
                {
                    ImGui::TextUnformatted("Vehicle Tuning Assets");
                    ImGui::Separator();
                    const std::vector<std::filesystem::path> tuningAssets = GatherVehicleTuningAssets();
                    if (tuningAssets.empty())
                    {
                        ImGui::TextDisabled("No .lumatune assets found under Assets/");
                    }
                    else
                    {
                        for (const std::filesystem::path& tuningAssetPath : tuningAssets)
                        {
                            const std::string relativePath = MakeRelativeTuningAssetPath(tuningAssetPath);
                            if (ImGui::Selectable(relativePath.c_str(), vehicle.tuningAsset == relativePath))
                            {
                                vehicle.tuningAsset = relativePath;
                                ImGui::CloseCurrentPopup();
                            }
                        }
                    }
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Vehicle Component"))
            {
                registry.remove<VehicleComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove vehicle behavior from this entity.");
        }

        if (registry.all_of<VehicleInputComponent>(context.selectedEntity))
        {
            auto& input = registry.get<VehicleInputComponent>(context.selectedEntity);
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Vehicle Input", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Runtime input values written by player, AI, or script systems.");
                ImGui::PushID("VehicleInputComponent");
                CheckboxWithTooltip("Active", &input.active);
                DragFloatWithTooltip("Throttle", &input.throttle, 0.01f, -1.0f, 1.0f);
                DragFloatWithTooltip("Brake", &input.brake, 0.01f, 0.0f, 1.0f);
                DragFloatWithTooltip("Steering", &input.steering, 0.01f, -1.0f, 1.0f);
                DragFloatWithTooltip("Handbrake", &input.handbrake, 0.01f, 0.0f, 1.0f);
                DragFloatWithTooltip("Clutch", &input.clutch, 0.01f, 0.0f, 1.0f);
                CheckboxWithTooltip("Gear Up Requested", &input.gearUpRequested);
                CheckboxWithTooltip("Gear Down Requested", &input.gearDownRequested);
                CheckboxWithTooltip("Reset Requested", &input.resetRequested);
                input.throttle = std::clamp(input.throttle, -1.0f, 1.0f);
                input.brake = std::clamp(input.brake, 0.0f, 1.0f);
                input.steering = std::clamp(input.steering, -1.0f, 1.0f);
                input.handbrake = std::clamp(input.handbrake, 0.0f, 1.0f);
                input.clutch = std::clamp(input.clutch, 0.0f, 1.0f);
                ShowItemTooltip("Gear up/down are one-shot requests consumed by the drivetrain at runtime.");
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Vehicle Input Component"))
            {
                registry.remove<VehicleInputComponent>(context.selectedEntity);
            }
            ShowItemTooltip("Remove dedicated vehicle input state from this entity.");
        }
    }
}
