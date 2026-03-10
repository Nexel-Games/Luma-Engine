#include "Luma/Editor/Scene/SceneBootstrapService.h"

#include "Luma/Scene/DirectionalLightComponent.h"
#include "Luma/Scene/IDComponent.h"
#include "Luma/Scene/SkyLightComponent.h"

namespace Luma::Editor
{
    bool SceneBootstrapService::IsSelectionValid(const SceneBootstrapContext& context) const
    {
        return context.scene != nullptr &&
            context.selectedEntity != entt::null &&
            context.scene->GetRegistry().valid(context.selectedEntity) &&
            context.isEntitySelected &&
            context.isEntitySelected(context.selectedEntity);
    }

    void SceneBootstrapService::SeedDefaultSceneEntities(SceneBootstrapContext& context) const
    {
        if (context.scene == nullptr)
        {
            return;
        }

        const auto entities = context.scene->GetRegistry().view<IDComponent>();
        if (!entities.empty())
        {
            if (!IsSelectionValid(context))
            {
                const auto roots = context.scene->GetRootEntities();
                if (roots.empty())
                {
                    if (context.clearEntitySelection)
                    {
                        context.clearEntitySelection();
                    }
                }
                else if (context.selectSingleEntity)
                {
                    context.selectSingleEntity(roots.front());
                }
            }
            return;
        }

        Entity skyLightEntity = context.scene->CreateEntity("Sky Light");
        auto& skyLight = skyLightEntity.AddComponent<SkyLightComponent>();
        if (context.initializeSkyLightDefaults)
        {
            context.initializeSkyLightDefaults(skyLight);
        }

        Entity directionalLight = context.scene->CreateEntity("Directional Light");
        directionalLight.AddComponent<DirectionalLightComponent>();

        if (context.resetViewportCamera)
        {
            context.resetViewportCamera();
        }
        if (context.selectSingleEntity)
        {
            context.selectSingleEntity(skyLightEntity.GetHandle());
        }
    }
}
