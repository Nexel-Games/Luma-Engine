#pragma once

#include "Luma/Core/Foundation/Logging.h"

#include <string_view>

namespace Luma::Editor
{
    struct FooterBarPanelContext
    {
        std::string_view messageText;
        LogLevel messageLevel = LogLevel::Info;
        std::string_view metricsText;
    };

    class FooterBarPanel
    {
    public:
        void Draw(const FooterBarPanelContext& context);
    };
}
