#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Luma/RHI/IRenderBackend.h"

namespace Luma
{
    class GPUResourceManager
    {
    public:
        enum class DestroyMode
        {
            Immediate = 0,
            Deferred
        };

        struct Stats
        {
            std::size_t renderPassCount = 0;
            std::size_t pipelineCount = 0;
            std::size_t meshCount = 0;
            std::size_t textureCount = 0;
            std::size_t renderTargetCount = 0;
            std::size_t framebufferCount = 0;
            std::size_t descriptorSetCount = 0;
            std::size_t pendingDestroyCount = 0;
        };

        struct DebugEntry
        {
            ResourceHandle handle = InvalidResourceHandle;
            std::string typeLabel;
            std::string debugName;
            bool pendingDestroy = false;
            std::uint64_t createdFrame = 0;
            std::uint64_t retireFrame = 0;
        };

        void BeginFrame(IRenderBackend& backend);
        void EndFrame();
        void Shutdown();

        RenderPassHandle CreateRenderPass(const RenderPassDesc& desc);
        PipelineStateHandle CreatePipelineState(const PipelineStateDesc& desc);
        MeshHandle CreateMesh(const MeshDesc& desc, std::string_view debugName = {});
        TextureHandle CreateTexture(const TextureDesc& desc, std::string_view debugName = {});
        void UpdateTexture(TextureHandle handle, const TextureDesc& desc);
        RenderTargetHandle CreateRenderTarget(const RenderTargetDesc& desc, std::string_view debugName = {});
        FramebufferHandle CreateFramebuffer(const FramebufferDesc& desc, std::string_view debugName = {});
        DescriptorSetHandle CreateDescriptorSet(
            PipelineStateHandle pipeline,
            const DescriptorSetDesc& desc,
            std::string_view debugName = {});
        void UpdateDescriptorSet(DescriptorSetHandle handle, const DescriptorSetDesc& desc);

        void DestroyRenderPass(RenderPassHandle handle, DestroyMode mode = DestroyMode::Deferred);
        void DestroyPipelineState(PipelineStateHandle handle, DestroyMode mode = DestroyMode::Deferred);
        void DestroyMesh(MeshHandle handle, DestroyMode mode = DestroyMode::Deferred);
        void DestroyTexture(TextureHandle handle, DestroyMode mode = DestroyMode::Deferred);
        void DestroyRenderTarget(RenderTargetHandle handle, DestroyMode mode = DestroyMode::Deferred);
        void DestroyFramebuffer(FramebufferHandle handle, DestroyMode mode = DestroyMode::Deferred);
        void DestroyDescriptorSet(DescriptorSetHandle handle, DestroyMode mode = DestroyMode::Deferred);

        void SetDeferredReleaseFrames(std::uint64_t frameCount);
        Stats GetStats() const;
        std::vector<DebugEntry> GetDebugEntries() const;

    private:
        enum class ResourceType
        {
            RenderPass = 0,
            PipelineState,
            Mesh,
            Texture,
            RenderTarget,
            Framebuffer,
            DescriptorSet
        };

        struct PendingDestroy
        {
            ResourceType type = ResourceType::RenderPass;
            ResourceHandle handle = InvalidResourceHandle;
            std::uint64_t retireFrame = 0;
        };

        struct ResourceRecord
        {
            ResourceType type = ResourceType::RenderPass;
            std::string debugName;
            std::uint64_t createdFrame = 0;
            bool pendingDestroy = false;
            std::uint64_t retireFrame = 0;
        };

        void AttachBackend(IRenderBackend& backend);
        void QueueDestroy(ResourceType type, ResourceHandle handle);
        void DestroyNow(ResourceType type, ResourceHandle handle);
        void DrainDeferred(std::uint64_t upToFrameInclusive);
        void DestroyAllTrackedNow();
        static std::string TypeLabel(ResourceType type);
        void TrackResource(ResourceType type, ResourceHandle handle, std::string_view debugName);

        IRenderBackend* m_Backend = nullptr;
        std::uint64_t m_FrameIndex = 0;
        std::uint64_t m_DeferredReleaseFrames = 2;
        std::vector<PendingDestroy> m_PendingDestroy;

        std::unordered_set<ResourceHandle> m_RenderPasses;
        std::unordered_set<ResourceHandle> m_Pipelines;
        std::unordered_set<ResourceHandle> m_Meshes;
        std::unordered_set<ResourceHandle> m_Textures;
        std::unordered_set<ResourceHandle> m_RenderTargets;
        std::unordered_set<ResourceHandle> m_Framebuffers;
        std::unordered_set<ResourceHandle> m_DescriptorSets;
        std::unordered_map<ResourceHandle, ResourceRecord> m_Records;
    };
}
