#pragma once

#include <array>
#include <limits>
#include <string>
#include <vector>

#include "Luma/Renderer/PrimitiveMeshFactory.h"

namespace Luma
{
    struct MeshRendererComponent
    {
        bool visible = true;
        bool usePrimitive = true;
        PrimitiveType primitive = PrimitiveType::Cube;
        std::string meshSource;
        std::string importedSceneSource;
        std::vector<std::string> materialOverrides;
        std::uint32_t meshPartIndex = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t meshLod = 0;
        bool autoStreamLod = true;
        std::uint32_t maxAutoLod = 3;
        float lodNearDistance = 12.0f;
        float lodFarDistance = 120.0f;
        bool streamSectionsByDistance = true;
        float sectionLoadDistance = 180.0f;
        std::array<float, 4> color { 1.0f, 1.0f, 1.0f, 1.0f };
    };
}
