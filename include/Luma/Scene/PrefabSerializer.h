#pragma once

#include <filesystem>
#include <string>

#include "Luma/Scene/Scene.h"

namespace Luma
{
    class PrefabSerializer
    {
    public:
        static bool SerializePrefab(
            const Scene& sourceScene,
            EntityID rootEntity,
            const std::filesystem::path& prefabPath,
            std::string& outError);

        static bool InstantiatePrefab(
            Scene& targetScene,
            const std::filesystem::path& prefabPath,
            EntityID* outRootEntity,
            std::string& outError);
    };
}
