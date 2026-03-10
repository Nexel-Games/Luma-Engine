#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "Luma/RHI/IRenderBackend.h"

namespace Luma
{
    class OpenGLRenderBackend final : public IRenderBackend
    {
    public:
        OpenGLRenderBackend() = default;
        ~OpenGLRenderBackend() override = default;

        bool Initialize(GLFWwindow* window) override;
        void Shutdown() override;

        RenderPassHandle CreateRenderPass(const RenderPassDesc& desc) override;
        void DestroyRenderPass(RenderPassHandle handle) override;

        PipelineStateHandle CreatePipelineState(const PipelineStateDesc& desc) override;
        void DestroyPipelineState(PipelineStateHandle handle) override;

        MeshHandle CreateMesh(const MeshDesc& desc) override;
        void DestroyMesh(MeshHandle handle) override;

        TextureHandle CreateTexture(const TextureDesc& desc) override;
        void UpdateTexture(TextureHandle handle, const TextureDesc& desc) override;
        void DestroyTexture(TextureHandle handle) override;

        RenderTargetHandle CreateRenderTarget(const RenderTargetDesc& desc) override;
        void DestroyRenderTarget(RenderTargetHandle handle) override;

        FramebufferHandle CreateFramebuffer(const FramebufferDesc& desc) override;
        void DestroyFramebuffer(FramebufferHandle handle) override;

        DescriptorSetHandle CreateDescriptorSet(PipelineStateHandle pipeline, const DescriptorSetDesc& desc) override;
        void UpdateDescriptorSet(DescriptorSetHandle handle, const DescriptorSetDesc& desc) override;
        void DestroyDescriptorSet(DescriptorSetHandle handle) override;

        void BeginFrame() override;
        void BeginRenderPass(RenderPassHandle renderPass) override;
        void EndRenderPass() override;
        void BindPipeline(PipelineStateHandle pipeline) override;
        void BindDescriptorSet(DescriptorSetHandle descriptorSet) override;
        void DrawMesh(MeshHandle mesh) override;
        void EndFrame() override;

        void OnResize(std::uint32_t width, std::uint32_t height) override;
        void WaitIdle() override;
        RendererAPI GetAPI() const override;
        void SetSceneOutputSize(std::uint32_t width, std::uint32_t height) override;
        void* GetSceneOutputImGuiTexture() override;
        void* GetRenderTargetImGuiTexture(RenderTargetHandle renderTarget) override;
        TextureHandle GetRenderTargetTextureHandle(RenderTargetHandle renderTarget) const override;
        void SetSceneViewportRegion(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) override;
        void ClearSceneViewportRegion() override;
        void* CreateImGuiTextureRGBA8(std::uint32_t width, std::uint32_t height, const std::uint8_t* pixels) override;
        void DestroyImGuiTexture(void* textureHandle) override;

    private:
        struct GLRenderPassResource
        {
            RenderPassDesc desc;
        };

        struct GLPipelineStateResource
        {
            PipelineStateDesc desc;
            unsigned int program = 0;
        };

        struct GLMeshResource
        {
            unsigned int vertexBuffer = 0;
            unsigned int indexBuffer = 0;
            std::uint32_t indexCount = 0;
            IndexFormat indexFormat = IndexFormat::UInt32;
        };

        struct GLUniformBufferResource
        {
            unsigned int buffer = 0;
            std::size_t size = 0;
        };

        struct GLTextureResource
        {
            unsigned int texture = 0;
            std::uint32_t width = 0;
            std::uint32_t height = 0;
            GpuTextureFormat format = GpuTextureFormat::RGBA8;
            bool srgb = true;
        };

        struct GLRenderTargetResource
        {
            RenderTargetDesc desc;
            TextureHandle textureHandle = InvalidResourceHandle;
        };

        struct GLFramebufferResource
        {
            FramebufferDesc desc;
            unsigned int framebuffer = 0;
            unsigned int depthRenderbuffer = 0;
            std::uint32_t width = 1;
            std::uint32_t height = 1;
            bool useSceneOutput = true;
            bool presentToSwapchain = false;
        };

        struct GLSampledTextureBinding
        {
            unsigned int texture = 0;
            TextureHandle textureHandle = InvalidResourceHandle;
            bool owned = false;
        };

        struct GLDescriptorSetResource
        {
            PipelineStateHandle pipeline = InvalidResourceHandle;
            std::unordered_map<std::uint32_t, GLUniformBufferResource> uniformBuffers;
            std::unordered_map<std::uint32_t, GLSampledTextureBinding> sampledTextures;
        };

        static unsigned int CompileShader(unsigned int shaderType, const std::string& source);
        static std::string ReadTextFile(const std::string& path);
        static std::string ResolveShaderPath(const std::string& folder, const std::string& shaderFileName);
        static int VertexFormatComponentCount(VertexFormat format);
        static unsigned int PrimitiveTopologyToGL(PrimitiveTopology topology);
        static unsigned int BlendFactorToGL(BlendFactor factor);
        static unsigned int BlendOpToGL(BlendOp op);
        static int InternalTextureFormatToGL(GpuTextureFormat format, bool srgb);

        static std::string GetExecutableDirectory();
        ResourceHandle AllocateHandle();

        GLFWwindow* m_Window = nullptr;
        unsigned int m_VertexArray = 0;
        std::uint32_t m_ViewportWidth = 1;
        std::uint32_t m_ViewportHeight = 1;
        unsigned int m_SceneFramebuffer = 0;
        unsigned int m_SceneColorTexture = 0;
        unsigned int m_SceneDepthRenderbuffer = 0;
        std::uint32_t m_SceneOutputWidth = 0;
        std::uint32_t m_SceneOutputHeight = 0;
        bool m_UseSceneViewportRegion = false;
        std::uint32_t m_SceneViewportX = 0;
        std::uint32_t m_SceneViewportY = 0;
        std::uint32_t m_SceneViewportWidth = 1;
        std::uint32_t m_SceneViewportHeight = 1;

        ResourceHandle m_NextHandle = 1;

        std::unordered_map<RenderPassHandle, GLRenderPassResource> m_RenderPasses;
        std::unordered_map<PipelineStateHandle, GLPipelineStateResource> m_Pipelines;
        std::unordered_map<MeshHandle, GLMeshResource> m_Meshes;
        std::unordered_map<TextureHandle, GLTextureResource> m_Textures;
        std::unordered_map<RenderTargetHandle, GLRenderTargetResource> m_RenderTargets;
        std::unordered_map<FramebufferHandle, GLFramebufferResource> m_Framebuffers;
        std::unordered_map<DescriptorSetHandle, GLDescriptorSetResource> m_DescriptorSets;

        RenderPassHandle m_CurrentRenderPass = InvalidResourceHandle;
        PipelineStateHandle m_CurrentPipeline = InvalidResourceHandle;
        DescriptorSetHandle m_CurrentDescriptorSet = InvalidResourceHandle;
    };
}
