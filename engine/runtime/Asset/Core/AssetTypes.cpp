#include "Luma/Asset/Core/AssetTypes.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace Luma::Assets
{
    namespace
    {
        std::string ToLower(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });
            return value;
        }
    }

    std::string_view ToString(const AssetType type)
    {
        switch (type)
        {
        case AssetType::Texture2D:
            return "Texture2D";
        case AssetType::TextureCube:
            return "TextureCube";
        case AssetType::HDRI:
            return "HDRI";
        case AssetType::MaterialGraph:
            return "MaterialGraph";
        case AssetType::MaterialInstance:
            return "MaterialInstance";
        case AssetType::StaticMesh:
            return "StaticMesh";
        case AssetType::SkeletalMesh:
            return "SkeletalMesh";
        case AssetType::AnimationClip:
            return "AnimationClip";
        case AssetType::AudioClip:
            return "AudioClip";
        case AssetType::Prefab:
            return "Prefab";
        case AssetType::Scene:
            return "Scene";
        case AssetType::Procedural:
            return "Procedural";
        case AssetType::PackageManifest:
            return "PackageManifest";
        case AssetType::Unknown:
        default:
            return "Unknown";
        }
    }

    bool TryParseAssetType(const std::string_view value, AssetType& outType)
    {
        const std::string lowered = ToLower(std::string(value));
        if (lowered == "texture2d")
        {
            outType = AssetType::Texture2D;
            return true;
        }
        if (lowered == "texturecube")
        {
            outType = AssetType::TextureCube;
            return true;
        }
        if (lowered == "hdri")
        {
            outType = AssetType::HDRI;
            return true;
        }
        if (lowered == "materialgraph")
        {
            outType = AssetType::MaterialGraph;
            return true;
        }
        if (lowered == "materialinstance")
        {
            outType = AssetType::MaterialInstance;
            return true;
        }
        if (lowered == "staticmesh")
        {
            outType = AssetType::StaticMesh;
            return true;
        }
        if (lowered == "skeletalmesh")
        {
            outType = AssetType::SkeletalMesh;
            return true;
        }
        if (lowered == "animationclip")
        {
            outType = AssetType::AnimationClip;
            return true;
        }
        if (lowered == "audioclip")
        {
            outType = AssetType::AudioClip;
            return true;
        }
        if (lowered == "prefab")
        {
            outType = AssetType::Prefab;
            return true;
        }
        if (lowered == "scene")
        {
            outType = AssetType::Scene;
            return true;
        }
        if (lowered == "procedural")
        {
            outType = AssetType::Procedural;
            return true;
        }
        if (lowered == "packagemanifest")
        {
            outType = AssetType::PackageManifest;
            return true;
        }
        if (lowered == "unknown")
        {
            outType = AssetType::Unknown;
            return true;
        }

        return false;
    }
}

