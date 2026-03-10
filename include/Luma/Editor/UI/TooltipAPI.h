#pragma once

#include <array>
#include <string>
#include <string_view>

namespace Luma::UI
{
    struct TooltipStyle
    {
        std::array<float, 4> backgroundColor { 0.09f, 0.10f, 0.12f, 0.98f };
        std::array<float, 4> borderColor { 0.23f, 0.27f, 0.33f, 1.0f };
        std::array<float, 4> titleColor { 0.93f, 0.95f, 0.98f, 1.0f };
        std::array<float, 4> textColor { 0.79f, 0.83f, 0.90f, 1.0f };
        std::array<float, 2> padding { 10.0f, 8.0f };
        float cornerRadius = 6.0f;
        float borderSize = 1.0f;
    };

    struct TooltipDesc
    {
        std::string title;
        std::string description;
        std::string shortcut;
        std::string docLink;
        void* previewTexture = nullptr;
        std::array<float, 2> previewSize { 192.0f, 108.0f };
    };

    class Tooltip
    {
    public:
        static void SetDelay(float seconds);
        static float GetDelay();

        static void SetStyle(const TooltipStyle& style);
        static const TooltipStyle& GetStyle();

        static std::string VisibleLabel(const char* imguiLabel);
        static void Show(std::string_view text);
        static void ShowForItemLabel(const char* imguiLabel, std::string_view prefix = {});
        static void ShowRich(const TooltipDesc& desc);
        static void ShowInteractive(const TooltipDesc& desc);
    };
}

