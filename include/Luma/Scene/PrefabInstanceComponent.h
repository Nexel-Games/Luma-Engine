#pragma once

#include <string>

#include "Luma/Scene/UUID.h"

namespace Luma
{
    struct PrefabInstanceComponent
    {
        std::string prefabAsset;
        UUID sourceEntityId = 0;
        bool isRoot = false;
    };
}
