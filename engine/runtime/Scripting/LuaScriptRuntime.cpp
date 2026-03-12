#include "Luma/Scripting/LuaScriptRuntime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <lua.hpp>

#include "Luma/Asset/Core/AssetMetaIO.h"
#include "Luma/Core/App/Project.h"
#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Core/Foundation/Time.h"
#include "Luma/Input/Input.h"
#include "Luma/Scene/LuaScriptComponent.h"
#include "Luma/Scene/TagComponent.h"
#include "Luma/Scene/TransformComponent.h"
#include "Luma/Scripting/ScriptEngine.h"
#include "Luma/Scripting/Lua/LuaHelpers.h"

namespace Luma
{
    namespace
    {
        Scene* g_ActiveScriptScene = nullptr;

        std::string ToLowerString(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }

        bool TryReadVec3Argument(lua_State* state, const int startIndex, std::array<float, 3>& outValue)
        {
            if (lua_istable(state, startIndex))
            {
                lua_getfield(state, startIndex, "x");
                lua_getfield(state, startIndex, "y");
                lua_getfield(state, startIndex, "z");
                if (!lua_isnumber(state, -3) || !lua_isnumber(state, -2) || !lua_isnumber(state, -1))
                {
                    lua_pop(state, 3);
                    return false;
                }

                outValue[0] = static_cast<float>(lua_tonumber(state, -3));
                outValue[1] = static_cast<float>(lua_tonumber(state, -2));
                outValue[2] = static_cast<float>(lua_tonumber(state, -1));
                lua_pop(state, 3);
                return true;
            }

            if (!lua_isnumber(state, startIndex) ||
                !lua_isnumber(state, startIndex + 1) ||
                !lua_isnumber(state, startIndex + 2))
            {
                return false;
            }

            outValue[0] = static_cast<float>(lua_tonumber(state, startIndex));
            outValue[1] = static_cast<float>(lua_tonumber(state, startIndex + 1));
            outValue[2] = static_cast<float>(lua_tonumber(state, startIndex + 2));
            return true;
        }

        void PushVec3(lua_State* state, const std::array<float, 3>& value)
        {
            lua_createtable(state, 0, 3);
            lua_pushnumber(state, value[0]);
            lua_setfield(state, -2, "x");
            lua_pushnumber(state, value[1]);
            lua_setfield(state, -2, "y");
            lua_pushnumber(state, value[2]);
            lua_setfield(state, -2, "z");
        }

        bool TryGetEntityFromLua(lua_State* state, const int index, EntityID& outEntity)
        {
            if (!lua_isinteger(state, index))
            {
                return false;
            }

            outEntity = static_cast<EntityID>(static_cast<entt::id_type>(lua_tointeger(state, index)));
            return true;
        }

        bool IsSceneEntityValid(const EntityID entity)
        {
            return g_ActiveScriptScene != nullptr && g_ActiveScriptScene->GetRegistry().valid(entity);
        }

        std::optional<KeyCode> ParseKeyCode(const std::string_view value)
        {
            const std::string lowered = ToLowerString(std::string(value));
            if (lowered == "w")
            {
                return KeyCode::W;
            }
            if (lowered == "a")
            {
                return KeyCode::A;
            }
            if (lowered == "s")
            {
                return KeyCode::S;
            }
            if (lowered == "d")
            {
                return KeyCode::D;
            }
            if (lowered == "q")
            {
                return KeyCode::Q;
            }
            if (lowered == "e")
            {
                return KeyCode::E;
            }
            if (lowered == "space")
            {
                return KeyCode::Space;
            }
            if (lowered == "enter")
            {
                return KeyCode::Enter;
            }
            if (lowered == "escape")
            {
                return KeyCode::Escape;
            }
            if (lowered == "leftshift" || lowered == "shift")
            {
                return KeyCode::LeftShift;
            }
            if (lowered == "rightshift")
            {
                return KeyCode::RightShift;
            }
            if (lowered == "c")
            {
                return KeyCode::C;
            }
            if (lowered == "r")
            {
                return KeyCode::R;
            }
            return std::nullopt;
        }

        int LuaTimeGetDeltaSeconds(lua_State* state)
        {
            lua_pushnumber(state, Time::GetDeltaSeconds());
            return 1;
        }

        int LuaTimeGetElapsedSeconds(lua_State* state)
        {
            lua_pushnumber(state, Time::GetElapsedSeconds());
            return 1;
        }

        int LuaInputIsKeyDown(lua_State* state)
        {
            const std::string key = LuaHelpers::ToString(state, 1);
            if (const auto parsedKey = ParseKeyCode(key))
            {
                lua_pushboolean(state, Input::IsKeyDown(*parsedKey));
                return 1;
            }
            lua_pushboolean(state, 0);
            return 1;
        }

        int LuaInputWasKeyPressed(lua_State* state)
        {
            const std::string key = LuaHelpers::ToString(state, 1);
            if (const auto parsedKey = ParseKeyCode(key))
            {
                lua_pushboolean(state, Input::WasKeyPressed(*parsedKey));
                return 1;
            }
            lua_pushboolean(state, 0);
            return 1;
        }

        int LuaEntityIsValid(lua_State* state)
        {
            EntityID entity = entt::null;
            lua_pushboolean(state, TryGetEntityFromLua(state, 1, entity) && IsSceneEntityValid(entity));
            return 1;
        }

        int LuaEntityGetName(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushliteral(state, "");
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<TagComponent>(entity))
            {
                lua_pushliteral(state, "");
                return 1;
            }

            const std::string& name = registry.get<TagComponent>(entity).name;
            lua_pushlstring(state, name.c_str(), name.size());
            return 1;
        }

        int LuaTransformGetPosition(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<TransformComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            PushVec3(state, registry.get<TransformComponent>(entity).position);
            return 1;
        }

        int LuaTransformSetPosition(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            std::array<float, 3> value {};
            if (!TryReadVec3Argument(state, 2, value))
            {
                return 0;
            }

            auto& transform = g_ActiveScriptScene->GetRegistry().get<TransformComponent>(entity);
            transform.position = value;
            transform.dirty = true;
            return 0;
        }

        int LuaTransformTranslate(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            std::array<float, 3> delta {};
            if (!TryReadVec3Argument(state, 2, delta))
            {
                return 0;
            }

            auto& transform = g_ActiveScriptScene->GetRegistry().get<TransformComponent>(entity);
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                transform.position[axis] += delta[axis];
            }
            transform.dirty = true;
            return 0;
        }

        int LuaTransformGetRotation(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<TransformComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            PushVec3(state, registry.get<TransformComponent>(entity).rotation);
            return 1;
        }

        int LuaTransformSetRotation(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            std::array<float, 3> value {};
            if (!TryReadVec3Argument(state, 2, value))
            {
                return 0;
            }

            auto& transform = g_ActiveScriptScene->GetRegistry().get<TransformComponent>(entity);
            transform.rotation = value;
            transform.dirty = true;
            return 0;
        }

        int LuaTransformGetScale(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<TransformComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            PushVec3(state, registry.get<TransformComponent>(entity).scale);
            return 1;
        }

        int LuaTransformSetScale(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            std::array<float, 3> value {};
            if (!TryReadVec3Argument(state, 2, value))
            {
                return 0;
            }

            auto& transform = g_ActiveScriptScene->GetRegistry().get<TransformComponent>(entity);
            transform.scale = value;
            transform.dirty = true;
            return 0;
        }

        void RegisterRuntimeTables(lua_State* state)
        {
            lua_getglobal(state, "Luma");
            if (!lua_istable(state, -1))
            {
                lua_pop(state, 1);
                lua_newtable(state);
            }

            lua_newtable(state);
            lua_pushcfunction(state, LuaTimeGetDeltaSeconds);
            lua_setfield(state, -2, "GetDeltaSeconds");
            lua_pushcfunction(state, LuaTimeGetElapsedSeconds);
            lua_setfield(state, -2, "GetElapsedSeconds");
            lua_setfield(state, -2, "Time");

            lua_newtable(state);
            lua_pushcfunction(state, LuaInputIsKeyDown);
            lua_setfield(state, -2, "IsKeyDown");
            lua_pushcfunction(state, LuaInputWasKeyPressed);
            lua_setfield(state, -2, "WasKeyPressed");
            lua_setfield(state, -2, "Input");

            lua_newtable(state);
            lua_pushcfunction(state, LuaEntityIsValid);
            lua_setfield(state, -2, "IsValid");
            lua_pushcfunction(state, LuaEntityGetName);
            lua_setfield(state, -2, "GetName");
            lua_setfield(state, -2, "Entity");

            lua_newtable(state);
            lua_pushcfunction(state, LuaTransformGetPosition);
            lua_setfield(state, -2, "GetPosition");
            lua_pushcfunction(state, LuaTransformSetPosition);
            lua_setfield(state, -2, "SetPosition");
            lua_pushcfunction(state, LuaTransformTranslate);
            lua_setfield(state, -2, "Translate");
            lua_pushcfunction(state, LuaTransformGetRotation);
            lua_setfield(state, -2, "GetRotation");
            lua_pushcfunction(state, LuaTransformSetRotation);
            lua_setfield(state, -2, "SetRotation");
            lua_pushcfunction(state, LuaTransformGetScale);
            lua_setfield(state, -2, "GetScale");
            lua_pushcfunction(state, LuaTransformSetScale);
            lua_setfield(state, -2, "SetScale");
            lua_setfield(state, -2, "Transform");

            lua_setglobal(state, "Luma");
        }

        void CloneTableShallow(lua_State* state, const int sourceIndex)
        {
            const int absoluteSourceIndex = lua_absindex(state, sourceIndex);
            lua_newtable(state);
            lua_pushnil(state);
            while (lua_next(state, absoluteSourceIndex) != 0)
            {
                lua_pushvalue(state, -2);
                lua_insert(state, -2);
                lua_settable(state, -4);
            }
        }
    }

    LuaScriptRuntime::~LuaScriptRuntime()
    {
        Stop();
    }

    void LuaScriptRuntime::Start(Scene& scene)
    {
        Stop();

        if (!ScriptEngine::IsInitialized() && !ScriptEngine::Initialize())
        {
            LUMA_LOG_ERROR("Script", "LuaScriptRuntime failed to start because ScriptEngine did not initialize.");
            return;
        }

        ScriptEngine::RefreshPackagePaths();
        m_Running = true;
        m_Scene = &scene;

        if (lua_State* state = ScriptEngine::GetLuaState(); state != nullptr)
        {
            RegisterRuntimeTables(state);
        }

        SyncInstances(scene);
    }

    void LuaScriptRuntime::Update(Scene& scene, const float deltaTimeSeconds)
    {
        if (!m_Running)
        {
            return;
        }

        m_Scene = &scene;
        SyncInstances(scene);

        std::vector<EntityID> orderedEntities;
        orderedEntities.reserve(m_Instances.size());
        for (const auto& [entity, instance] : m_Instances)
        {
            if (instance.enabled && !instance.faulted)
            {
                orderedEntities.push_back(entity);
            }
        }

        for (const EntityID entity : orderedEntities)
        {
            auto instanceIt = m_Instances.find(entity);
            if (instanceIt == m_Instances.end())
            {
                continue;
            }

            ScriptInstance& instance = instanceIt->second;
            g_ActiveScriptScene = &scene;
            const bool succeeded = CallMethod(scene, instance, "OnUpdate", deltaTimeSeconds, true);
            g_ActiveScriptScene = nullptr;
            if (!succeeded)
            {
                instance.faulted = true;
            }
        }
    }

    void LuaScriptRuntime::Stop()
    {
        if (!m_Running)
        {
            m_Instances.clear();
            ClearDefinitions();
            m_LastErrors.clear();
            m_Scene = nullptr;
            return;
        }

        Scene* scene = m_Scene;
        if (scene != nullptr)
        {
            std::vector<EntityID> entities;
            entities.reserve(m_Instances.size());
            for (const auto& [entity, instance] : m_Instances)
            {
                (void)instance;
                entities.push_back(entity);
            }

            for (const EntityID entity : entities)
            {
                DestroyInstance(*scene, entity, true);
            }
        }
        else
        {
            if (lua_State* state = ScriptEngine::GetLuaState(); state != nullptr)
            {
                for (auto& [entity, instance] : m_Instances)
                {
                    (void)entity;
                    if (instance.tableRef != LUA_NOREF)
                    {
                        luaL_unref(state, LUA_REGISTRYINDEX, instance.tableRef);
                    }
                }
            }
            m_Instances.clear();
        }

        ClearDefinitions();
        m_LastErrors.clear();
        m_Scene = nullptr;
        m_Running = false;
        g_ActiveScriptScene = nullptr;
    }

    const std::string* LuaScriptRuntime::FindLastError(const EntityID entity) const
    {
        const auto it = m_LastErrors.find(entity);
        return it != m_LastErrors.end() ? &it->second : nullptr;
    }

    void LuaScriptRuntime::SyncInstances(Scene& scene)
    {
        auto& registry = scene.GetRegistry();

        std::vector<EntityID> staleEntities;
        staleEntities.reserve(m_Instances.size());
        for (const auto& [entity, instance] : m_Instances)
        {
            (void)instance;
            if (!registry.valid(entity) || !registry.all_of<LuaScriptComponent>(entity))
            {
                staleEntities.push_back(entity);
                continue;
            }

            const LuaScriptComponent& component = registry.get<LuaScriptComponent>(entity);
            if (component.scriptAsset.empty())
            {
                staleEntities.push_back(entity);
            }
        }

        for (const EntityID entity : staleEntities)
        {
            DestroyInstance(scene, entity, true);
        }

        const auto view = registry.view<LuaScriptComponent>();
        for (const EntityID entity : view)
        {
            const LuaScriptComponent& component = view.get<LuaScriptComponent>(entity);
            if (component.scriptAsset.empty())
            {
                continue;
            }

            const auto instanceIt = m_Instances.find(entity);
            if (instanceIt == m_Instances.end())
            {
                CreateInstance(scene, entity, component);
                continue;
            }

            ScriptInstance& instance = instanceIt->second;
            instance.enabled = component.enabled;
            if (instance.assetPath.generic_string() != std::filesystem::path(component.scriptAsset).generic_string())
            {
                DestroyInstance(scene, entity, true);
                CreateInstance(scene, entity, component);
            }
        }
    }

    bool LuaScriptRuntime::CreateInstance(Scene& scene, const EntityID entity, const LuaScriptComponent& component)
    {
        lua_State* state = ScriptEngine::GetLuaState();
        if (state == nullptr)
        {
            m_LastErrors[entity] = "ScriptEngine is not initialized.";
            return false;
        }

        ScriptDefinition definition {};
        const std::filesystem::path assetPath(component.scriptAsset);
        if (!EnsureScriptDefinition(assetPath, definition))
        {
            m_LastErrors[entity] = "Failed to resolve script definition.";
            return false;
        }

        lua_rawgeti(state, LUA_REGISTRYINDEX, definition.tableRef);
        if (!lua_istable(state, -1))
        {
            lua_pop(state, 1);
            m_LastErrors[entity] = "Script definition did not resolve to a Lua table.";
            return false;
        }

        lua_newtable(state);
        lua_newtable(state);
        lua_pushvalue(state, -3);
        lua_setfield(state, -2, "__index");
        lua_setmetatable(state, -2);

        lua_pushinteger(state, static_cast<lua_Integer>(static_cast<entt::id_type>(entity)));
        lua_setfield(state, -2, "entity");

        lua_getfield(state, -2, "Properties");
        if (lua_istable(state, -1))
        {
            CloneTableShallow(state, -1);
            lua_setfield(state, -3, "Properties");
        }
        lua_pop(state, 1);

        const int instanceRef = luaL_ref(state, LUA_REGISTRYINDEX);
        lua_pop(state, 1);

        ScriptInstance instance {};
        instance.entity = entity;
        instance.assetPath = assetPath;
        instance.sourcePath = definition.sourcePath;
        instance.tableRef = instanceRef;
        instance.enabled = component.enabled;
        instance.faulted = false;

        m_Instances[entity] = instance;
        m_LastErrors.erase(entity);

        g_ActiveScriptScene = &scene;
        const bool created = CallMethod(scene, m_Instances[entity], "OnCreate", 0.0f, false);
        g_ActiveScriptScene = nullptr;
        if (!created)
        {
            m_Instances[entity].faulted = true;
            return false;
        }

        return true;
    }

    void LuaScriptRuntime::DestroyInstance(Scene& scene, const EntityID entity, const bool callOnDestroy)
    {
        const auto instanceIt = m_Instances.find(entity);
        if (instanceIt == m_Instances.end())
        {
            m_LastErrors.erase(entity);
            return;
        }

        lua_State* state = ScriptEngine::GetLuaState();
        if (callOnDestroy)
        {
            g_ActiveScriptScene = &scene;
            CallMethod(scene, instanceIt->second, "OnDestroy", 0.0f, false);
            g_ActiveScriptScene = nullptr;
        }

        if (state != nullptr && instanceIt->second.tableRef != LUA_NOREF)
        {
            luaL_unref(state, LUA_REGISTRYINDEX, instanceIt->second.tableRef);
        }

        m_Instances.erase(instanceIt);
        m_LastErrors.erase(entity);
    }

    bool LuaScriptRuntime::EnsureScriptDefinition(const std::filesystem::path& assetPath, ScriptDefinition& outDefinition)
    {
        lua_State* state = ScriptEngine::GetLuaState();
        if (state == nullptr)
        {
            return false;
        }

        const std::string key = NormalizePathKey(assetPath);
        if (const auto it = m_Definitions.find(key); it != m_Definitions.end())
        {
            outDefinition = it->second;
            return true;
        }

        std::string resolveError;
        const std::filesystem::path sourcePath = ResolveScriptSourcePath(assetPath, resolveError);
        if (sourcePath.empty())
        {
            LUMA_LOG_ERROR("Script", resolveError);
            return false;
        }

        const int stackBase = lua_gettop(state);
        const int tracebackIndex = LuaHelpers::PushTraceback(state);
        const std::string sourcePathString = sourcePath.string();

        if (luaL_loadfile(state, sourcePathString.c_str()) != LUA_OK)
        {
            const std::string error = LuaHelpers::PopError(state);
            lua_settop(state, stackBase);
            LUMA_LOG_ERROR("Script", "Failed to load Lua script '" + sourcePathString + "': " + error);
            return false;
        }

        if (lua_pcall(state, 0, 1, tracebackIndex) != LUA_OK)
        {
            const std::string error = LuaHelpers::PopError(state);
            lua_settop(state, stackBase);
            LUMA_LOG_ERROR("Script", "Failed to execute Lua script '" + sourcePathString + "': " + error);
            return false;
        }

        if (!lua_istable(state, -1))
        {
            lua_settop(state, stackBase);
            LUMA_LOG_ERROR("Script", "Lua script must return a table: " + sourcePathString);
            return false;
        }

        ScriptDefinition definition {};
        definition.sourcePath = sourcePath;
        definition.tableRef = luaL_ref(state, LUA_REGISTRYINDEX);
        m_Definitions.emplace(key, definition);
        outDefinition = definition;
        lua_settop(state, stackBase);
        return true;
    }

    bool LuaScriptRuntime::CallMethod(
        Scene& scene,
        ScriptInstance& instance,
        const char* methodName,
        const float optionalNumberArg,
        const bool passNumberArg)
    {
        lua_State* state = ScriptEngine::GetLuaState();
        if (state == nullptr)
        {
            return false;
        }

        const int stackBase = lua_gettop(state);
        const int tracebackIndex = LuaHelpers::PushTraceback(state);
        lua_rawgeti(state, LUA_REGISTRYINDEX, instance.tableRef);
        if (!lua_istable(state, -1))
        {
            lua_settop(state, stackBase);
            return false;
        }

        lua_getfield(state, -1, methodName);
        if (lua_isnil(state, -1))
        {
            lua_settop(state, stackBase);
            return true;
        }

        if (!lua_isfunction(state, -1))
        {
            const std::string error = std::string(methodName) + " exists but is not a function.";
            m_LastErrors[instance.entity] = error;
            lua_settop(state, stackBase);
            LUMA_LOG_ERROR("Script", error);
            return false;
        }

        lua_pushvalue(state, -2);
        int argumentCount = 1;
        if (passNumberArg)
        {
            lua_pushnumber(state, optionalNumberArg);
            ++argumentCount;
        }

        if (lua_pcall(state, argumentCount, 0, tracebackIndex) != LUA_OK)
        {
            const std::string error = LuaHelpers::PopError(state);
            std::string entityName = "Entity";
            auto& registry = scene.GetRegistry();
            if (registry.valid(instance.entity) && registry.all_of<TagComponent>(instance.entity))
            {
                entityName = registry.get<TagComponent>(instance.entity).name;
            }

            const std::string message =
                "Lua error in " + instance.sourcePath.string() +
                " [" + entityName + "::" + methodName + "]: " + error;
            m_LastErrors[instance.entity] = message;
            lua_settop(state, stackBase);
            LUMA_LOG_ERROR("Script", message);
            return false;
        }

        lua_settop(state, stackBase);
        return true;
    }

    std::filesystem::path LuaScriptRuntime::ResolveScriptSourcePath(
        const std::filesystem::path& assetPath,
        std::string& outError) const
    {
        outError.clear();
        if (assetPath.empty())
        {
            outError = "Lua script asset path is empty.";
            return {};
        }

        std::filesystem::path resolvedPath = assetPath;
        if (!resolvedPath.is_absolute())
        {
            if (Project::IsLoaded())
            {
                resolvedPath = Project::GetProjectRoot() / resolvedPath;
            }
            else
            {
                resolvedPath = std::filesystem::current_path() / resolvedPath;
            }
        }
        resolvedPath = resolvedPath.lexically_normal();

        const std::string extension = ToLowerString(resolvedPath.extension().string());
        if (extension == ".lua")
        {
            if (!std::filesystem::exists(resolvedPath))
            {
                outError = "Lua script file does not exist: " + resolvedPath.string();
                return {};
            }
            return resolvedPath;
        }

        if (extension == ".lumascript")
        {
            Assets::AssetMeta meta {};
            std::string metaError;
            if (!Assets::ReadMetaFile(resolvedPath.string() + ".meta", meta, metaError))
            {
                outError = "Failed to read Lua script asset metadata: " + metaError;
                return {};
            }
            if (meta.sourcePaths.empty())
            {
                outError = "Lua script asset has no source path: " + resolvedPath.string();
                return {};
            }

            std::filesystem::path sourcePath = meta.sourcePaths.front();
            if (!sourcePath.is_absolute())
            {
                if (Project::IsLoaded())
                {
                    sourcePath = Project::GetProjectRoot() / sourcePath;
                }
                else
                {
                    sourcePath = std::filesystem::current_path() / sourcePath;
                }
            }
            sourcePath = sourcePath.lexically_normal();
            if (!std::filesystem::exists(sourcePath))
            {
                outError = "Lua script source file does not exist: " + sourcePath.string();
                return {};
            }

            return sourcePath;
        }

        outError = "Unsupported Lua script reference: " + assetPath.string();
        return {};
    }

    std::string LuaScriptRuntime::NormalizePathKey(const std::filesystem::path& path) const
    {
        std::string key = path.lexically_normal().generic_string();
#if defined(_WIN32)
        std::transform(
            key.begin(),
            key.end(),
            key.begin(),
            [](const unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
#endif
        return key;
    }

    void LuaScriptRuntime::ClearDefinitions()
    {
        lua_State* state = ScriptEngine::GetLuaState();
        if (state != nullptr)
        {
            for (auto& [key, definition] : m_Definitions)
            {
                (void)key;
                if (definition.tableRef != LUA_NOREF)
                {
                    luaL_unref(state, LUA_REGISTRYINDEX, definition.tableRef);
                }
            }
        }
        m_Definitions.clear();
    }
}
