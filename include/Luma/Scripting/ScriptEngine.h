#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <system_error>

#include "Luma/Scripting/ScriptProperty.h"

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

        static bool ResolveScriptSourcePath(
            const std::filesystem::path& assetPath,
            std::filesystem::path& outSourcePath,
            std::string* outError = nullptr);
        static const LuaScriptAssetMetadata* GetScriptMetadata(
            const std::filesystem::path& assetPath,
            std::string* outError = nullptr);
        static void InvalidateScriptMetadata(const std::filesystem::path& assetPath);
        static void ClearScriptMetadata();

        static lua_State* GetLuaState();

    private:
        struct CachedScriptMetadata
        {
            LuaScriptAssetMetadata metadata;
            std::string error;
            std::filesystem::path sourcePath;
            std::filesystem::file_time_type lastWriteTime {};
            bool loaded = false;
            bool hasWriteTime = false;
        };

        static std::unique_ptr<LuaState> s_State;
        static std::unordered_map<std::string, CachedScriptMetadata> s_MetadataCache;
    };
}
