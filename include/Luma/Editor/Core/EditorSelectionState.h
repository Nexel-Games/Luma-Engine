#pragma once

#include <algorithm>
#include <vector>

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    class EditorSelectionState
    {
    public:
        EntityID& PrimaryRef()
        {
            return m_Primary;
        }

        const EntityID& PrimaryRef() const
        {
            return m_Primary;
        }

        std::vector<EntityID>& EntitiesRef()
        {
            return m_Entities;
        }

        const std::vector<EntityID>& EntitiesRef() const
        {
            return m_Entities;
        }

        void SelectSingle(const EntityID entity)
        {
            Clear();
            if (entity == entt::null)
            {
                return;
            }

            m_Primary = entity;
            m_Entities.push_back(entity);
        }

        void Toggle(const EntityID entity)
        {
            if (entity == entt::null)
            {
                return;
            }

            const auto selectedIt = std::find(m_Entities.begin(), m_Entities.end(), entity);
            if (selectedIt != m_Entities.end())
            {
                m_Entities.erase(selectedIt);
                if (m_Primary == entity)
                {
                    m_Primary = m_Entities.empty() ? entt::null : m_Entities.back();
                }
                return;
            }

            m_Entities.push_back(entity);
            m_Primary = entity;
        }

        void Append(const EntityID entity)
        {
            if (entity == entt::null)
            {
                return;
            }
            if (!IsSelected(entity))
            {
                m_Entities.push_back(entity);
            }
            m_Primary = entity;
        }

        bool IsSelected(const EntityID entity) const
        {
            if (entity == entt::null)
            {
                return false;
            }

            return std::find(m_Entities.begin(), m_Entities.end(), entity) != m_Entities.end();
        }

        void Clear()
        {
            m_Primary = entt::null;
            m_Entities.clear();
        }

        void Prune(const entt::registry& registry)
        {
            m_Entities.erase(
                std::remove_if(
                    m_Entities.begin(),
                    m_Entities.end(),
                    [&registry](const EntityID entity)
                    {
                        return entity == entt::null || !registry.valid(entity);
                    }),
                m_Entities.end());

            if (m_Primary != entt::null && !registry.valid(m_Primary))
            {
                m_Primary = entt::null;
            }
            if (m_Primary == entt::null && !m_Entities.empty())
            {
                m_Primary = m_Entities.back();
            }
            if (m_Primary != entt::null && !IsSelected(m_Primary))
            {
                m_Entities.push_back(m_Primary);
            }
        }

    private:
        EntityID m_Primary = entt::null;
        std::vector<EntityID> m_Entities;
    };
}
