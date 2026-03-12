#include "Luma/Scene/Scene.h"

#include <algorithm>
#include <array>
#include <functional>

#include "Luma/Scene/IDComponent.h"
#include "Luma/Scene/RelationshipComponent.h"
#include "Luma/Scene/TagComponent.h"
#include "Luma/Scene/TransformComponent.h"

namespace Luma
{
    Entity Scene::CreateEntity()
    {
        return CreateEntity("Entity");
    }

    Entity Scene::CreateEntity(const std::string& name)
    {
        const EntityID entity = m_Registry.create();
        const UUID uuid = GenerateUUID();

        m_Registry.emplace<IDComponent>(entity, uuid);
        TagComponent tagComponent;
        tagComponent.name = name.empty() ? "Entity" : name;
        tagComponent.tag = "Untagged";
        m_Registry.emplace<TagComponent>(entity, std::move(tagComponent));
        m_Registry.emplace<TransformComponent>(entity);
        m_Registry.emplace<RelationshipComponent>(entity);
        m_EntityByUUID[uuid] = entity;

        return Entity(entity, &m_Registry);
    }

    void Scene::Clear()
    {
        m_Registry.clear();
        m_EntityByUUID.clear();
    }

    void Scene::DestroyEntity(EntityID entity)
    {
        if (!m_Registry.valid(entity))
        {
            return;
        }

        RemoveFromParent(entity);
        DestroyEntityRecursive(entity);
    }

    void Scene::Swap(Scene& other) noexcept
    {
        m_Registry.swap(other.m_Registry);
        m_EntityByUUID.swap(other.m_EntityByUUID);
    }

    void Scene::SetParent(const EntityID child, const EntityID parent)
    {
        if (child == entt::null || parent == entt::null || child == parent)
        {
            return;
        }
        if (!m_Registry.valid(child) || !m_Registry.valid(parent))
        {
            return;
        }
        if (!m_Registry.all_of<RelationshipComponent>(child) || !m_Registry.all_of<RelationshipComponent>(parent))
        {
            return;
        }
        if (IsDescendantOf(parent, child))
        {
            return;
        }

        RemoveFromParent(child);
        auto& childRel = m_Registry.get<RelationshipComponent>(child);
        auto& parentRel = m_Registry.get<RelationshipComponent>(parent);
        childRel.parent = parent;
        parentRel.children.push_back(child);
    }

    void Scene::Unparent(const EntityID child)
    {
        if (child == entt::null || !m_Registry.valid(child))
        {
            return;
        }

        RemoveFromParent(child);
    }

    std::vector<EntityID> Scene::GetRootEntities() const
    {
        std::vector<EntityID> roots;
        const auto view = m_Registry.view<RelationshipComponent>();

        for (const EntityID entity : view)
        {
            const auto& relationship = view.get<RelationshipComponent>(entity);
            if (relationship.parent == entt::null)
            {
                roots.push_back(entity);
            }
        }

        return roots;
    }

    std::vector<EntityID> Scene::GetChildren(const EntityID parent) const
    {
        if (parent == entt::null || !m_Registry.valid(parent) || !m_Registry.all_of<RelationshipComponent>(parent))
        {
            return {};
        }

        const auto& relationship = m_Registry.get<RelationshipComponent>(parent);
        return relationship.children;
    }

    void Scene::UpdateWorldTransforms()
    {
        const auto roots = GetRootEntities();
        std::function<void(EntityID, const TransformComponent*)> updateRecursive;
        updateRecursive = [this, &updateRecursive](const EntityID entity, const TransformComponent* parentWorld)
        {
            if (!m_Registry.valid(entity) ||
                !m_Registry.all_of<TransformComponent, RelationshipComponent>(entity))
            {
                return;
            }

            auto& transform = m_Registry.get<TransformComponent>(entity);

            if (parentWorld == nullptr)
            {
                transform.worldPosition = transform.position;
                transform.worldRotation = transform.rotation;
                transform.worldScale = transform.scale;
            }
            else
            {
                for (std::size_t i = 0; i < 3; ++i)
                {
                    transform.worldPosition[i] = parentWorld->worldPosition[i] + transform.position[i];
                    transform.worldRotation[i] = parentWorld->worldRotation[i] + transform.rotation[i];
                    transform.worldScale[i] = parentWorld->worldScale[i] * transform.scale[i];
                }
            }

            transform.dirty = false;

            const auto& relationship = m_Registry.get<RelationshipComponent>(entity);
            for (const EntityID child : relationship.children)
            {
                updateRecursive(child, &transform);
            }
        };

        for (const EntityID root : roots)
        {
            updateRecursive(root, nullptr);
        }
    }

    EntityID Scene::FindByUUID(const UUID id) const
    {
        const auto it = m_EntityByUUID.find(id);
        if (it == m_EntityByUUID.end())
        {
            return entt::null;
        }

        return it->second;
    }

    void Scene::SetEntityUUID(const EntityID entity, const UUID id)
    {
        if (!m_Registry.valid(entity))
        {
            return;
        }

        if (m_Registry.all_of<IDComponent>(entity))
        {
            const UUID previousId = m_Registry.get<IDComponent>(entity).id;
            m_EntityByUUID.erase(previousId);
            m_Registry.get<IDComponent>(entity).id = id;
        }
        else
        {
            m_Registry.emplace<IDComponent>(entity, id);
        }

        m_EntityByUUID[id] = entity;
    }

    void Scene::DestroyEntityRecursive(const EntityID entity)
    {
        if (!m_Registry.valid(entity))
        {
            return;
        }

        if (m_Registry.all_of<RelationshipComponent>(entity))
        {
            auto& relationship = m_Registry.get<RelationshipComponent>(entity);
            const auto children = relationship.children;
            relationship.children.clear();

            for (const EntityID child : children)
            {
                DestroyEntityRecursive(child);
            }
        }

        if (m_Registry.all_of<IDComponent>(entity))
        {
            const UUID id = m_Registry.get<IDComponent>(entity).id;
            m_EntityByUUID.erase(id);
        }

        m_Registry.destroy(entity);
    }

    void Scene::RemoveFromParent(const EntityID entity)
    {
        if (!m_Registry.valid(entity) || !m_Registry.all_of<RelationshipComponent>(entity))
        {
            return;
        }

        auto& relationship = m_Registry.get<RelationshipComponent>(entity);
        if (relationship.parent == entt::null || !m_Registry.valid(relationship.parent))
        {
            relationship.parent = entt::null;
            return;
        }

        auto& parentRelationship = m_Registry.get<RelationshipComponent>(relationship.parent);
        parentRelationship.children.erase(
            std::remove(parentRelationship.children.begin(), parentRelationship.children.end(), entity),
            parentRelationship.children.end());
        relationship.parent = entt::null;
    }

    bool Scene::IsDescendantOf(const EntityID entity, const EntityID potentialAncestor) const
    {
        if (entity == entt::null || potentialAncestor == entt::null || entity == potentialAncestor)
        {
            return entity == potentialAncestor;
        }

        EntityID current = entity;
        while (current != entt::null && m_Registry.valid(current) && m_Registry.all_of<RelationshipComponent>(current))
        {
            const auto& relationship = m_Registry.get<RelationshipComponent>(current);
            if (relationship.parent == potentialAncestor)
            {
                return true;
            }
            current = relationship.parent;
        }

        return false;
    }
}
