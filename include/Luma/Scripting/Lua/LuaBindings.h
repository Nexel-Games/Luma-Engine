#pragma once

struct lua_State;

namespace Luma
{
    class LuaBindings
    {
    public:
        static void RegisterCore(lua_State* state);
    };
}
