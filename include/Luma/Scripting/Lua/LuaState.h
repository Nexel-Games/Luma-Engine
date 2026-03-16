#pragma once

#include <filesystem>
#include <string>
#include <string_view>

struct lua_State;

namespace Luma
{
    class LuaState
    {
    public:
        LuaState() = default;
        ~LuaState();

        LuaState(const LuaState&) = delete;
        LuaState& operator=(const LuaState&) = delete;
        LuaState(LuaState&&) = delete;
        LuaState& operator=(LuaState&&) = delete;

        bool Initialize();
        void Shutdown();
        bool IsInitialized() const;

        void RegisterCoreBindings();
        void ConfigurePackagePaths();

        bool ExecuteFile(const std::filesystem::path& path, std::string* outError = nullptr);
        bool ExecuteString(std::string_view code, std::string_view chunkName, std::string* outError = nullptr);

        lua_State* GetRawState() const
        {
            return m_State;
        }

    private:
        lua_State* m_State = nullptr;
    };
}
