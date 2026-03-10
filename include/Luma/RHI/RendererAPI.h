#pragma once

#include <string_view>

namespace Luma
{
    enum class RendererAPI
    {
        OpenGL
    };

    inline std::string_view ToString(const RendererAPI api)
    {
        switch (api)
        {
        case RendererAPI::OpenGL:
            return "OpenGL";
        default:
            return "Unknown";
        }
    }
}
