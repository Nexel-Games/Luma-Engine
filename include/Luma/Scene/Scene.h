#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "Luma/Scene/UUID.h"

namespace Luma
{
    using EntityID = entt::entity;

    class Entity
    {
    public:
        Entity() = default;
        Entity(EntityID handle, entt::registry* registry)
            : m_EntityHandle(handle), m_Registry(registry)
        {
        }

        template<typename T, typename... Args>
        T& AddComponent(Args&&... args)
        {
            return m_Registry->emplace<T>(m_EntityHandle, std::forward<Args>(args)...);
        }

        template<typename T, typename... Args>
        T& AddOrReplaceComponent(Args&&... args)
        {
            return m_Registry->emplace_or_replace<T>(m_EntityHandle, std::forward<Args>(args)...);
        }

        template<typename T>
        T& GetComponent()
        {
            return m_Registry->get<T>(m_EntityHandle);
        }

        template<typename T>
        const T& GetComponent() const
        {
            return m_Registry->get<T>(m_EntityHandle);
        }

        template<typename T>
        bool HasComponent() const
        {
            return m_Registry->all_of<T>(m_EntityHandle);
        }

        template<typename T>
        void RemoveComponent()
        {
            m_Registry->remove<T>(m_EntityHandle);
        }

        EntityID GetHandle() const
        {
            return m_EntityHandle;
        }

        explicit operator bool() const
        {
            return m_Registry != nullptr && m_EntityHandle != entt::null;
        }

        bool operator==(const Entity& other) const
        {
            return m_EntityHandle == other.m_EntityHandle && m_Registry == other.m_Registry;
        }

        bool operator!=(const Entity& other) const
        {
            return !(*this == other);
        }

    private:
        EntityID m_EntityHandle = entt::null;
        entt::registry* m_Registry = nullptr;
    };

    class Scene
    {
    public:
        Scene() = default;
        ~Scene() = default;

        Entity CreateEntity();
        Entity CreateEntity(const std::string& name);
        void Clear();
        void DestroyEntity(EntityID entity);
        void Swap(Scene& other) noexcept;

        void SetParent(EntityID child, EntityID parent);
        void Unparent(EntityID child);

        std::vector<EntityID> GetRootEntities() const;
        std::vector<EntityID> GetChildren(EntityID parent) const;
        void UpdateWorldTransforms();

        EntityID FindByUUID(UUID id) const;
        void SetEntityUUID(EntityID entity, UUID id);

        entt::registry& GetRegistry()
        {
            return m_Registry;
        }

        const entt::registry& GetRegistry() const
        {
            return m_Registry;
        }

    private:
        void DestroyEntityRecursive(EntityID entity);
        void RemoveFromParent(EntityID entity);
        bool IsDescendantOf(EntityID entity, EntityID potentialAncestor) const;

        entt::registry m_Registry;
        std::unordered_map<UUID, EntityID> m_EntityByUUID;
    };
}
