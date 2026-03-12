#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

struct lua_State;

namespace Luma
{
    class LuaState;

    class ScriptEngine
    {
    public:
        static bool Initialize();
        static void Shutdown();
        static bool IsInitialized();

        static void RefreshPackagePaths();

        static bool ExecuteFile(const std::filesystem::path& path, std::string* outError = nullptr);
        static bool ExecuteString(std::string_view code, std::string_view chunkName, std::string* outError = nullptr);

        static lua_State* GetLuaState();

    private:
        static std::unique_ptr<LuaState> s_State;
    };
}
