#pragma once

#include <memory>
#include <vector>

#include "Luma/Core/App/Layer.h"

namespace Luma
{
    class LayerStack
    {
    public:
        LayerStack() = default;
        ~LayerStack();

        LayerStack(const LayerStack&) = delete;
        LayerStack& operator=(const LayerStack&) = delete;
        LayerStack(LayerStack&&) = delete;
        LayerStack& operator=(LayerStack&&) = delete;

        void PushLayer(std::unique_ptr<Layer> layer);
        void PushOverlay(std::unique_ptr<Layer> overlay);
        void Clear();

        auto begin()
        {
            return m_Layers.begin();
        }

        auto end()
        {
            return m_Layers.end();
        }

        auto begin() const
        {
            return m_Layers.begin();
        }

        auto end() const
        {
            return m_Layers.end();
        }

        auto rbegin()
        {
            return m_Layers.rbegin();
        }

        auto rend()
        {
            return m_Layers.rend();
        }

        auto rbegin() const
        {
            return m_Layers.rbegin();
        }

        auto rend() const
        {
            return m_Layers.rend();
        }

    private:
        std::vector<std::unique_ptr<Layer>> m_Layers;
        std::size_t m_LayerInsertIndex = 0;
    };
}
