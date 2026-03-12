#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct HierarchyPanelContext
    {
        Scene* scene = nullptr;
        void* panelIconTexture = nullptr;
        void* createIconTexture = nullptr;
        void* cameraIconTexture = nullptr;
        void* cubeIconTexture = nullptr;
        void* planeIconTexture = nullptr;
        void* sphereIconTexture = nullptr;
        void* cylinderIconTexture = nullptr;
        std::function<void(EntityID)> drawEntityCreationMenu;
        std::function<bool(EntityID)> isEntitySelected;
        std::function<bool(EntityID)> isEntityHidden;
        std::function<void(EntityID)> selectSingleEntity;
        std::function<void(EntityID)> toggleEntitySelection;
        std::function<void()> pruneSelection;
        std::function<void()> markSceneRenderCacheDirty;
        std::function<EntityID(const std::filesystem::path&, EntityID)> createEntityFromMeshAsset;
        std::function<bool(const std::filesystem::path&)> isMeshAssetPathCandidate;
        std::function<void(std::string)> setContentStatus;
    };

    class HierarchyPanel
    {
    public:
        void Draw(bool* open, const HierarchyPanelContext& context);

    private:
        bool EntityMatchesFilter(const HierarchyPanelContext& context, EntityID entity, const std::string& filterLower) const;
        void DrawEntityNode(
            const HierarchyPanelContext& context,
            EntityID entity,
            EntityID& pendingDeleteEntity,
            const std::string& filterLower);

        std::string m_SearchQuery;
    };
}
