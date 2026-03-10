#include "Luma/Core/App/LayerStack.h"

#include <cstddef>

namespace Luma
{
    LayerStack::~LayerStack()
    {
        Clear();
    }

    void LayerStack::PushLayer(std::unique_ptr<Layer> layer)
    {
        layer->OnAttach();
        m_Layers.emplace(m_Layers.begin() + static_cast<std::ptrdiff_t>(m_LayerInsertIndex), std::move(layer));
        ++m_LayerInsertIndex;
    }

    void LayerStack::PushOverlay(std::unique_ptr<Layer> overlay)
    {
        overlay->OnAttach();
        m_Layers.emplace_back(std::move(overlay));
    }

    void LayerStack::Clear()
    {
        for (auto it = m_Layers.rbegin(); it != m_Layers.rend(); ++it)
        {
            (*it)->OnDetach();
        }
        m_Layers.clear();
        m_LayerInsertIndex = 0;
    }
}
