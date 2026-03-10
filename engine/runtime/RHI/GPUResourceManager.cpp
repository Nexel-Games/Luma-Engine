#include "Luma/RHI/GPUResourceManager.h"

#include <algorithm>

namespace Luma
{
    namespace
    {
        bool RemoveTrackedHandle(std::unordered_set<ResourceHandle>& tracked, const ResourceHandle handle)
        {
            const auto it = tracked.find(handle);
            if (it == tracked.end())
            {
                return false;
            }

            tracked.erase(it);
            return true;
        }
    }

    void GPUResourceManager::BeginFrame(IRenderBackend& backend)
    {
        AttachBackend(backend);
        DrainDeferred(m_FrameIndex);
    }

    void GPUResourceManager::EndFrame()
    {
        ++m_FrameIndex;
        DrainDeferred(m_FrameIndex);
    }

    void GPUResourceManager::Shutdown()
    {
        if (m_Backend != nullptr)
        {
            m_Backend->WaitIdle();
        }

        for (const PendingDestroy& pending : m_PendingDestroy)
        {
            DestroyNow(pending.type, pending.handle);
        }
        m_PendingDestroy.clear();

        DestroyAllTrackedNow();
        m_Records.clear();
        m_FrameIndex = 0;
        m_Backend = nullptr;
    }

    RenderPassHandle GPUResourceManager::CreateRenderPass(const RenderPassDesc& desc)
    {
        if (m_Backend == nullptr)
        {
            return InvalidResourceHandle;
        }

        const RenderPassHandle handle = m_Backend->CreateRenderPass(desc);
        if (handle != InvalidResourceHandle)
        {
            m_RenderPasses.insert(handle);
            TrackResource(ResourceType::RenderPass, handle, desc.debugName);
        }
        return handle;
    }

    PipelineStateHandle GPUResourceManager::CreatePipelineState(const PipelineStateDesc& desc)
    {
        if (m_Backend == nullptr)
        {
            return InvalidResourceHandle;
        }

        const PipelineStateHandle handle = m_Backend->CreatePipelineState(desc);
        if (handle != InvalidResourceHandle)
        {
            m_Pipelines.insert(handle);
            TrackResource(ResourceType::PipelineState, handle, desc.debugName);
        }
        return handle;
    }

    MeshHandle GPUResourceManager::CreateMesh(const MeshDesc& desc, const std::string_view debugName)
    {
        if (m_Backend == nullptr)
        {
            return InvalidResourceHandle;
        }

        const MeshHandle handle = m_Backend->CreateMesh(desc);
        if (handle != InvalidResourceHandle)
        {
            m_Meshes.insert(handle);
            TrackResource(ResourceType::Mesh, handle, debugName);
        }
        return handle;
    }

    TextureHandle GPUResourceManager::CreateTexture(const TextureDesc& desc, const std::string_view debugName)
    {
        if (m_Backend == nullptr)
        {
            return InvalidResourceHandle;
        }

        const TextureHandle handle = m_Backend->CreateTexture(desc);
        if (handle != InvalidResourceHandle)
        {
            m_Textures.insert(handle);
            TrackResource(ResourceType::Texture, handle, debugName.empty() ? desc.debugName : debugName);
        }
        return handle;
    }

    void GPUResourceManager::UpdateTexture(const TextureHandle handle, const TextureDesc& desc)
    {
        if (m_Backend == nullptr || handle == InvalidResourceHandle)
        {
            return;
        }

        if (!m_Textures.contains(handle))
        {
            return;
        }

        m_Backend->UpdateTexture(handle, desc);
    }

    RenderTargetHandle GPUResourceManager::CreateRenderTarget(const RenderTargetDesc& desc, const std::string_view debugName)
    {
        if (m_Backend == nullptr)
        {
            return InvalidResourceHandle;
        }

        const RenderTargetHandle handle = m_Backend->CreateRenderTarget(desc);
        if (handle != InvalidResourceHandle)
        {
            m_RenderTargets.insert(handle);
            TrackResource(ResourceType::RenderTarget, handle, debugName.empty() ? desc.debugName : debugName);
        }
        return handle;
    }

    FramebufferHandle GPUResourceManager::CreateFramebuffer(const FramebufferDesc& desc, const std::string_view debugName)
    {
        if (m_Backend == nullptr)
        {
            return InvalidResourceHandle;
        }

        const FramebufferHandle handle = m_Backend->CreateFramebuffer(desc);
        if (handle != InvalidResourceHandle)
        {
            m_Framebuffers.insert(handle);
            TrackResource(ResourceType::Framebuffer, handle, debugName.empty() ? desc.debugName : debugName);
        }
        return handle;
    }

    DescriptorSetHandle GPUResourceManager::CreateDescriptorSet(
        const PipelineStateHandle pipeline,
        const DescriptorSetDesc& desc,
        const std::string_view debugName)
    {
        if (m_Backend == nullptr)
        {
            return InvalidResourceHandle;
        }

        const DescriptorSetHandle handle = m_Backend->CreateDescriptorSet(pipeline, desc);
        if (handle != InvalidResourceHandle)
        {
            m_DescriptorSets.insert(handle);
            TrackResource(ResourceType::DescriptorSet, handle, debugName);
        }
        return handle;
    }

    void GPUResourceManager::UpdateDescriptorSet(const DescriptorSetHandle handle, const DescriptorSetDesc& desc)
    {
        if (m_Backend == nullptr || handle == InvalidResourceHandle)
        {
            return;
        }

        if (!m_DescriptorSets.contains(handle))
        {
            return;
        }

        m_Backend->UpdateDescriptorSet(handle, desc);
    }

    void GPUResourceManager::DestroyRenderPass(const RenderPassHandle handle, const DestroyMode mode)
    {
        if (handle == InvalidResourceHandle)
        {
            return;
        }

        if (!RemoveTrackedHandle(m_RenderPasses, handle))
        {
            return;
        }

        if (mode == DestroyMode::Deferred)
        {
            QueueDestroy(ResourceType::RenderPass, handle);
            return;
        }

        DestroyNow(ResourceType::RenderPass, handle);
    }

    void GPUResourceManager::DestroyPipelineState(const PipelineStateHandle handle, const DestroyMode mode)
    {
        if (handle == InvalidResourceHandle)
        {
            return;
        }

        if (!RemoveTrackedHandle(m_Pipelines, handle))
        {
            return;
        }

        if (mode == DestroyMode::Deferred)
        {
            QueueDestroy(ResourceType::PipelineState, handle);
            return;
        }

        DestroyNow(ResourceType::PipelineState, handle);
    }

    void GPUResourceManager::DestroyMesh(const MeshHandle handle, const DestroyMode mode)
    {
        if (handle == InvalidResourceHandle)
        {
            return;
        }

        if (!RemoveTrackedHandle(m_Meshes, handle))
        {
            return;
        }

        if (mode == DestroyMode::Deferred)
        {
            QueueDestroy(ResourceType::Mesh, handle);
            return;
        }

        DestroyNow(ResourceType::Mesh, handle);
    }

    void GPUResourceManager::DestroyDescriptorSet(const DescriptorSetHandle handle, const DestroyMode mode)
    {
        if (handle == InvalidResourceHandle)
        {
            return;
        }

        if (!RemoveTrackedHandle(m_DescriptorSets, handle))
        {
            return;
        }

        if (mode == DestroyMode::Deferred)
        {
            QueueDestroy(ResourceType::DescriptorSet, handle);
            return;
        }

        DestroyNow(ResourceType::DescriptorSet, handle);
    }

    void GPUResourceManager::DestroyTexture(const TextureHandle handle, const DestroyMode mode)
    {
        if (handle == InvalidResourceHandle)
        {
            return;
        }

        if (!RemoveTrackedHandle(m_Textures, handle))
        {
            return;
        }

        if (mode == DestroyMode::Deferred)
        {
            QueueDestroy(ResourceType::Texture, handle);
            return;
        }

        DestroyNow(ResourceType::Texture, handle);
    }

    void GPUResourceManager::DestroyRenderTarget(const RenderTargetHandle handle, const DestroyMode mode)
    {
        if (handle == InvalidResourceHandle)
        {
            return;
        }

        if (!RemoveTrackedHandle(m_RenderTargets, handle))
        {
            return;
        }

        if (mode == DestroyMode::Deferred)
        {
            QueueDestroy(ResourceType::RenderTarget, handle);
            return;
        }

        DestroyNow(ResourceType::RenderTarget, handle);
    }

    void GPUResourceManager::DestroyFramebuffer(const FramebufferHandle handle, const DestroyMode mode)
    {
        if (handle == InvalidResourceHandle)
        {
            return;
        }

        if (!RemoveTrackedHandle(m_Framebuffers, handle))
        {
            return;
        }

        if (mode == DestroyMode::Deferred)
        {
            QueueDestroy(ResourceType::Framebuffer, handle);
            return;
        }

        DestroyNow(ResourceType::Framebuffer, handle);
    }

    void GPUResourceManager::SetDeferredReleaseFrames(const std::uint64_t frameCount)
    {
        m_DeferredReleaseFrames = frameCount;
    }

    GPUResourceManager::Stats GPUResourceManager::GetStats() const
    {
        return {
            m_RenderPasses.size(),
            m_Pipelines.size(),
            m_Meshes.size(),
            m_Textures.size(),
            m_RenderTargets.size(),
            m_Framebuffers.size(),
            m_DescriptorSets.size(),
            m_PendingDestroy.size()
        };
    }

    std::vector<GPUResourceManager::DebugEntry> GPUResourceManager::GetDebugEntries() const
    {
        std::vector<DebugEntry> entries;
        entries.reserve(m_Records.size());

        for (const auto& [handle, record] : m_Records)
        {
            entries.push_back({
                handle,
                TypeLabel(record.type),
                record.debugName,
                record.pendingDestroy,
                record.createdFrame,
                record.retireFrame
            });
        }

        std::sort(
            entries.begin(),
            entries.end(),
            [](const DebugEntry& lhs, const DebugEntry& rhs)
            {
                return lhs.handle < rhs.handle;
            });
        return entries;
    }

    void GPUResourceManager::AttachBackend(IRenderBackend& backend)
    {
        if (m_Backend == &backend)
        {
            return;
        }

        if (m_Backend != nullptr)
        {
            Shutdown();
        }

        m_Backend = &backend;
    }

    void GPUResourceManager::QueueDestroy(const ResourceType type, const ResourceHandle handle)
    {
        if (m_Backend == nullptr || handle == InvalidResourceHandle)
        {
            return;
        }

        auto recordIt = m_Records.find(handle);
        if (recordIt != m_Records.end())
        {
            if (recordIt->second.pendingDestroy)
            {
                return;
            }
            recordIt->second.pendingDestroy = true;
            recordIt->second.retireFrame = m_FrameIndex + m_DeferredReleaseFrames;
        }

        m_PendingDestroy.push_back({
            type,
            handle,
            m_FrameIndex + m_DeferredReleaseFrames
        });
    }

    void GPUResourceManager::DestroyNow(const ResourceType type, const ResourceHandle handle)
    {
        if (handle == InvalidResourceHandle)
        {
            return;
        }

        if (m_Backend == nullptr)
        {
            m_Records.erase(handle);
            return;
        }

        switch (type)
        {
        case ResourceType::RenderPass:
            m_Backend->DestroyRenderPass(static_cast<RenderPassHandle>(handle));
            break;
        case ResourceType::PipelineState:
            m_Backend->DestroyPipelineState(static_cast<PipelineStateHandle>(handle));
            break;
        case ResourceType::Mesh:
            m_Backend->DestroyMesh(static_cast<MeshHandle>(handle));
            break;
        case ResourceType::Texture:
            m_Backend->DestroyTexture(static_cast<TextureHandle>(handle));
            break;
        case ResourceType::RenderTarget:
            m_Backend->DestroyRenderTarget(static_cast<RenderTargetHandle>(handle));
            break;
        case ResourceType::Framebuffer:
            m_Backend->DestroyFramebuffer(static_cast<FramebufferHandle>(handle));
            break;
        case ResourceType::DescriptorSet:
            m_Backend->DestroyDescriptorSet(static_cast<DescriptorSetHandle>(handle));
            break;
        default:
            break;
        }

        m_Records.erase(handle);
    }

    void GPUResourceManager::DrainDeferred(const std::uint64_t upToFrameInclusive)
    {
        if (m_Backend == nullptr || m_PendingDestroy.empty())
        {
            return;
        }

        std::vector<PendingDestroy> pendingKeep;
        pendingKeep.reserve(m_PendingDestroy.size());

        for (const PendingDestroy& pending : m_PendingDestroy)
        {
            if (pending.retireFrame <= upToFrameInclusive)
            {
                DestroyNow(pending.type, pending.handle);
            }
            else
            {
                pendingKeep.push_back(pending);
            }
        }

        m_PendingDestroy.swap(pendingKeep);
    }

    void GPUResourceManager::DestroyAllTrackedNow()
    {
        if (m_Backend == nullptr)
        {
            m_RenderPasses.clear();
            m_Pipelines.clear();
            m_Meshes.clear();
            m_Textures.clear();
            m_RenderTargets.clear();
            m_Framebuffers.clear();
            m_DescriptorSets.clear();
            m_Records.clear();
            return;
        }

        for (const DescriptorSetHandle handle : m_DescriptorSets)
        {
            DestroyNow(ResourceType::DescriptorSet, handle);
        }
        m_DescriptorSets.clear();

        for (const MeshHandle handle : m_Meshes)
        {
            DestroyNow(ResourceType::Mesh, handle);
        }
        m_Meshes.clear();

        for (const TextureHandle handle : m_Textures)
        {
            DestroyNow(ResourceType::Texture, handle);
        }
        m_Textures.clear();

        for (const FramebufferHandle handle : m_Framebuffers)
        {
            DestroyNow(ResourceType::Framebuffer, handle);
        }
        m_Framebuffers.clear();

        for (const RenderTargetHandle handle : m_RenderTargets)
        {
            DestroyNow(ResourceType::RenderTarget, handle);
        }
        m_RenderTargets.clear();

        for (const PipelineStateHandle handle : m_Pipelines)
        {
            DestroyNow(ResourceType::PipelineState, handle);
        }
        m_Pipelines.clear();

        for (const RenderPassHandle handle : m_RenderPasses)
        {
            DestroyNow(ResourceType::RenderPass, handle);
        }
        m_RenderPasses.clear();
    }

    std::string GPUResourceManager::TypeLabel(const ResourceType type)
    {
        switch (type)
        {
        case ResourceType::RenderPass:
            return "RenderPass";
        case ResourceType::PipelineState:
            return "PipelineState";
        case ResourceType::Mesh:
            return "Mesh";
        case ResourceType::Texture:
            return "Texture";
        case ResourceType::RenderTarget:
            return "RenderTarget";
        case ResourceType::Framebuffer:
            return "Framebuffer";
        case ResourceType::DescriptorSet:
            return "DescriptorSet";
        default:
            return "Unknown";
        }
    }

    void GPUResourceManager::TrackResource(
        const ResourceType type,
        const ResourceHandle handle,
        const std::string_view debugName)
    {
        if (handle == InvalidResourceHandle)
        {
            return;
        }

        ResourceRecord record;
        record.type = type;
        if (!debugName.empty())
        {
            record.debugName = std::string(debugName);
        }
        else
        {
            record.debugName = TypeLabel(type) + " #" + std::to_string(handle);
        }
        record.createdFrame = m_FrameIndex;
        record.pendingDestroy = false;
        record.retireFrame = 0;
        m_Records[handle] = std::move(record);
    }
}
