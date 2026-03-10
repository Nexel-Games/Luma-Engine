#pragma once

#include <functional>

#include "Luma/Renderer/PrimitiveMeshFactory.h"
#include "Luma/Scene/Scene.h"

namespace Luma
{
    struct MaterialComponent;
    struct PostProcessComponent;
    struct SkyLightComponent;
}

namespace Luma::Editor
{
    struct InspectorAddComponentPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
        std::function<void(MaterialComponent&)> initializeDefaultMaterial;
        std::function<void(SkyLightComponent&)> initializeSkyLightDefaults;
        std::function<void(PostProcessComponent&)> initializePostProcessDefaults;
        std::function<void(EntityID, PrimitiveType)> ensurePrimitiveCollider;
        std::function<void()> markSceneGeometryDirty;
        std::function<void()> markSceneMaterialsDirty;
        std::function<void()> markSceneEnvironmentDirty;
    };

    class InspectorAddComponentPanel
    {
    public:
        void Draw(const InspectorAddComponentPanelContext& context);
    };
}
