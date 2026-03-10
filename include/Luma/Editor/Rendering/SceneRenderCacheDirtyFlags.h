#pragma once

#include <cstdint>

namespace Luma::Editor
{
    enum class SceneRenderCacheDirtyFlags : std::uint32_t
    {
        None = 0,
        Geometry = 1u << 0,
        Materials = 1u << 1,
        Environment = 1u << 2,
        All = Geometry | Materials | Environment
    };

    constexpr SceneRenderCacheDirtyFlags operator|(
        const SceneRenderCacheDirtyFlags lhs,
        const SceneRenderCacheDirtyFlags rhs)
    {
        return static_cast<SceneRenderCacheDirtyFlags>(
            static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(rhs));
    }

    constexpr SceneRenderCacheDirtyFlags operator&(
        const SceneRenderCacheDirtyFlags lhs,
        const SceneRenderCacheDirtyFlags rhs)
    {
        return static_cast<SceneRenderCacheDirtyFlags>(
            static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(rhs));
    }

    inline SceneRenderCacheDirtyFlags& operator|=(
        SceneRenderCacheDirtyFlags& lhs,
        const SceneRenderCacheDirtyFlags rhs)
    {
        lhs = lhs | rhs;
        return lhs;
    }

    constexpr bool HasAnySceneRenderCacheDirtyFlags(
        const SceneRenderCacheDirtyFlags value,
        const SceneRenderCacheDirtyFlags flags)
    {
        return static_cast<std::uint32_t>(value & flags) != 0;
    }
}
