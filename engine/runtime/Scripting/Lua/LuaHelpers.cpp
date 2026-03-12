#include "Luma/Scripting/Lua/LuaHelpers.h"

#include <string>

#include <lua.hpp>

namespace Luma
{
    namespace
    {
        int Traceback(lua_State* state)
        {
            const char* message = lua_tostring(state, 1);
            if (message == nullptr)
            {
                if (!luaL_callmeta(state, 1, "__tostring"))
                {
                    lua_pushliteral(state, "Lua error");
                }
            }

            luaL_traceback(state, state, lua_tostring(state, 1), 1);
            return 1;
        }
    }

    int LuaHelpers::PushTraceback(lua_State* state)
    {
        lua_pushcfunction(state, Traceback);
        return lua_gettop(state);
    }

    std::string LuaHelpers::PopError(lua_State* state)
    {
        if (state == nullptr || lua_gettop(state) <= 0)
        {
            return {};
        }

        std::string error = ToString(state, -1);
        lua_pop(state, 1);
        return error;
    }

    std::string LuaHelpers::ToString(lua_State* state, const int index)
    {
        if (state == nullptr)
        {
            return {};
        }

        size_t length = 0;
        const char* text = luaL_tolstring(state, index, &length);
        std::string result = text != nullptr ? std::string(text, length) : std::string();
        lua_pop(state, 1);
        return result;
    }

    void LuaHelpers::AppendPackagePath(lua_State* state, const std::string_view pathPattern)
    {
        if (state == nullptr || pathPattern.empty())
        {
            return;
        }

        lua_getglobal(state, "package");
        if (!lua_istable(state, -1))
        {
            lua_pop(state, 1);
            return;
        }

        lua_getfield(state, -1, "path");
        std::string currentPath;
        if (lua_isstring(state, -1))
        {
            currentPath = lua_tostring(state, -1);
        }
        lua_pop(state, 1);

        const std::string pathPatternString(pathPattern);
        if (!currentPath.empty() && currentPath.find(pathPatternString) != std::string::npos)
        {
            lua_pop(state, 1);
            return;
        }

        if (!currentPath.empty() && currentPath.back() != ';')
        {
            currentPath.push_back(';');
        }
        currentPath += pathPatternString;

        lua_pushlstring(state, currentPath.c_str(), currentPath.size());
        lua_setfield(state, -2, "path");
        lua_pop(state, 1);
    }
}
