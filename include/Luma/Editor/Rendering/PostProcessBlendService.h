#pragma once

#include <array>

#include "Luma/Core/App/RenderPipeline.h"
#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    class PostProcessBlendService
    {
    public:
        void BuildBlendedView(
            const Scene& scene,
            const std::array<float, 3>& cameraWorldPosition,
            ScenePostProcessView& outPostProcess) const;
    };
}
