#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

#include "Luma/Core/App/RenderPipeline.h"
#include "Luma/Scene/MaterialComponent.h"

namespace Luma::Editor
{
    struct MaterialRenderProxyCacheContext
    {
        std::function<void(MaterialComponent&)> initializeDefaultMaterial;
        std::function<bool(const std::filesystem::path&, MaterialComponent&, std::string&)> loadMaterialAsset;
        std::function<MaterialRenderProxy(const MaterialComponent&, const std::filesystem::path&)> buildProxy;
    };

    class MaterialRenderProxyCacheService
    {
    public:
        bool TryGetProxy(
            const MaterialRenderProxyCacheContext& context,
            const std::filesystem::path& assetPath,
            MaterialRenderProxy& outProxy);
        void Invalidate(const std::filesystem::path& assetPath);
        void Clear();

    private:
        struct CacheEntry
        {
            std::filesystem::file_time_type lastWriteTime {};
            MaterialRenderProxy proxy;
            bool valid = false;
        };

        std::unordered_map<std::string, CacheEntry> m_Cache;
    };
}
