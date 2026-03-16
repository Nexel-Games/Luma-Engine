#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "Luma/Editor/Assets/MeshAssetPickerService.h"
#include "Luma/Editor/Panels/Assets/MeshAssetPickerPanel.h"
#include "Luma/Renderer/PrimitiveMeshFactory.h"
#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct InspectorMeshRendererPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
        std::function<std::vector<std::filesystem::path>()> listContentRoots;
        std::function<void(EntityID, PrimitiveType)> ensurePrimitiveCollider;
        std::function<void()> markSceneRenderCacheDirty;
        std::function<void()> markSceneGeometryDirty;
        std::function<void()> markSceneMaterialsDirty;
    };

    class InspectorMeshRendererPanel
    {
    public:
        void Draw(const InspectorMeshRendererPanelContext& context);

    private:
        MeshAssetPickerService m_MeshAssetPickerService;
        MeshAssetPickerPanel m_MeshAssetPickerPanel;
    };
}
