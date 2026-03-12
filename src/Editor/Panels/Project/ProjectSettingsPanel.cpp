#include "Luma/Editor/Panels/Project/ProjectSettingsPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <utility>
#include <vector>

#include <imgui.h>

#include "Luma/Core/App/Project.h"
#include "Luma/Core/App/Application.h"
#include "Luma/Editor/Core/GameplayInputBindingService.h"
#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Input/Input.h"
#include "Luma/Scene/VehicleInputActions.h"

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
        bool ComboWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::Combo(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Select ");
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
        bool SelectableWithTooltip(const char* label, Args&&... args)
        {
            const bool selected = ImGui::Selectable(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return selected;
        }

        const char* KeyCodeLabel(const KeyCode key)
        {
            switch (key)
            {
            case KeyCode::Space:
                return "Space";
            case KeyCode::C:
                return "C";
            case KeyCode::R:
                return "R";
            case KeyCode::W:
                return "W";
            case KeyCode::A:
                return "A";
            case KeyCode::S:
                return "S";
            case KeyCode::D:
                return "D";
            case KeyCode::Q:
                return "Q";
            case KeyCode::E:
                return "E";
            case KeyCode::LeftShift:
                return "Left Shift";
            case KeyCode::RightShift:
                return "Right Shift";
            case KeyCode::Enter:
                return "Enter";
            case KeyCode::Escape:
                return "Escape";
            case KeyCode::Unknown:
            case KeyCode::Count:
            default:
                return "Unknown Key";
            }
        }

        const char* MouseButtonLabel(const MouseButton button)
        {
            switch (button)
            {
            case MouseButton::Left:
                return "Mouse Left";
            case MouseButton::Right:
                return "Mouse Right";
            case MouseButton::Middle:
                return "Mouse Middle";
            case MouseButton::Button4:
                return "Mouse Button4";
            case MouseButton::Button5:
                return "Mouse Button5";
            case MouseButton::Count:
            default:
                return "Mouse Unknown";
            }
        }

        const char* MouseAxisLabel(const MouseAxis axis)
        {
            switch (axis)
            {
            case MouseAxis::DeltaX:
                return "Mouse Delta X";
            case MouseAxis::DeltaY:
                return "Mouse Delta Y";
            case MouseAxis::WheelX:
                return "Mouse Wheel X";
            case MouseAxis::WheelY:
                return "Mouse Wheel Y";
            default:
                return "Mouse Axis";
            }
        }

        const char* TriggerLabel(const InputTrigger trigger)
        {
            switch (trigger)
            {
            case InputTrigger::Down:
                return "Down";
            case InputTrigger::Pressed:
                return "Pressed";
            case InputTrigger::Released:
                return "Released";
            default:
                return "Down";
            }
        }

        std::string BindingToLabel(const InputBinding& binding)
        {
            std::string label;
            if (binding.control == InputControlType::Axis1D)
            {
                if (binding.device == InputDeviceType::Mouse)
                {
                    if (binding.code >= 0 && binding.code <= static_cast<int>(MouseAxis::WheelY))
                    {
                        label = MouseAxisLabel(static_cast<MouseAxis>(binding.code));
                    }
                    else
                    {
                        label = "Mouse Axis(" + std::to_string(binding.code) + ")";
                    }
                }
                else
                {
                    label = "Axis(" + std::to_string(binding.code) + ")";
                }
            }
            else
            {
                if (binding.device == InputDeviceType::Keyboard)
                {
                    if (!binding.useRawCode && binding.code > static_cast<int>(KeyCode::Unknown) &&
                        binding.code < static_cast<int>(KeyCode::Count))
                    {
                        label = KeyCodeLabel(static_cast<KeyCode>(binding.code));
                    }
                    else
                    {
                        label = "Key(" + std::to_string(binding.code) + ")";
                    }
                }
                else if (binding.device == InputDeviceType::Mouse)
                {
                    if (!binding.useRawCode && binding.code >= 0 && binding.code < static_cast<int>(MouseButton::Count))
                    {
                        label = MouseButtonLabel(static_cast<MouseButton>(binding.code));
                    }
                    else
                    {
                        label = "Mouse(" + std::to_string(binding.code) + ")";
                    }
                }
                else
                {
                    label = "Button(" + std::to_string(binding.code) + ")";
                }

                if (binding.trigger != InputTrigger::Down)
                {
                    label += " [";
                    label += TriggerLabel(binding.trigger);
                    label += "]";
                }
            }

            if (std::abs(binding.scale - 1.0f) > 1.0e-4f)
            {
                label += " x";
                std::ostringstream scale;
                scale.precision(2);
                scale << std::fixed << binding.scale;
                label += scale.str();
            }

            return label;
        }

        bool HasVehicleBindings(const std::string_view contextName)
        {
            return !Input::GetActionBindings(contextName, VehicleInputActions::Throttle).empty() ||
                !Input::GetActionBindings(contextName, VehicleInputActions::Steer).empty() ||
                !Input::GetActionBindings(contextName, VehicleInputActions::Brake).empty() ||
                !Input::GetActionBindings(contextName, VehicleInputActions::Handbrake).empty() ||
                !Input::GetActionBindings(contextName, VehicleInputActions::Reset).empty();
        }
    }

    void ProjectSettingsPanel::Draw(bool* open, const ProjectSettingsPanelContext& context)
    {
        ImGui::SetNextWindowSize(ImVec2(920.0f, 620.0f), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Project Settings", open))
        {
            ImGui::End();
            return;
        }

        constexpr float navWidth = 200.0f;
        ImGui::BeginChild("ProjectSettingsSections", ImVec2(navWidth, 0.0f), true, ImGuiWindowFlags_NoScrollbar);
        ImGui::TextUnformatted("Sections");
        ImGui::Separator();

        if (SelectableWithTooltip("Project", m_SectionIndex == 0))
        {
            m_SectionIndex = 0;
        }
        ShowItemTooltip("Project-wide configuration: metadata, rendering profile, and build profiles.");

        if (SelectableWithTooltip("Layers", m_SectionIndex == 1))
        {
            m_SectionIndex = 1;
        }
        ShowItemTooltip("Manage project-wide entity tags used by the inspector.");

        if (SelectableWithTooltip("Input System", m_SectionIndex == 2))
        {
            m_SectionIndex = 2;
        }
        ShowItemTooltip("Gameplay action bindings and input rebind controls.");
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("ProjectSettingsContent", ImVec2(0.0f, 0.0f), false);
        if (m_SectionIndex == 0)
        {
            DrawProjectSection(context);
        }
        else if (m_SectionIndex == 1)
        {
            DrawLayersSection(context);
        }
        else
        {
            DrawInputSystemSection(context);
        }
        ImGui::EndChild();

        ImGui::End();
    }

    void ProjectSettingsPanel::Reset()
    {
        m_DraftInitialized = false;
        m_SectionIndex = 0;
        m_Draft = {};
        ClearInputCapture();
        m_NewTagName.clear();
    }

    void ProjectSettingsPanel::InvalidateDraft()
    {
        m_DraftInitialized = false;
    }

    void ProjectSettingsPanel::CancelInputCapture()
    {
        ClearInputCapture();
    }

    void ProjectSettingsPanel::DrawProjectSection(const ProjectSettingsPanelContext& context)
    {
        if (!Project::IsLoaded())
        {
            ImGui::TextDisabled("No loaded project.");
            return;
        }

        SyncDraftFromLoaded();

        auto drawStringField = [](const char* label, std::string& value, const std::size_t capacity = 512)
        {
            std::vector<char> buffer(capacity, '\0');
            std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
            if (InputTextWithTooltip(label, buffer.data(), buffer.size()))
            {
                value = buffer.data();
            }
            ShowItemTooltipFromLabel(label, "Edit ");
        };

        drawStringField("Project Name", m_Draft.name, 256);
        drawStringField("Template", m_Draft.templateName, 256);
        drawStringField("Project Version", m_Draft.projectVersion, 128);
        drawStringField("Engine Version", m_Draft.engineVersion, 128);
        drawStringField("Start Scene", m_Draft.startScene, 512);

        ImGui::Separator();
        ImGui::TextUnformatted("Rendering");
        constexpr const char* pipelineOptions[] = { "CoreLite", "CoreX" };
        int pipelineIndex = m_Draft.pipeline == RenderPipelineProfile::CoreX ? 1 : 0;
        if (ComboWithTooltip("Pipeline", &pipelineIndex, pipelineOptions, IM_ARRAYSIZE(pipelineOptions)))
        {
            m_Draft.pipeline = pipelineIndex == 1 ? RenderPipelineProfile::CoreX : RenderPipelineProfile::CoreLite;
        }
        ShowItemTooltip("Select render pipeline profile for this project.");

        m_Draft.backend = BackendPreference::OpenGL;
        ImGui::Text("Backend: %s", ToString(m_Draft.backend).data());
        ShowItemTooltip("Alpha builds ship OpenGL only. The renderer interfaces remain backend-agnostic for future implementations.");
        CheckboxWithTooltip("VSync", &m_Draft.vsync);
        ShowItemTooltip("Enable or disable vertical sync by default.");

        ImGui::Separator();
        ImGui::TextUnformatted("Build Profiles");
        constexpr const char* profileOptions[] = { "Debug", "Development", "Release" };
        int activeProfileIndex = 1;
        if (m_Draft.build.activeProfile == BuildProfile::Debug)
        {
            activeProfileIndex = 0;
        }
        else if (m_Draft.build.activeProfile == BuildProfile::Release)
        {
            activeProfileIndex = 2;
        }
        if (ComboWithTooltip("Active Profile", &activeProfileIndex, profileOptions, IM_ARRAYSIZE(profileOptions)))
        {
            if (activeProfileIndex == 0)
            {
                m_Draft.build.activeProfile = BuildProfile::Debug;
            }
            else if (activeProfileIndex == 2)
            {
                m_Draft.build.activeProfile = BuildProfile::Release;
            }
            else
            {
                m_Draft.build.activeProfile = BuildProfile::Development;
            }
        }
        ShowItemTooltip("Choose the active build profile.");

        DrawBuildProfileEditor("Debug", m_Draft.build.debug, "Debug");
        DrawBuildProfileEditor("Development", m_Draft.build.development, "Development");
        DrawBuildProfileEditor("Release", m_Draft.build.release, "Release");

        ImGui::Spacing();
        if (ButtonWithTooltip("Save Project Settings"))
        {
            if (Project::UpdateSettings(m_Draft, true))
            {
                if (Application* app = Application::Get(); app != nullptr)
                {
                    app->GetRenderer().SetVSyncEnabled(Project::GetConfig().vsync);
                }
                m_DraftInitialized = false;
                if (context.setProjectConfigStatus)
                {
                    context.setProjectConfigStatus("Project settings saved.");
                }
                if (context.onProjectConfigSaved)
                {
                    context.onProjectConfigSaved();
                }
            }
            else
            {
                if (context.setProjectConfigStatus)
                {
                    context.setProjectConfigStatus("Failed to save project settings.");
                }
            }
        }
        ShowItemTooltip("Save current project settings to disk.");

        ImGui::SameLine();
        if (ButtonWithTooltip("Reload"))
        {
            m_DraftInitialized = false;
            SyncDraftFromLoaded();
            if (context.setProjectConfigStatus)
            {
                context.setProjectConfigStatus("Project settings reloaded.");
            }
        }
        ShowItemTooltip("Reload project settings from disk and discard unsaved changes.");

        const std::string_view configStatus = context.getProjectConfigStatus ? context.getProjectConfigStatus() : std::string_view {};
        if (!configStatus.empty())
        {
            ImGui::TextDisabled("%.*s", static_cast<int>(configStatus.size()), configStatus.data());
        }
    }

    void ProjectSettingsPanel::DrawInputSystemSection(const ProjectSettingsPanelContext& context)
    {
        const bool keyboardAvailable = Input::IsDeviceAvailable(InputDeviceType::Keyboard);
        const bool mouseAvailable = Input::IsDeviceAvailable(InputDeviceType::Mouse);

        ImGui::TextUnformatted("Gameplay Input");
        ImGui::Separator();
        ImGui::Text("Keyboard: %s", keyboardAvailable ? "Available" : "Unavailable");
        ImGui::SameLine();
        ImGui::Text("Mouse: %s", mouseAvailable ? "Available" : "Unavailable");

        if (ButtonWithTooltip("Reset Gameplay Defaults"))
        {
            if (context.resetGameplayInputBindings)
            {
                context.resetGameplayInputBindings();
                if (context.setProjectInputStatus)
                {
                    context.setProjectInputStatus("Gameplay input bindings reset to defaults.");
                }
            }
            else
            {
                if (context.setProjectInputStatus)
                {
                    context.setProjectInputStatus("Gameplay input reset is unavailable.");
                }
            }
        }
        ShowItemTooltip("Restore gameplay input bindings to default mappings.");

        const std::string_view inputStatus = context.getProjectInputStatus ? context.getProjectInputStatus() : std::string_view {};
        if (!inputStatus.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("%.*s", static_cast<int>(inputStatus.size()), inputStatus.data());
        }

        ImGui::Spacing();

        std::vector<std::string> vehicleContexts;
        for (const InputContextDesc& registeredContext : Input::GetRegisteredContexts())
        {
            if (registeredContext.name == VehicleInputActions::Context || HasVehicleBindings(registeredContext.name))
            {
                vehicleContexts.push_back(registeredContext.name);
            }
        }
        if (vehicleContexts.empty())
        {
            vehicleContexts.push_back(std::string(VehicleInputActions::Context));
        }
        if (std::find(vehicleContexts.begin(), vehicleContexts.end(), m_SelectedVehicleInputMap) == vehicleContexts.end())
        {
            m_SelectedVehicleInputMap = vehicleContexts.front();
        }

        if (!m_InputCapture.action.empty() && !m_InputCapture.context.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.88f, 0.72f, 0.30f, 1.0f));
            ImGui::Text("Listening for new binding... Action: %s", m_InputCapture.action.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ButtonWithTooltip("Cancel Rebind"))
            {
                ClearInputCapture();
            }
            ShowItemTooltip("Stop listening for a new input binding.");

            InputBinding capturedBinding {};
            if (Input::PollNextBinding(capturedBinding, m_InputCapture.allowAxes))
            {
                bool applied = false;
                if (m_InputCapture.isAppend)
                {
                    applied = Input::BindAction(m_InputCapture.context, m_InputCapture.action, capturedBinding);
                }
                else
                {
                    applied = Input::RebindAction(
                        m_InputCapture.context,
                        m_InputCapture.action,
                        m_InputCapture.bindingIndex,
                        capturedBinding);
                }

                if (context.setProjectInputStatus)
                {
                    context.setProjectInputStatus(
                        applied
                            ? "Binding updated: " + m_InputCapture.action
                            : "Failed to update binding: " + m_InputCapture.action);
                }
                ClearInputCapture();
            }
        }

        const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("GameplayInputSystemBindings", 4, tableFlags))
        {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 190.0f);
            ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("Bindings", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();

            std::size_t inputEntryCount = 0;
            const InputActionSettingsEntry* inputEntries =
                GameplayInputBindingService::GetDefaultInputActionSettingsEntries(inputEntryCount);
            for (std::size_t inputEntryIndex = 0; inputEntryIndex < inputEntryCount; ++inputEntryIndex)
            {
                const InputActionSettingsEntry& entry = inputEntries[inputEntryIndex];
                if (GameplayInputBindingService::IsVehicleInputAction(entry.action))
                {
                    continue;
                }
                ImGui::TableNextRow();
                ImGui::PushID(entry.label);

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(entry.label);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%.*s", static_cast<int>(entry.context.size()), entry.context.data());

                ImGui::TableSetColumnIndex(2);
                const std::vector<InputBinding> bindings = Input::GetActionBindings(entry.context, entry.action);
                if (bindings.empty())
                {
                    ImGui::TextDisabled("Unbound");
                    ImGui::SameLine();
                    if (SmallButtonWithTooltip("Set"))
                    {
                        m_InputCapture.context = std::string(entry.context);
                        m_InputCapture.action = std::string(entry.action);
                        m_InputCapture.bindingIndex = 0;
                        m_InputCapture.isAppend = true;
                        m_InputCapture.allowAxes = entry.allowAxes;
                    }
                    ShowItemTooltip("Set the first binding for this action.");
                }
                else
                {
                    for (std::size_t bindingIndex = 0; bindingIndex < bindings.size(); ++bindingIndex)
                    {
                        if (bindingIndex > 0)
                        {
                            ImGui::SameLine();
                        }

                        std::string bindingButtonLabel = BindingToLabel(bindings[bindingIndex]);
                        bindingButtonLabel += "##Binding";
                        bindingButtonLabel += std::to_string(bindingIndex);
                        if (SmallButtonWithTooltip(bindingButtonLabel.c_str()))
                        {
                            m_InputCapture.context = std::string(entry.context);
                            m_InputCapture.action = std::string(entry.action);
                            m_InputCapture.bindingIndex = bindingIndex;
                            m_InputCapture.isAppend = false;
                            m_InputCapture.allowAxes = entry.allowAxes;
                        }
                        ShowItemTooltip("Rebind this input mapping.");
                    }

                    ImGui::SameLine();
                    if (SmallButtonWithTooltip("+"))
                    {
                        m_InputCapture.context = std::string(entry.context);
                        m_InputCapture.action = std::string(entry.action);
                        m_InputCapture.bindingIndex = bindings.size();
                        m_InputCapture.isAppend = true;
                        m_InputCapture.allowAxes = entry.allowAxes;
                    }
                    ShowItemTooltip("Add an additional binding to this action.");
                }

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.2f", Input::GetActionValue(entry.action));

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Vehicle Input Maps");

        if (!vehicleContexts.empty())
        {
            std::vector<const char*> vehicleContextLabels;
            vehicleContextLabels.reserve(vehicleContexts.size());
            int selectedVehicleContextIndex = 0;
            for (std::size_t i = 0; i < vehicleContexts.size(); ++i)
            {
                vehicleContextLabels.push_back(vehicleContexts[i].c_str());
                if (vehicleContexts[i] == m_SelectedVehicleInputMap)
                {
                    selectedVehicleContextIndex = static_cast<int>(i);
                }
            }

            if (ComboWithTooltip("Active Vehicle Map", &selectedVehicleContextIndex, vehicleContextLabels.data(), static_cast<int>(vehicleContextLabels.size())))
            {
                selectedVehicleContextIndex = std::clamp(selectedVehicleContextIndex, 0, static_cast<int>(vehicleContexts.size()) - 1);
                m_SelectedVehicleInputMap = vehicleContexts[static_cast<std::size_t>(selectedVehicleContextIndex)];
            }
            ShowItemTooltip("Select which vehicle input map to view and edit.");
        }

        std::vector<char> newVehicleMapBuffer(128, '\0');
        std::snprintf(newVehicleMapBuffer.data(), newVehicleMapBuffer.size(), "%s", m_NewVehicleInputMapName.c_str());
        if (InputTextWithTooltip("New Vehicle Map", newVehicleMapBuffer.data(), newVehicleMapBuffer.size()))
        {
            m_NewVehicleInputMapName = newVehicleMapBuffer.data();
        }
        ShowItemTooltip("Enter a new vehicle input context name.");

        if (ButtonWithTooltip("Create Vehicle Map"))
        {
            if (!m_NewVehicleInputMapName.empty())
            {
                GameplayInputBindingService::ResetVehicleInputBindings(m_NewVehicleInputMapName);
                m_SelectedVehicleInputMap = m_NewVehicleInputMapName;
                m_NewVehicleInputMapName.clear();
                if (context.setProjectInputStatus)
                {
                    context.setProjectInputStatus("Created vehicle input map: " + m_SelectedVehicleInputMap);
                }
            }
            else if (context.setProjectInputStatus)
            {
                context.setProjectInputStatus("Vehicle input map name cannot be empty.");
            }
        }
        ShowItemTooltip("Create a new named vehicle input map cloned from Vehicle.Default.");

        ImGui::SameLine();
        if (ButtonWithTooltip("Reset Selected Vehicle Map"))
        {
            GameplayInputBindingService::ResetVehicleInputBindings(m_SelectedVehicleInputMap);
            if (context.setProjectInputStatus)
            {
                context.setProjectInputStatus("Reset vehicle input map: " + m_SelectedVehicleInputMap);
            }
        }
        ShowItemTooltip("Restore the selected vehicle map to the default vehicle bindings.");

        if (m_SelectedVehicleInputMap != VehicleInputActions::Context)
        {
            ImGui::SameLine();
            if (ButtonWithTooltip("Delete Selected Vehicle Map"))
            {
                Input::RemoveContext(m_SelectedVehicleInputMap);
                m_SelectedVehicleInputMap = std::string(VehicleInputActions::Context);
                if (context.setProjectInputStatus)
                {
                    context.setProjectInputStatus("Deleted custom vehicle input map.");
                }
            }
            ShowItemTooltip("Delete the selected custom vehicle input map.");
        }

        if (ImGui::BeginTable("VehicleInputSystemBindings", 4, tableFlags))
        {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 190.0f);
            ImGui::TableSetupColumn("Context", ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("Bindings", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();

            std::size_t inputEntryCount = 0;
            const InputActionSettingsEntry* inputEntries =
                GameplayInputBindingService::GetDefaultInputActionSettingsEntries(inputEntryCount);
            for (std::size_t inputEntryIndex = 0; inputEntryIndex < inputEntryCount; ++inputEntryIndex)
            {
                const InputActionSettingsEntry& entry = inputEntries[inputEntryIndex];
                if (!GameplayInputBindingService::IsVehicleInputAction(entry.action))
                {
                    continue;
                }

                ImGui::TableNextRow();
                ImGui::PushID((std::string("VehicleMap.") + entry.label).c_str());

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(entry.label);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", m_SelectedVehicleInputMap.c_str());

                ImGui::TableSetColumnIndex(2);
                const std::vector<InputBinding> bindings = Input::GetActionBindings(m_SelectedVehicleInputMap, entry.action);
                if (bindings.empty())
                {
                    ImGui::TextDisabled("Unbound");
                    ImGui::SameLine();
                    if (SmallButtonWithTooltip("Set"))
                    {
                        m_InputCapture.context = m_SelectedVehicleInputMap;
                        m_InputCapture.action = std::string(entry.action);
                        m_InputCapture.bindingIndex = 0;
                        m_InputCapture.isAppend = true;
                        m_InputCapture.allowAxes = entry.allowAxes;
                    }
                    ShowItemTooltip("Set the first binding for this vehicle action.");
                }
                else
                {
                    for (std::size_t bindingIndex = 0; bindingIndex < bindings.size(); ++bindingIndex)
                    {
                        if (bindingIndex > 0)
                        {
                            ImGui::SameLine();
                        }

                        std::string bindingButtonLabel = BindingToLabel(bindings[bindingIndex]);
                        bindingButtonLabel += "##VehicleBinding";
                        bindingButtonLabel += std::to_string(bindingIndex);
                        if (SmallButtonWithTooltip(bindingButtonLabel.c_str()))
                        {
                            m_InputCapture.context = m_SelectedVehicleInputMap;
                            m_InputCapture.action = std::string(entry.action);
                            m_InputCapture.bindingIndex = bindingIndex;
                            m_InputCapture.isAppend = false;
                            m_InputCapture.allowAxes = entry.allowAxes;
                        }
                        ShowItemTooltip("Rebind this vehicle input mapping.");
                    }

                    ImGui::SameLine();
                    if (SmallButtonWithTooltip("+"))
                    {
                        m_InputCapture.context = m_SelectedVehicleInputMap;
                        m_InputCapture.action = std::string(entry.action);
                        m_InputCapture.bindingIndex = bindings.size();
                        m_InputCapture.isAppend = true;
                        m_InputCapture.allowAxes = entry.allowAxes;
                    }
                    ShowItemTooltip("Add an additional binding to this vehicle action.");
                }

                ImGui::TableSetColumnIndex(3);
                ImGui::Text("%.2f", Input::GetActionValue(m_SelectedVehicleInputMap, entry.action));

                ImGui::PopID();
            }

            ImGui::EndTable();
        }
    }

    void ProjectSettingsPanel::DrawLayersSection(const ProjectSettingsPanelContext& context)
    {
        if (!Project::IsLoaded())
        {
            ImGui::TextDisabled("No loaded project.");
            return;
        }

        SyncDraftFromLoaded();

        ImGui::TextUnformatted("Entity Tags");
        ImGui::Separator();
        ImGui::TextWrapped("All new entities default to the Untagged tag. Add project-specific tags here for the entity inspector.");

        std::vector<char> newTagBuffer(128, '\0');
        std::snprintf(newTagBuffer.data(), newTagBuffer.size(), "%s", m_NewTagName.c_str());
        if (InputTextWithTooltip("New Tag", newTagBuffer.data(), newTagBuffer.size()))
        {
            m_NewTagName = newTagBuffer.data();
        }
        ShowItemTooltip("Enter a new entity tag name.");

        ImGui::SameLine();
        if (ButtonWithTooltip("Add Tag"))
        {
            auto normalized = m_NewTagName;
            normalized.erase(normalized.begin(), std::find_if(normalized.begin(), normalized.end(), [](unsigned char ch)
            {
                return !std::isspace(ch);
            }));
            normalized.erase(std::find_if(normalized.rbegin(), normalized.rend(), [](unsigned char ch)
            {
                return !std::isspace(ch);
            }).base(), normalized.end());

            if (normalized.empty())
            {
                if (context.setProjectConfigStatus)
                {
                    context.setProjectConfigStatus("Tag name cannot be empty.");
                }
            }
            else
            {
                const auto exists = std::find_if(
                    m_Draft.tags.begin(),
                    m_Draft.tags.end(),
                    [&normalized](const std::string& existing)
                    {
                        if (existing.size() != normalized.size())
                        {
                            return false;
                        }
                        for (std::size_t i = 0; i < existing.size(); ++i)
                        {
                            if (std::tolower(static_cast<unsigned char>(existing[i])) !=
                                std::tolower(static_cast<unsigned char>(normalized[i])))
                            {
                                return false;
                            }
                        }
                        return true;
                    });
                if (exists != m_Draft.tags.end())
                {
                    if (context.setProjectConfigStatus)
                    {
                        context.setProjectConfigStatus("Tag already exists: " + normalized);
                    }
                }
                else
                {
                    m_Draft.tags.push_back(normalized);
                    m_NewTagName.clear();
                    if (context.setProjectConfigStatus)
                    {
                        context.setProjectConfigStatus("Added tag: " + normalized);
                    }
                }
            }
        }
        ShowItemTooltip("Add a new tag to this project.");

        ImGui::Spacing();
        const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("ProjectTagsTable", 2, tableFlags))
        {
            ImGui::TableSetupColumn("Tag", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 90.0f);
            ImGui::TableHeadersRow();

            constexpr std::array<std::string_view, 4> defaultTags = {
                "Untagged",
                "Player",
                "MainCamera",
                "GameController"
            };

            for (std::size_t tagIndex = 0; tagIndex < m_Draft.tags.size(); ++tagIndex)
            {
                std::string& tag = m_Draft.tags[tagIndex];
                ImGui::TableNextRow();
                ImGui::PushID(static_cast<int>(tagIndex));

                ImGui::TableSetColumnIndex(0);
                std::vector<char> tagBuffer(128, '\0');
                std::snprintf(tagBuffer.data(), tagBuffer.size(), "%s", tag.c_str());
                const bool isDefaultTag = std::find(defaultTags.begin(), defaultTags.end(), std::string_view(tag)) != defaultTags.end();
                if (isDefaultTag)
                {
                    ImGui::TextUnformatted(tag.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("(Default)");
                }
                else if (InputTextWithTooltip("##TagName", tagBuffer.data(), tagBuffer.size()))
                {
                    tag = tagBuffer.data();
                }

                ImGui::TableSetColumnIndex(1);
                if (isDefaultTag)
                {
                    ImGui::TextDisabled("Locked");
                }
                else if (SmallButtonWithTooltip("Remove"))
                {
                    const std::string removedTag = tag;
                    m_Draft.tags.erase(m_Draft.tags.begin() + static_cast<std::ptrdiff_t>(tagIndex));
                    if (context.setProjectConfigStatus)
                    {
                        context.setProjectConfigStatus("Removed tag: " + removedTag);
                    }
                    ImGui::PopID();
                    break;
                }

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        ImGui::Spacing();
        if (ButtonWithTooltip("Save Project Settings"))
        {
            if (Project::UpdateSettings(m_Draft, true))
            {
                m_DraftInitialized = false;
                if (context.setProjectConfigStatus)
                {
                    context.setProjectConfigStatus("Project settings saved.");
                }
                if (context.onProjectConfigSaved)
                {
                    context.onProjectConfigSaved();
                }
            }
            else if (context.setProjectConfigStatus)
            {
                context.setProjectConfigStatus("Failed to save project settings.");
            }
        }
        ShowItemTooltip("Save the current tag list to the project file.");

        ImGui::SameLine();
        if (ButtonWithTooltip("Reload"))
        {
            m_DraftInitialized = false;
            SyncDraftFromLoaded();
            if (context.setProjectConfigStatus)
            {
                context.setProjectConfigStatus("Project settings reloaded.");
            }
        }
        ShowItemTooltip("Reload tags from disk and discard unsaved changes.");

        const std::string_view configStatus = context.getProjectConfigStatus ? context.getProjectConfigStatus() : std::string_view {};
        if (!configStatus.empty())
        {
            ImGui::TextDisabled("%.*s", static_cast<int>(configStatus.size()), configStatus.data());
        }
    }

    void ProjectSettingsPanel::DrawBuildProfileEditor(
        const char* label,
        Project::BuildProfileConfig& config,
        const char* idSuffix)
    {
        std::string header = label;
        header += "##BuildProfile";
        header += idSuffix;
        if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            return;
        }

        std::string validationLabel = "Enable Validation##";
        validationLabel += idSuffix;
        std::string optimizationLabel = "Enable Optimizations##";
        optimizationLabel += idSuffix;
        std::string debugSymbolsLabel = "Enable Debug Symbols##";
        debugSymbolsLabel += idSuffix;
        std::string hotReloadLabel = "Enable Hot Reload##";
        hotReloadLabel += idSuffix;
        std::string outputLabel = "Output Directory##";
        outputLabel += idSuffix;
        std::string definesLabel = "Defines##";
        definesLabel += idSuffix;

        CheckboxWithTooltip(validationLabel.c_str(), &config.enableValidation);
        ShowItemTooltip("Enable runtime validation checks for this build profile.");
        CheckboxWithTooltip(optimizationLabel.c_str(), &config.enableOptimizations);
        ShowItemTooltip("Enable compiler optimizations for this build profile.");
        CheckboxWithTooltip(debugSymbolsLabel.c_str(), &config.enableDebugSymbols);
        ShowItemTooltip("Emit debug symbols for this build profile.");
        CheckboxWithTooltip(hotReloadLabel.c_str(), &config.enableHotReload);
        ShowItemTooltip("Enable hot-reload support for this build profile.");

        std::vector<char> outputBuffer(512, '\0');
        std::snprintf(outputBuffer.data(), outputBuffer.size(), "%s", config.outputDirectory.c_str());
        if (InputTextWithTooltip(outputLabel.c_str(), outputBuffer.data(), outputBuffer.size()))
        {
            config.outputDirectory = outputBuffer.data();
        }
        ShowItemTooltip("Output directory for build artifacts.");

        std::vector<char> definesBuffer(512, '\0');
        std::snprintf(definesBuffer.data(), definesBuffer.size(), "%s", config.defines.c_str());
        if (InputTextWithTooltip(definesLabel.c_str(), definesBuffer.data(), definesBuffer.size()))
        {
            config.defines = definesBuffer.data();
        }
        ShowItemTooltip("Semicolon/comma separated preprocessor defines.");
    }

    void ProjectSettingsPanel::SyncDraftFromLoaded()
    {
        if (!Project::IsLoaded())
        {
            m_DraftInitialized = false;
            return;
        }

        if (m_DraftInitialized)
        {
            return;
        }

        m_Draft = Project::GetConfig();
        m_DraftInitialized = true;
    }

    void ProjectSettingsPanel::ClearInputCapture()
    {
        m_InputCapture.context.clear();
        m_InputCapture.action.clear();
        m_InputCapture.bindingIndex = 0;
        m_InputCapture.isAppend = false;
        m_InputCapture.allowAxes = false;
    }
}
