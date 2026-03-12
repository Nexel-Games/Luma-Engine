#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "Luma/Scene/Scene.h"

namespace Luma
{
    struct LuaScriptComponent;

    class LuaScriptRuntime
    {
    public:
        LuaScriptRuntime() = default;
        ~LuaScriptRuntime();

        LuaScriptRuntime(const LuaScriptRuntime&) = delete;
        LuaScriptRuntime& operator=(const LuaScriptRuntime&) = delete;

        void Start(Scene& scene);
        void Update(Scene& scene, float deltaTimeSeconds);
        void Stop();

        bool IsRunning() const
        {
            return m_Running;
        }

        const std::string* FindLastError(EntityID entity) const;

    private:
        struct ScriptDefinition
        {
            int tableRef = -2;
            std::filesystem::path sourcePath;
        };

        struct ScriptInstance
        {
            EntityID entity = entt::null;
            std::filesystem::path assetPath;
            std::filesystem::path sourcePath;
            int tableRef = -2;
            bool enabled = true;
            bool faulted = false;
        };

        void SyncInstances(Scene& scene);
        bool CreateInstance(Scene& scene, EntityID entity, const LuaScriptComponent& component);
        void DestroyInstance(Scene& scene, EntityID entity, bool callOnDestroy);
        bool EnsureScriptDefinition(const std::filesystem::path& assetPath, ScriptDefinition& outDefinition);
        bool CallMethod(Scene& scene, ScriptInstance& instance, const char* methodName, float optionalNumberArg, bool passNumberArg);
        std::filesystem::path ResolveScriptSourcePath(const std::filesystem::path& assetPath, std::string& outError) const;
        std::string NormalizePathKey(const std::filesystem::path& path) const;
        void ClearDefinitions();

        bool m_Running = false;
        Scene* m_Scene = nullptr;
        std::unordered_map<EntityID, ScriptInstance> m_Instances;
        std::unordered_map<std::string, ScriptDefinition> m_Definitions;
        std::unordered_map<EntityID, std::string> m_LastErrors;
    };
}
