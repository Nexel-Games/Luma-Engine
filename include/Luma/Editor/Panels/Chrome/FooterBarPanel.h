#pragma once

#include <string_view>

namespace Luma::Editor
{
    struct FooterBarPanelContext
    {
        std::string_view statusText;
        std::string_view metricsText;
    };

    class FooterBarPanel
    {
    public:
        void Draw(const FooterBarPanelContext& context);
    };
}
