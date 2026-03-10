#pragma once

#include <string>
#include <utility>

namespace Luma
{
    class IRenderBackend;

    class Layer
    {
    public:
        explicit Layer(std::string debugName) : m_DebugName(std::move(debugName))
        {
        }

        virtual ~Layer() = default;

        virtual void OnAttach()
        {
        }

        virtual void OnDetach()
        {
        }

        virtual void OnUpdate(float deltaTimeSeconds)
        {
            (void)deltaTimeSeconds;
        }

        virtual void OnImGuiRender()
        {
        }

        virtual void OnRender(IRenderBackend& renderer)
        {
            (void)renderer;
        }

        virtual bool OnCloseRequested()
        {
            return true;
        }

        const std::string& GetDebugName() const
        {
            return m_DebugName;
        }

    private:
        std::string m_DebugName;
    };
}
