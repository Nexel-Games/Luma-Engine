#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Core/Foundation/UUID.h"
#include "Luma/Editor/Content/ContentBrowserHostFacadeService.h"
#include "Luma/Editor/Core/EditorSelectionState.h"
#include "Luma/Editor/Core/EditorStatusState.h"
#include "Luma/Editor/Rendering/SceneRenderCacheDirtyFlags.h"
#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct PrefabWorkflowContext
    {
        Scene* scene = nullptr;
        EditorStatusState* editorStatus = nullptr;
        EditorSelectionState* selectionState = nullptr;
        std::filesystem::path* selectedContentEntry = nullptr;
        const std::filesystem::path* currentContentDirectory = nullptr;
        float timeSeconds = 0.0f;
        bool playModeActive = false;
        bool sceneDirty = false;
        ContentBrowserHostFacadeService* contentBrowserHostFacadeService = nullptr;
        std::function<ContentBrowserHostFacadeContext()> buildContentBrowserContext;
        std::function<void(SceneRenderCacheDirtyFlags)> markSceneRenderCacheDirty;
        std::function<std::vector<UUID>()> captureSelectedEntityUuids;
        std::function<void(const std::vector<UUID>&, UUID)> restoreSelectedEntityUuids;
        std::function<void()> updateSceneDirtyState;
        std::function<void(EntityID)> selectSingleEntity;
    };

    class PrefabWorkflowService final
    {
    public:
        std::filesystem::path BuildUniquePrefabAssetPath(
            const PrefabWorkflowContext& context,
            std::string_view baseName);
        EntityID FindPrefabInstanceRoot(
            const PrefabWorkflowContext& context,
            EntityID entity) const;
        void MarkPrefabInstanceHierarchy(
            const PrefabWorkflowContext& context,
            EntityID rootEntity,
            const std::string& prefabAsset) const;
        bool CreatePrefabFromEntity(
            PrefabWorkflowContext& context,
            EntityID rootEntity);
        EntityID InstantiatePrefabAsset(
            PrefabWorkflowContext& context,
            const std::filesystem::path& prefabPath,
            EntityID parentEntity);
        bool ApplyPrefabInstance(
            PrefabWorkflowContext& context,
            EntityID entity);
        bool RevertPrefabInstance(
            PrefabWorkflowContext& context,
            EntityID entity);
        std::string GetPrefabInstanceStatus(
            PrefabWorkflowContext& context,
            EntityID entity);
        std::vector<std::string> GetPrefabOverridePaths(
            PrefabWorkflowContext& context,
            EntityID entity);
        bool RevertPrefabComponent(
            PrefabWorkflowContext& context,
            EntityID entity,
            std::string_view componentPath);
        bool RevertPrefabOverridePath(
            PrefabWorkflowContext& context,
            EntityID entity,
            std::string_view overridePath);
        void SelectPrefabAsset(
            PrefabWorkflowContext& context,
            EntityID entity);

    private:
        EntityID m_PrefabStatusCacheRootEntity = entt::null;
        std::string m_PrefabStatusCachePrefabAsset;
        std::string m_PrefabStatusCacheValue;
        float m_PrefabStatusCacheTimeSeconds = -1000.0f;
        bool m_PrefabStatusCacheSceneDirty = false;
        EntityID m_PrefabOverrideCacheEntity = entt::null;
        std::string m_PrefabOverrideCachePrefabAsset;
        UUID m_PrefabOverrideCacheSourceEntityId = 0;
        std::vector<std::string> m_PrefabOverrideCacheValue;
        float m_PrefabOverrideCacheTimeSeconds = -1000.0f;
        bool m_PrefabOverrideCacheSceneDirty = false;
    };
}
