#include "Luma/Editor/UI/TooltipAPI.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>

#include <imgui.h>
#include <imgui_internal.h>

namespace Luma::UI
{
    namespace
    {
        struct TooltipRuntime
        {
            TooltipStyle style {};
            float delaySeconds = 0.4f;
            ImGuiID hoveredId = 0;
            double hoveredSince = 0.0;
        };

        TooltipRuntime& Runtime()
        {
            static TooltipRuntime runtime;
            return runtime;
        }

        std::string TrimWhitespace(std::string value)
        {
            auto notWhitespace = [](const unsigned char c)
            {
                return !std::isspace(c);
            };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notWhitespace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notWhitespace).base(), value.end());
            return value;
        }

        ImGuiID ResolveHoveredItemId()
        {
            ImGuiContext* context = ImGui::GetCurrentContext();
            if (context == nullptr)
            {
                return 0;
            }

            ImGuiID itemId = context->LastItemData.ID;
            if (itemId != 0)
            {
                return itemId;
            }

            const ImRect rect = context->LastItemData.Rect;
            std::uint32_t hash = 2166136261u;
            auto hashFloat = [&hash](const float value)
            {
                const auto quantized = static_cast<std::uint32_t>(std::floor(value * 8.0f));
                hash ^= quantized + 0x9e3779b9u + (hash << 6) + (hash >> 2);
            };
            hashFloat(rect.Min.x);
            hashFloat(rect.Min.y);
            hashFloat(rect.Max.x);
            hashFloat(rect.Max.y);
            return static_cast<ImGuiID>(hash);
        }

        bool ShouldShowForLastItem()
        {
            if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNone | ImGuiHoveredFlags_AllowWhenDisabled))
            {
                Runtime().hoveredId = 0;
                Runtime().hoveredSince = 0.0;
                return false;
            }

            const ImGuiID currentItemId = ResolveHoveredItemId();
            const double now = ImGui::GetTime();
            TooltipRuntime& runtime = Runtime();
            if (runtime.hoveredId != currentItemId)
            {
                runtime.hoveredId = currentItemId;
                runtime.hoveredSince = now;
            }

            if (runtime.delaySeconds <= 0.0f)
            {
                return true;
            }

            return (now - runtime.hoveredSince) >= static_cast<double>(runtime.delaySeconds);
        }

        void BeginStyledTooltip()
        {
            const TooltipStyle& style = Runtime().style;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(style.padding[0], style.padding[1]));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, style.cornerRadius);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, style.borderSize);
            ImGui::PushStyleColor(
                ImGuiCol_PopupBg,
                ImVec4(style.backgroundColor[0], style.backgroundColor[1], style.backgroundColor[2], style.backgroundColor[3]));
            ImGui::PushStyleColor(
                ImGuiCol_Border,
                ImVec4(style.borderColor[0], style.borderColor[1], style.borderColor[2], style.borderColor[3]));
            ImGui::BeginTooltip();
        }

        void EndStyledTooltip()
        {
            ImGui::EndTooltip();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
        }
    }

    void Tooltip::SetDelay(const float seconds)
    {
        Runtime().delaySeconds = std::max(seconds, 0.0f);
    }

    float Tooltip::GetDelay()
    {
        return Runtime().delaySeconds;
    }

    void Tooltip::SetStyle(const TooltipStyle& style)
    {
        Runtime().style = style;
    }

    const TooltipStyle& Tooltip::GetStyle()
    {
        return Runtime().style;
    }

    std::string Tooltip::VisibleLabel(const char* imguiLabel)
    {
        if (imguiLabel == nullptr)
        {
            return {};
        }

        std::string label = imguiLabel;
        const std::size_t idIndex = label.find("##");
        if (idIndex != std::string::npos)
        {
            label.erase(idIndex);
        }

        return TrimWhitespace(label);
    }

    void Tooltip::Show(const std::string_view text)
    {
        if (text.empty())
        {
            return;
        }
        if (!ShouldShowForLastItem())
        {
            return;
        }

        BeginStyledTooltip();
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImVec4(
                Runtime().style.textColor[0],
                Runtime().style.textColor[1],
                Runtime().style.textColor[2],
                Runtime().style.textColor[3]));
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 38.0f);
        ImGui::TextUnformatted(text.data(), text.data() + text.size());
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        EndStyledTooltip();
    }

    void Tooltip::ShowForItemLabel(const char* imguiLabel, const std::string_view prefix)
    {
        const std::string visibleLabel = VisibleLabel(imguiLabel);
        if (visibleLabel.empty())
        {
            return;
        }

        if (prefix.empty())
        {
            Show(visibleLabel);
            return;
        }

        std::string tooltip(prefix);
        tooltip += visibleLabel;
        Show(tooltip);
    }

    void Tooltip::ShowRich(const TooltipDesc& desc)
    {
        if (desc.title.empty() && desc.description.empty() && desc.shortcut.empty() && desc.docLink.empty() && desc.previewTexture == nullptr)
        {
            return;
        }
        if (!ShouldShowForLastItem())
        {
            return;
        }

        BeginStyledTooltip();
        const TooltipStyle& style = Runtime().style;

        if (!desc.title.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(style.titleColor[0], style.titleColor[1], style.titleColor[2], style.titleColor[3]));
            ImGui::TextUnformatted(desc.title.c_str());
            ImGui::PopStyleColor();
        }

        if (!desc.description.empty())
        {
            if (!desc.title.empty())
            {
                ImGui::Separator();
            }
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(style.textColor[0], style.textColor[1], style.textColor[2], style.textColor[3]));
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 38.0f);
            ImGui::TextUnformatted(desc.description.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        if (!desc.shortcut.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled("Shortcut: %s", desc.shortcut.c_str());
        }
        if (!desc.docLink.empty())
        {
            ImGui::TextDisabled("Docs: %s", desc.docLink.c_str());
        }
        if (desc.previewTexture != nullptr)
        {
            ImGui::Spacing();
            ImGui::Image(
                reinterpret_cast<ImTextureID>(desc.previewTexture),
                ImVec2(desc.previewSize[0], desc.previewSize[1]));
        }

        EndStyledTooltip();
    }

    void Tooltip::ShowInteractive(const TooltipDesc& desc)
    {
        ShowRich(desc);
    }
}
