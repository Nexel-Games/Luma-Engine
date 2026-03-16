#pragma once

#include <cstdint>

#include "Luma/RHI/RendererAPI.h"
#include "Luma/RHI/RHIResources.h"

struct GLFWwindow;
struct ImDrawData;

namespace Luma
{
    class IRenderBackend
    {
    public:
        virtual ~IRenderBackend() = default;

        virtual bool Initialize(GLFWwindow* window) = 0;
        virtual void Shutdown() = 0;

        virtual RenderPassHandle CreateRenderPass(const RenderPassDesc& desc) = 0;
        virtual void DestroyRenderPass(RenderPassHandle handle) = 0;

        virtual PipelineStateHandle CreatePipelineState(const PipelineStateDesc& desc) = 0;
        virtual void DestroyPipelineState(PipelineStateHandle handle) = 0;

        virtual MeshHandle CreateMesh(const MeshDesc& desc) = 0;
        virtual void DestroyMesh(MeshHandle handle) = 0;

        virtual TextureHandle CreateTexture(const TextureDesc& desc) = 0;
        virtual void UpdateTexture(TextureHandle handle, const TextureDesc& desc) = 0;
        virtual void DestroyTexture(TextureHandle handle) = 0;

        virtual RenderTargetHandle CreateRenderTarget(const RenderTargetDesc& desc) = 0;
        virtual void DestroyRenderTarget(RenderTargetHandle handle) = 0;

        virtual FramebufferHandle CreateFramebuffer(const FramebufferDesc& desc) = 0;
        virtual void DestroyFramebuffer(FramebufferHandle handle) = 0;

        virtual DescriptorSetHandle CreateDescriptorSet(PipelineStateHandle pipeline, const DescriptorSetDesc& desc) = 0;
        virtual void UpdateDescriptorSet(DescriptorSetHandle handle, const DescriptorSetDesc& desc) = 0;
        virtual void DestroyDescriptorSet(DescriptorSetHandle handle) = 0;

        virtual void BeginFrame() = 0;
        virtual void BeginRenderPass(RenderPassHandle renderPass) = 0;
        virtual void EndRenderPass() = 0;
        virtual void BindPipeline(PipelineStateHandle pipeline) = 0;
        virtual void BindDescriptorSet(DescriptorSetHandle descriptorSet) = 0;
        virtual void DrawMesh(MeshHandle mesh) = 0;
        virtual void EndFrame() = 0;

        virtual void OnResize(std::uint32_t width, std::uint32_t height) = 0;
        virtual void WaitIdle() = 0;
        virtual RendererAPI GetAPI() const = 0;
        virtual void SetVSyncEnabled(bool enabled)
        {
            (void)enabled;
        }
        virtual bool IsVSyncEnabled() const
        {
            return false;
        }
        virtual void SetSceneOutputSize(std::uint32_t width, std::uint32_t height)
        {
            (void)width;
            (void)height;
        }
        virtual void* GetSceneOutputImGuiTexture()
        {
            return nullptr;
        }
        virtual void* GetRenderTargetImGuiTexture(RenderTargetHandle renderTarget)
        {
            (void)renderTarget;
            return nullptr;
        }
        virtual TextureHandle GetRenderTargetTextureHandle(RenderTargetHandle renderTarget) const
        {
            (void)renderTarget;
            return InvalidTextureHandle;
        }
        virtual void SetSceneViewportRegion(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height)
        {
            (void)x;
            (void)y;
            (void)width;
            (void)height;
        }
        virtual void ClearSceneViewportRegion()
        {
        }

        virtual bool InitializeImGuiBackend()
        {
            return false;
        }

        virtual void ShutdownImGuiBackend()
        {
        }

        virtual void BeginImGuiBackendFrame()
        {
        }

        virtual void RenderImGuiDrawData(ImDrawData* drawData)
        {
            (void)drawData;
        }

        virtual void* CreateImGuiTextureRGBA8(std::uint32_t width, std::uint32_t height, const std::uint8_t* pixels)
        {
            (void)width;
            (void)height;
            (void)pixels;
            return nullptr;
        }

        virtual void DestroyImGuiTexture(void* textureHandle)
        {
            (void)textureHandle;
        }
    };
}
