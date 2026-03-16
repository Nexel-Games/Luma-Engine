#pragma once

#include <functional>

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    enum class EntityTemplateKind
    {
        Empty = 0,
        Camera,
        DirectionalLight,
        PointLight,
        SkyLight,
        Player,
        AudioSource,
        Cube,
        Plane,
        Sphere,
        Cylinder,
        Capsule,
        Cone,
        Torus
    };

    struct EntityCreationMenuContext
    {
        EntityID parentEntity = entt::null;
        std::function<void(EntityTemplateKind, EntityID)> createTemplate;
    };

    class EntityCreationMenu
    {
    public:
        void Draw(const EntityCreationMenuContext& context);
    };
}
