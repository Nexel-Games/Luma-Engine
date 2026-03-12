#pragma once

#include <string>

namespace Luma
{
    struct LuaScriptComponent
    {
        bool enabled = true;
        std::string scriptAsset;
    };
}
