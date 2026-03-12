#include "Luma/Scripting/Lua/LuaBindings.h"

#include <lua.hpp>

#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Scripting/Lua/LuaHelpers.h"

namespace Luma
{
    namespace
    {
        int LogFromLua(lua_State* state, const LogLevel level)
        {
            const std::string message = LuaHelpers::ToString(state, 1);

            switch (level)
            {
            case LogLevel::Trace:
                LUMA_LOG_TRACE("Script", message);
                break;
            case LogLevel::Info:
                LUMA_LOG_INFO("Script", message);
                break;
            case LogLevel::Warn:
                LUMA_LOG_WARN("Script", message);
                break;
            case LogLevel::Error:
                LUMA_LOG_ERROR("Script", message);
                break;
            case LogLevel::Fatal:
                LUMA_LOG_FATAL("Script", message);
                break;
            default:
                LUMA_LOG_INFO("Script", message);
                break;
            }

            return 0;
        }

        int LuaLogTrace(lua_State* state)
        {
            return LogFromLua(state, LogLevel::Trace);
        }

        int LuaLogInfo(lua_State* state)
        {
            return LogFromLua(state, LogLevel::Info);
        }

        int LuaLogWarn(lua_State* state)
        {
            return LogFromLua(state, LogLevel::Warn);
        }

        int LuaLogError(lua_State* state)
        {
            return LogFromLua(state, LogLevel::Error);
        }

        int LuaLogFatal(lua_State* state)
        {
            return LogFromLua(state, LogLevel::Fatal);
        }
    }

    void LuaBindings::RegisterCore(lua_State* state)
    {
        if (state == nullptr)
        {
            return;
        }

        lua_newtable(state);

        lua_newtable(state);
        lua_pushcfunction(state, LuaLogTrace);
        lua_setfield(state, -2, "Trace");
        lua_pushcfunction(state, LuaLogInfo);
        lua_setfield(state, -2, "Info");
        lua_pushcfunction(state, LuaLogWarn);
        lua_setfield(state, -2, "Warn");
        lua_pushcfunction(state, LuaLogError);
        lua_setfield(state, -2, "Error");
        lua_pushcfunction(state, LuaLogFatal);
        lua_setfield(state, -2, "Fatal");
        lua_setfield(state, -2, "Log");

        lua_setglobal(state, "Luma");
    }
}
