#pragma once

#include <string>

#include "Luma/Core/App/Project.h"
#include "Luma/RHI/RendererAPI.h"

namespace Luma
{
    struct RenderSelectionResult
    {
        RendererAPI rendererAPI = RendererAPI::OpenGL;
        bool available = false;
        bool usedFallback = false;
        bool hardFailure = false;
        std::string message;
    };

    RendererAPI DefaultRendererForPipeline(RenderPipelineProfile pipeline);
    RenderSelectionResult ResolveRenderSelection(RenderPipelineProfile pipeline, BackendPreference backendPreference);
}
