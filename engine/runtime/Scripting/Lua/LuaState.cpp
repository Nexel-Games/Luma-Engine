#include "Luma/Scripting/Lua/LuaState.h"

#include <array>
#include <filesystem>
#include <string>

#include <lua.hpp>

#include "Luma/Core/App/Project.h"
#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Scripting/Lua/LuaBindings.h"
#include "Luma/Scripting/Lua/LuaHelpers.h"

namespace Luma
{
    namespace
    {
        int LuaPanic(lua_State* state)
        {
            const char* message = lua_tostring(state, -1);
            LUMA_LOG_FATAL("Script", message != nullptr ? message : "Lua panic.");
            return 0;
        }

        std::filesystem::path ResolveScriptPath(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return {};
            }

            if (path.is_absolute())
            {
                return path;
            }

            if (std::filesystem::exists(path))
            {
                return std::filesystem::absolute(path);
            }

            if (Project::IsLoaded())
            {
                const std::filesystem::path projectScriptsPath = Project::GetScriptsPath() / path;
                if (std::filesystem::exists(projectScriptsPath))
                {
                    return std::filesystem::absolute(projectScriptsPath);
                }

                const std::filesystem::path projectAssetsScriptsPath = Project::GetAssetsPath() / "Scripts" / path;
                if (std::filesystem::exists(projectAssetsScriptsPath))
                {
                    return std::filesystem::absolute(projectAssetsScriptsPath);
                }
            }

            return std::filesystem::absolute(path);
        }

        void AppendPackagePatterns(lua_State* state, const std::filesystem::path& directory)
        {
            if (state == nullptr || directory.empty() || !std::filesystem::exists(directory))
            {
                return;
            }

            const std::string root = directory.generic_string();
            LuaHelpers::AppendPackagePath(state, root + "/?.lua");
            LuaHelpers::AppendPackagePath(state, root + "/?/init.lua");
        }
    }

    LuaState::~LuaState()
    {
        Shutdown();
    }

    bool LuaState::Initialize()
    {
        if (m_State != nullptr)
        {
            return true;
        }

        m_State = luaL_newstate();
        if (m_State == nullptr)
        {
            LUMA_LOG_ERROR("Script", "Failed to allocate Lua state.");
            return false;
        }

        lua_atpanic(m_State, LuaPanic);
        luaL_openlibs(m_State);
        RegisterCoreBindings();
        ConfigurePackagePaths();

        LUMA_LOG_INFO("Script", std::string("Initialized Lua runtime ") + LUA_VERSION);
        return true;
    }

    void LuaState::Shutdown()
    {
        if (m_State == nullptr)
        {
            return;
        }

        lua_close(m_State);
        m_State = nullptr;
        LUMA_LOG_INFO("Script", "Lua runtime shutdown complete.");
    }

    bool LuaState::IsInitialized() const
    {
        return m_State != nullptr;
    }

    void LuaState::RegisterCoreBindings()
    {
        LuaBindings::RegisterCore(m_State);
    }

    void LuaState::ConfigurePackagePaths()
    {
        if (m_State == nullptr)
        {
            return;
        }

        const std::array<std::filesystem::path, 4> packageRoots = {
            std::filesystem::current_path() / "assets" / "scripts",
            std::filesystem::current_path() / "Scripts",
            Project::IsLoaded() ? Project::GetScriptsPath() : std::filesystem::path(),
            Project::IsLoaded() ? (Project::GetAssetsPath() / "Scripts") : std::filesystem::path()
        };

        for (const std::filesystem::path& root : packageRoots)
        {
            AppendPackagePatterns(m_State, root);
        }
    }

    bool LuaState::ExecuteFile(const std::filesystem::path& path, std::string* outError)
    {
        if (m_State == nullptr)
        {
            const std::string error = "Lua runtime is not initialized.";
            if (outError != nullptr)
            {
                *outError = error;
            }
            LUMA_LOG_ERROR("Script", error);
            return false;
        }

        const std::filesystem::path resolvedPath = ResolveScriptPath(path);
        if (resolvedPath.empty() || !std::filesystem::exists(resolvedPath))
        {
            const std::string error = "Lua script file does not exist: " + path.string();
            if (outError != nullptr)
            {
                *outError = error;
            }
            LUMA_LOG_ERROR("Script", error);
            return false;
        }

        const int stackBase = lua_gettop(m_State);
        const int tracebackIndex = LuaHelpers::PushTraceback(m_State);
        const std::string resolvedPathString = resolvedPath.string();

        if (luaL_loadfile(m_State, resolvedPathString.c_str()) != LUA_OK)
        {
            const std::string error = LuaHelpers::PopError(m_State);
            lua_settop(m_State, stackBase);
            if (outError != nullptr)
            {
                *outError = error;
            }
            LUMA_LOG_ERROR("Script", "Failed to load '" + resolvedPathString + "': " + error);
            return false;
        }

        if (lua_pcall(m_State, 0, LUA_MULTRET, tracebackIndex) != LUA_OK)
        {
            const std::string error = LuaHelpers::PopError(m_State);
            lua_settop(m_State, stackBase);
            if (outError != nullptr)
            {
                *outError = error;
            }
            LUMA_LOG_ERROR("Script", "Runtime error in '" + resolvedPathString + "': " + error);
            return false;
        }

        lua_settop(m_State, stackBase);
        return true;
    }

    bool LuaState::ExecuteString(const std::string_view code, const std::string_view chunkName, std::string* outError)
    {
        if (m_State == nullptr)
        {
            const std::string error = "Lua runtime is not initialized.";
            if (outError != nullptr)
            {
                *outError = error;
            }
            LUMA_LOG_ERROR("Script", error);
            return false;
        }

        const int stackBase = lua_gettop(m_State);
        const int tracebackIndex = LuaHelpers::PushTraceback(m_State);
        const std::string chunkNameString(chunkName);

        if (luaL_loadbuffer(m_State, code.data(), code.size(), chunkNameString.c_str()) != LUA_OK)
        {
            const std::string error = LuaHelpers::PopError(m_State);
            lua_settop(m_State, stackBase);
            if (outError != nullptr)
            {
                *outError = error;
            }
            LUMA_LOG_ERROR("Script", "Failed to load chunk '" + chunkNameString + "': " + error);
            return false;
        }

        if (lua_pcall(m_State, 0, LUA_MULTRET, tracebackIndex) != LUA_OK)
        {
            const std::string error = LuaHelpers::PopError(m_State);
            lua_settop(m_State, stackBase);
            if (outError != nullptr)
            {
                *outError = error;
            }
            LUMA_LOG_ERROR("Script", "Runtime error in chunk '" + chunkNameString + "': " + error);
            return false;
        }

        lua_settop(m_State, stackBase);
        return true;
    }
}
