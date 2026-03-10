#include "Luma/RHI/RHIFactory.h"

#include "RHI/OpenGLRenderBackend.h"

namespace Luma
{
    std::unique_ptr<IRenderBackend> CreateRenderBackend(const RendererAPI api)
    {
        if (api == RendererAPI::OpenGL)
        {
            return std::make_unique<OpenGLRenderBackend>();
        }

        return nullptr;
    }

    bool IsRendererAPIAvailable(const RendererAPI api)
    {
        return api == RendererAPI::OpenGL;
    }
}
