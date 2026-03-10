#pragma once

#include <functional>
#include <string>

#include "Luma/Editor/Panels/Scene/EntityCreationMenu.h"
#include "Luma/Renderer/PrimitiveMeshFactory.h"
#include "Luma/Scene/Scene.h"

namespace Luma
{
    struct MaterialComponent;
    struct SkyLightComponent;
}

namespace Luma::Editor
{
    struct EntityTemplateCreationContext
    {
        Scene* scene = nullptr;
        std::function<std::string(const std::string&)> generateUniqueEntityName;
        std::function<void(EntityID, PrimitiveType)> ensurePrimitiveCollider;
        std::function<void(MaterialComponent&)> initializeDefaultMaterial;
        std::function<void(SkyLightComponent&)> initializeSkyLightDefaults;
        std::function<void(EntityID)> selectSingleEntity;
    };

    class EntityTemplateCreationService
    {
    public:
        EntityID CreateEntityFromTemplate(
            const EntityTemplateCreationContext& context,
            EntityTemplateKind templateKind,
            EntityID parentEntity = entt::null) const;
    };
}
