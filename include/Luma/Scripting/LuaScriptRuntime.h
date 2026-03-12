#pragma once

#include <filesystem>
#include <system_error>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "Luma/Physics/PhysicsTypes.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scripting/ScriptProperty.h"

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
        void RunFixedUpdates(Scene& scene, std::uint32_t stepCount, float fixedDeltaTimeSeconds);
        void DispatchPhysicsCallbacks(Scene& scene, const std::vector<PhysicsEvent>& events);
        void Stop();

        bool IsRunning() const
        {
            return m_Running;
        }

        bool HasInstance(EntityID entity) const;
        bool IsFaulted(EntityID entity) const;
        const std::filesystem::path* FindInstanceSourcePath(EntityID entity) const;
        const std::string* FindLastError(EntityID entity) const;
        bool SetProperty(EntityID entity, const std::string& propertyName, const ScriptValue& value);

    private:
        struct ScriptDefinition
        {
            int tableRef = -2;
            std::filesystem::path assetPath;
            std::filesystem::path sourcePath;
            std::filesystem::file_time_type lastWriteTime {};
            bool hasWriteTime = false;
        };

        struct ScriptInstance
        {
            EntityID entity = entt::null;
            std::filesystem::path assetPath;
            std::filesystem::path sourcePath;
            int tableRef = -2;
            bool enabled = true;
            bool faulted = false;
            bool started = false;
        };

        void SyncInstances(Scene& scene);
        void ProcessHotReloads(Scene& scene);
        bool IsHotReloadEnabled() const;
        bool CreateInstance(Scene& scene, EntityID entity, const LuaScriptComponent& component);
        void DestroyInstance(Scene& scene, EntityID entity, bool callOnDestroy);
        bool EnsureScriptDefinition(const std::filesystem::path& assetPath, ScriptDefinition& outDefinition);
        bool CallMethod(Scene& scene, ScriptInstance& instance, const char* methodName, float optionalNumberArg, bool passNumberArg);
        bool CallMethod(Scene& scene, ScriptInstance& instance, const char* methodName, EntityID optionalEntityArg, bool passEntityArg);
        std::string NormalizePathKey(const std::filesystem::path& path) const;
        void ClearDefinitions();

        bool m_Running = false;
        Scene* m_Scene = nullptr;
        std::unordered_map<EntityID, ScriptInstance> m_Instances;
        std::unordered_map<std::string, ScriptDefinition> m_Definitions;
        std::unordered_map<EntityID, std::string> m_LastErrors;
    };
}
