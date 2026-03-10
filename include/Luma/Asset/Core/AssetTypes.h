#pragma once

#include <string_view>

#include "Luma/Core/Foundation/UUID.h"

namespace Luma::Assets
{
    using AssetID = UUID;

    enum class AssetType
    {
        Unknown = 0,
        Texture2D,
        TextureCube,
        HDRI,
        MaterialGraph,
        MaterialInstance,
        StaticMesh,
        SkeletalMesh,
        AnimationClip,
        AudioClip,
        Prefab,
        Scene,
        Procedural,
        PackageManifest
    };

    std::string_view ToString(AssetType type);
    bool TryParseAssetType(std::string_view value, AssetType& outType);
}

