#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <string>

#include "Luma/Editor/Rendering/SceneRenderCacheDirtyFlags.h"
#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct SceneRenderCacheStateContext
    {
        const Scene* scene = nullptr;
        bool viewportGridEnabled = false;
        const std::filesystem::path* skyboxSourcePath = nullptr;
        SceneRenderCacheDirtyFlags* renderSceneCacheDirtyFlags = nullptr;
        bool* lastViewportGridEnabled = nullptr;
        std::string* lastSkyMeshSignature = nullptr;
        std::function<EntityID()> findPrimarySkyEntity;
    };

    class SceneRenderCacheStateService
    {
    public:
        std::string BuildActiveSkyMeshSignature(const SceneRenderCacheStateContext& context) const;
        void FinalizeBuild(SceneRenderCacheStateContext& context) const;
    };
}
