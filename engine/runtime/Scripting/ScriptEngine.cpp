#include "Luma/Scripting/ScriptEngine.h"

#include <utility>

#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Scripting/Lua/LuaState.h"

namespace Luma
{
    std::unique_ptr<LuaState> ScriptEngine::s_State;

    bool ScriptEngine::Initialize()
    {
        if (s_State != nullptr)
        {
            return true;
        }

        s_State = std::make_unique<LuaState>();
        if (!s_State->Initialize())
        {
            s_State.reset();
            return false;
        }

        return true;
    }

    void ScriptEngine::Shutdown()
    {
        if (s_State == nullptr)
        {
            return;
        }

        s_State->Shutdown();
        s_State.reset();
    }

    bool ScriptEngine::IsInitialized()
    {
        return s_State != nullptr && s_State->IsInitialized();
    }

    void ScriptEngine::RefreshPackagePaths()
    {
        if (s_State == nullptr)
        {
            return;
        }

        s_State->ConfigurePackagePaths();
    }

    bool ScriptEngine::ExecuteFile(const std::filesystem::path& path, std::string* outError)
    {
        if (s_State == nullptr)
        {
            const std::string error = "ScriptEngine is not initialized.";
            if (outError != nullptr)
            {
                *outError = error;
            }
            LUMA_LOG_ERROR("Script", error);
            return false;
        }

        return s_State->ExecuteFile(path, outError);
    }

    bool ScriptEngine::ExecuteString(const std::string_view code, const std::string_view chunkName, std::string* outError)
    {
        if (s_State == nullptr)
        {
            const std::string error = "ScriptEngine is not initialized.";
            if (outError != nullptr)
            {
                *outError = error;
            }
            LUMA_LOG_ERROR("Script", error);
            return false;
        }

        return s_State->ExecuteString(code, chunkName, outError);
    }

    lua_State* ScriptEngine::GetLuaState()
    {
        if (s_State == nullptr)
        {
            return nullptr;
        }

        return s_State->GetRawState();
    }
}
