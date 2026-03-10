#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Luma
{
    class IRenderBackend;

    struct RenderGraphContext
    {
        IRenderBackend& renderer;
        float timeSeconds = 0.0f;
        std::uint64_t frameIndex = 0;
    };

    struct RenderGraphPassDesc
    {
        std::string debugName;
        bool enabled = true;
    };

    using RenderGraphPassCallback = std::function<void(const RenderGraphContext&)>;

    class RenderGraph
    {
    public:
        using PassHandle = std::uint32_t;
        static constexpr PassHandle InvalidPassHandle = 0;

        PassHandle AddPass(const RenderGraphPassDesc& desc, RenderGraphPassCallback callback);
        bool AddDependency(PassHandle pass, PassHandle dependsOn);

        void Clear();
        bool Compile();
        bool Execute(const RenderGraphContext& context);

        bool IsCompiled() const;
        const std::string& GetLastCompileError() const;
        const std::vector<PassHandle>& GetExecutionOrder() const;
        std::size_t GetPassCount() const;

    private:
        struct PassNode
        {
            RenderGraphPassDesc desc;
            RenderGraphPassCallback callback;
            std::vector<PassHandle> dependencies;
        };

        static bool IsHandleValid(PassHandle handle, std::size_t passCount);

        std::vector<PassNode> m_Passes;
        std::vector<PassHandle> m_ExecutionOrder;
        std::string m_LastCompileError;
        bool m_Compiled = false;
    };
}
