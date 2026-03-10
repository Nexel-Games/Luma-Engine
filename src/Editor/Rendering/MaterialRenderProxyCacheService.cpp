#include "Luma/Editor/Rendering/MaterialRenderProxyCacheService.h"

#include <system_error>

namespace Luma::Editor
{
    bool MaterialRenderProxyCacheService::TryGetProxy(
        const MaterialRenderProxyCacheContext& context,
        const std::filesystem::path& assetPath,
        MaterialRenderProxy& outProxy)
    {
        if (assetPath.empty() ||
            !context.initializeDefaultMaterial ||
            !context.loadMaterialAsset ||
            !context.buildProxy)
        {
            return false;
        }

        std::error_code errorCode;
        const bool exists = std::filesystem::exists(assetPath, errorCode);
        if (errorCode || !exists)
        {
            Invalidate(assetPath);
            return false;
        }

        const std::filesystem::file_time_type lastWriteTime = std::filesystem::last_write_time(assetPath, errorCode);
        if (errorCode)
        {
            return false;
        }

        const std::string cacheKey = assetPath.generic_string();
        if (CacheEntry* entry = [&]() -> CacheEntry*
            {
                const auto it = m_Cache.find(cacheKey);
                return it == m_Cache.end() ? nullptr : &it->second;
            }();
            entry != nullptr && entry->lastWriteTime == lastWriteTime)
        {
            if (!entry->valid)
            {
                return false;
            }

            outProxy = entry->proxy;
            return true;
        }

        MaterialComponent material {};
        context.initializeDefaultMaterial(material);
        material.sharedMaterial = cacheKey;
        std::string loadError;
        if (!context.loadMaterialAsset(assetPath, material, loadError))
        {
            CacheEntry& entry = m_Cache[cacheKey];
            entry.lastWriteTime = lastWriteTime;
            entry.valid = false;
            return false;
        }

        CacheEntry& entry = m_Cache[cacheKey];
        entry.lastWriteTime = lastWriteTime;
        entry.proxy = context.buildProxy(material, assetPath);
        entry.valid = true;
        outProxy = entry.proxy;
        return true;
    }

    void MaterialRenderProxyCacheService::Invalidate(const std::filesystem::path& assetPath)
    {
        if (assetPath.empty())
        {
            return;
        }

        m_Cache.erase(assetPath.generic_string());
    }

    void MaterialRenderProxyCacheService::Clear()
    {
        m_Cache.clear();
    }
}
