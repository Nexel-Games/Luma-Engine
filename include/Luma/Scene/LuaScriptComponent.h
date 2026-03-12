#pragma once

#include <unordered_map>
#include <string>

#include "Luma/Scripting/ScriptProperty.h"

namespace Luma
{
    struct LuaScriptComponent
    {
        bool enabled = true;
        std::string scriptAsset;
        std::unordered_map<std::string, ScriptValue> propertyOverrides;
    };
}
