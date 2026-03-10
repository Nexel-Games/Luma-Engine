#include "Luma/Core/App/RenderSelection.h"

#include "Luma/RHI/RHIFactory.h"

namespace Luma
{
    namespace
    {
        bool IsAvailable(const RendererAPI api)
        {
            return IsRendererAPIAvailable(api);
        }

        RendererAPI BackendPreferenceToAPI(const BackendPreference backendPreference, const RenderPipelineProfile pipeline)
        {
            switch (backendPreference)
            {
            case BackendPreference::OpenGL:
                return RendererAPI::OpenGL;
            case BackendPreference::Auto:
            default:
                return DefaultRendererForPipeline(pipeline);
            }
        }
    }

    RendererAPI DefaultRendererForPipeline(const RenderPipelineProfile pipeline)
    {
        (void)pipeline;
        return RendererAPI::OpenGL;
    }

    RenderSelectionResult ResolveRenderSelection(
        const RenderPipelineProfile pipeline,
        const BackendPreference backendPreference)
    {
        RenderSelectionResult result;
        result.rendererAPI = BackendPreferenceToAPI(backendPreference, pipeline);
        result.available = IsAvailable(result.rendererAPI);

        if (result.available)
        {
            return result;
        }

        if (backendPreference != BackendPreference::Auto)
        {
            result.hardFailure = true;
            result.message = "Selected backend is unavailable on this system.";
            return result;
        }

        result.rendererAPI = RendererAPI::OpenGL;
        result.available = IsAvailable(result.rendererAPI);
        result.usedFallback = true;
        result.message = result.available
            ? "Requested backend is unavailable; using OpenGL."
            : "No supported renderer backend was found.";
        result.hardFailure = !result.available;
        return result;
    }
}
