#include "Luma/Editor/Panels/Inspector/InspectorAudioPanel.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <utility>

#include <imgui.h>

#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Audio/Core/AudioSystem.h"
#include "Luma/Scene/AudioListenerComponent.h"
#include "Luma/Scene/AudioSourceComponent.h"
#include "Luma/Scene/TagComponent.h"

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
        bool DragFloatWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragFloat(label, std::forward<Args>(args)...);
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

        bool IsAudioSelection(const std::filesystem::path& path)
        {
            const std::string extension = path.extension().generic_string();
            return extension == ".wav" || extension == ".ogg" || extension == ".mp3" || extension == ".flac" ||
                extension == ".lumaaudio";
        }

        std::string NormalizeReferenceString(std::string value)
        {
            std::replace(value.begin(), value.end(), '\\', '/');
            return value;
        }

        void DrawSectionLabel(const char* label, const char* tooltip)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", label);
            if (tooltip != nullptr)
            {
                ShowItemTooltip(tooltip);
            }
        }
    }

    void InspectorAudioPanel::StopPreview()
    {
        if (m_PreviewHandle != 0)
        {
            Audio::AudioSystem::Stop(m_PreviewHandle);
            m_PreviewHandle = 0;
        }

        m_PreviewEntity = entt::null;
        m_PreviewClipAsset.clear();
    }

    void InspectorAudioPanel::Draw(const InspectorAudioPanelContext& context)
    {
        if (context.scene == nullptr || context.selectedEntity == entt::null)
        {
            StopPreview();
            return;
        }

        auto& registry = context.scene->GetRegistry();
        if (!registry.valid(context.selectedEntity))
        {
            StopPreview();
            return;
        }

        if (m_PreviewHandle != 0 && !Audio::AudioSystem::IsPlaying(m_PreviewHandle))
        {
            StopPreview();
        }

        if (m_PreviewHandle != 0 &&
            (m_PreviewEntity != context.selectedEntity ||
             !registry.valid(m_PreviewEntity)))
        {
            StopPreview();
        }

        if (registry.all_of<AudioSourceComponent>(context.selectedEntity))
        {
            auto& source = registry.get<AudioSourceComponent>(context.selectedEntity);
            if (m_PreviewHandle != 0 &&
                m_PreviewEntity == context.selectedEntity &&
                m_PreviewClipAsset != source.clipAsset)
            {
                StopPreview();
            }

            ImGui::Separator();
            if (ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Configure clip playback, spatial settings, and runtime audio behavior.");
                ImGui::PushID("AudioSourceComponent");

                std::array<char, 260> clipBuffer {};
                std::snprintf(clipBuffer.data(), clipBuffer.size(), "%s", source.clipAsset.c_str());
                if (InputTextWithTooltip("Clip", clipBuffer.data(), clipBuffer.size()))
                {
                    source.clipAsset = NormalizeReferenceString(clipBuffer.data());
                }

                ImGui::SameLine();
                if (ButtonWithTooltip("Use Selected"))
                {
                    if (context.selectedContentEntry != nullptr && !context.selectedContentEntry->empty() &&
                        IsAudioSelection(*context.selectedContentEntry))
                    {
                        source.clipAsset = NormalizeReferenceString(context.selectedContentEntry->generic_string());
                        if (context.setContentStatus)
                        {
                            context.setContentStatus("Assigned selected audio asset to Audio Source.");
                        }
                    }
                }

                DrawSectionLabel("Playback", "Core playback settings for this source.");
                CheckboxWithTooltip("Play On Awake", &source.playOnAwake);
                CheckboxWithTooltip("Looping", &source.looping);
                CheckboxWithTooltip("Spatialized", &source.spatialized);
                CheckboxWithTooltip("Mute", &source.mute);
                DragFloatWithTooltip("Volume", &source.volume, 0.01f, 0.0f, 4.0f);
                DragFloatWithTooltip("Pitch", &source.pitch, 0.01f, 0.01f, 4.0f);

                if (source.spatialized)
                {
                    DrawSectionLabel("3D Spatial", "Distance falloff settings for positional playback.");
                    DragFloatWithTooltip("Min Distance", &source.minDistance, 0.05f, 0.0f, 1000.0f);
                    DragFloatWithTooltip("Max Distance", &source.maxDistance, 0.05f, 0.01f, 5000.0f);
                    source.minDistance = std::max(source.minDistance, 0.0f);
                    source.maxDistance = std::max(source.maxDistance, source.minDistance + 0.01f);
                }

                source.volume = std::clamp(source.volume, 0.0f, 4.0f);
                source.pitch = std::clamp(source.pitch, 0.01f, 4.0f);

                if (!context.playModeActive)
                {
                    DrawSectionLabel("Preview", "Preview this source in the editor without entering Play mode.");
                    const bool canPreview = !source.clipAsset.empty() && !source.mute;
                    if (!canPreview)
                    {
                        ImGui::BeginDisabled();
                    }

                    if (ButtonWithTooltip("Play Preview"))
                    {
                        StopPreview();
                        if (const auto clip = Audio::AudioSystem::LoadClip(source.clipAsset);
                            clip && clip->IsValid())
                        {
                            Audio::PlaySettings settings {};
                            settings.volume = source.volume;
                            settings.pitch = source.pitch;
                            settings.looping = source.looping;
                            settings.spatialized = false;
                            m_PreviewHandle = Audio::AudioSystem::Play2D(clip, settings);
                            if (m_PreviewHandle != 0)
                            {
                                m_PreviewEntity = context.selectedEntity;
                                m_PreviewClipAsset = source.clipAsset;
                            }
                        }
                    }

                    if (!canPreview)
                    {
                        ImGui::EndDisabled();
                    }

                    ImGui::SameLine();
                    if (!canPreview)
                    {
                        ImGui::BeginDisabled();
                    }
                    if (ButtonWithTooltip("One Shot"))
                    {
                        if (const auto clip = Audio::AudioSystem::LoadClip(source.clipAsset);
                            clip && clip->IsValid())
                        {
                            Audio::PlaySettings settings {};
                            settings.volume = source.volume;
                            settings.pitch = source.pitch;
                            settings.looping = false;
                            settings.spatialized = false;
                            (void)Audio::AudioSystem::Play2D(clip, settings);
                        }
                    }
                    if (!canPreview)
                    {
                        ImGui::EndDisabled();
                    }

                    ImGui::SameLine();
                    const bool previewActive =
                        m_PreviewHandle != 0 &&
                        m_PreviewEntity == context.selectedEntity &&
                        Audio::AudioSystem::IsPlaying(m_PreviewHandle);
                    if (!previewActive)
                    {
                        ImGui::BeginDisabled();
                    }
                    if (ButtonWithTooltip("Stop Preview"))
                    {
                        StopPreview();
                    }
                    if (!previewActive)
                    {
                        ImGui::EndDisabled();
                    }
                }
                else
                {
                    DrawSectionLabel("Runtime", "Current runtime playback state while in Play mode.");
                    const bool isPlaying = source.runtimeHandle != 0 && Audio::AudioSystem::IsPlaying(source.runtimeHandle);
                    ImGui::TextDisabled("State: %s", isPlaying ? "Playing" : "Idle");
                    ShowItemTooltip("Whether this source currently has an active runtime voice.");
                }

                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Audio Source Component"))
            {
                if (m_PreviewEntity == context.selectedEntity)
                {
                    StopPreview();
                }
                registry.remove<AudioSourceComponent>(context.selectedEntity);
            }
        }

        if (registry.all_of<AudioListenerComponent>(context.selectedEntity))
        {
            auto& listener = registry.get<AudioListenerComponent>(context.selectedEntity);

            ImGui::Separator();
            if (ImGui::CollapsingHeader("Audio Listener", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ShowItemTooltip("Marks this entity as an active scene audio listener during Play mode.");
                ImGui::PushID("AudioListenerComponent");
                CheckboxWithTooltip("Enabled", &listener.enabled);
                DragFloatWithTooltip("Volume", &listener.volume, 0.01f, 0.0f, 2.0f);
                listener.volume = std::clamp(listener.volume, 0.0f, 2.0f);
                ImGui::PopID();
            }

            if (ButtonWithTooltip("Remove Audio Listener Component"))
            {
                registry.remove<AudioListenerComponent>(context.selectedEntity);
            }
        }
    }
}
