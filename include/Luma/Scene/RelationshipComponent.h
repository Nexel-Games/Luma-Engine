#pragma once

#include <vector>

#include <entt/entt.hpp>

namespace Luma
{
    struct RelationshipComponent
    {
        entt::entity parent = entt::null;
        std::vector<entt::entity> children;
    };
}
