#include "Luma/RHI/RenderGraph.h"

#include <algorithm>
#include <cstddef>
#include <deque>

namespace Luma
{
    RenderGraph::PassHandle RenderGraph::AddPass(const RenderGraphPassDesc& desc, RenderGraphPassCallback callback)
    {
        if (!callback)
        {
            return InvalidPassHandle;
        }

        PassNode node;
        node.desc = desc;
        node.callback = std::move(callback);
        m_Passes.push_back(std::move(node));
        m_Compiled = false;
        return static_cast<PassHandle>(m_Passes.size());
    }

    bool RenderGraph::AddDependency(const PassHandle pass, const PassHandle dependsOn)
    {
        if (!IsHandleValid(pass, m_Passes.size()) || !IsHandleValid(dependsOn, m_Passes.size()))
        {
            return false;
        }

        if (pass == dependsOn)
        {
            return false;
        }

        PassNode& node = m_Passes[static_cast<std::size_t>(pass - 1)];
        if (std::find(node.dependencies.begin(), node.dependencies.end(), dependsOn) == node.dependencies.end())
        {
            node.dependencies.push_back(dependsOn);
            m_Compiled = false;
        }

        return true;
    }

    void RenderGraph::Clear()
    {
        m_Passes.clear();
        m_ExecutionOrder.clear();
        m_LastCompileError.clear();
        m_Compiled = false;
    }

    bool RenderGraph::Compile()
    {
        m_ExecutionOrder.clear();
        m_LastCompileError.clear();

        if (m_Passes.empty())
        {
            m_Compiled = true;
            return true;
        }

        const std::size_t passCount = m_Passes.size();
        std::vector<std::size_t> indegrees(passCount, 0);
        std::vector<std::vector<PassHandle>> outgoing(passCount);
        std::size_t enabledPassCount = 0;

        for (std::size_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            const PassNode& passNode = m_Passes[passIndex];
            if (!passNode.desc.enabled)
            {
                continue;
            }

            ++enabledPassCount;

            for (const PassHandle dependencyHandle : passNode.dependencies)
            {
                if (!IsHandleValid(dependencyHandle, passCount))
                {
                    m_LastCompileError =
                        "RenderGraph compile failed: pass \"" + passNode.desc.debugName + "\" has an invalid dependency handle.";
                    m_Compiled = false;
                    return false;
                }

                const std::size_t dependencyIndex = static_cast<std::size_t>(dependencyHandle - 1);
                if (!m_Passes[dependencyIndex].desc.enabled)
                {
                    continue;
                }

                ++indegrees[passIndex];
                outgoing[dependencyIndex].push_back(static_cast<PassHandle>(passIndex + 1));
            }
        }

        std::deque<PassHandle> readyQueue;
        readyQueue.clear();
        for (std::size_t passIndex = 0; passIndex < passCount; ++passIndex)
        {
            if (!m_Passes[passIndex].desc.enabled)
            {
                continue;
            }

            if (indegrees[passIndex] == 0)
            {
                readyQueue.push_back(static_cast<PassHandle>(passIndex + 1));
            }
        }

        while (!readyQueue.empty())
        {
            const PassHandle readyHandle = readyQueue.front();
            readyQueue.pop_front();

            m_ExecutionOrder.push_back(readyHandle);
            const std::size_t readyIndex = static_cast<std::size_t>(readyHandle - 1);

            for (const PassHandle dependentHandle : outgoing[readyIndex])
            {
                const std::size_t dependentIndex = static_cast<std::size_t>(dependentHandle - 1);
                if (indegrees[dependentIndex] > 0)
                {
                    --indegrees[dependentIndex];
                    if (indegrees[dependentIndex] == 0)
                    {
                        readyQueue.push_back(dependentHandle);
                    }
                }
            }
        }

        if (m_ExecutionOrder.size() != enabledPassCount)
        {
            m_LastCompileError = "RenderGraph compile failed: cyclic dependency detected.";
            m_Compiled = false;
            return false;
        }

        m_Compiled = true;
        return true;
    }

    bool RenderGraph::Execute(const RenderGraphContext& context)
    {
        if (!m_Compiled && !Compile())
        {
            return false;
        }

        for (const PassHandle passHandle : m_ExecutionOrder)
        {
            if (!IsHandleValid(passHandle, m_Passes.size()))
            {
                continue;
            }

            const PassNode& node = m_Passes[static_cast<std::size_t>(passHandle - 1)];
            if (!node.desc.enabled || !node.callback)
            {
                continue;
            }

            node.callback(context);
        }

        return true;
    }

    bool RenderGraph::IsCompiled() const
    {
        return m_Compiled;
    }

    const std::string& RenderGraph::GetLastCompileError() const
    {
        return m_LastCompileError;
    }

    const std::vector<RenderGraph::PassHandle>& RenderGraph::GetExecutionOrder() const
    {
        return m_ExecutionOrder;
    }

    std::size_t RenderGraph::GetPassCount() const
    {
        return m_Passes.size();
    }

    bool RenderGraph::IsHandleValid(const PassHandle handle, const std::size_t passCount)
    {
        return handle != InvalidPassHandle && static_cast<std::size_t>(handle) <= passCount;
    }
}
