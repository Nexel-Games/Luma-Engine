#pragma once

#include <filesystem>
#include <string>

namespace Luma
{
    class Scene;

    class SceneSerializer
    {
    public:
        static bool Serialize(const Scene& scene, const std::filesystem::path& scenePath, std::string& outError);
        static bool Deserialize(const std::filesystem::path& scenePath, Scene& scene, std::string& outError);
    };
}
