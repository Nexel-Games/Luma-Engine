#pragma once

#include <string>
#include <string_view>

struct lua_State;

namespace Luma
{
    class LuaHelpers
    {
    public:
        static int PushTraceback(lua_State* state);
        static std::string PopError(lua_State* state);
        static std::string ToString(lua_State* state, int index);
        static void AppendPackagePath(lua_State* state, std::string_view pathPattern);
    };
}
