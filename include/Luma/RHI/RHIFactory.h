#pragma once

#include <memory>

#include "Luma/RHI/IRenderBackend.h"

namespace Luma
{
    std::unique_ptr<IRenderBackend> CreateRenderBackend(RendererAPI api);
    bool IsRendererAPIAvailable(RendererAPI api);
}
