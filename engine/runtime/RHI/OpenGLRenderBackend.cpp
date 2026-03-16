#include "RHI/OpenGLRenderBackend.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace Luma
{
    bool OpenGLRenderBackend::Initialize(GLFWwindow* window)
    {
        m_Window = window;
        if (!m_Window)
        {
            return false;
        }

        glfwMakeContextCurrent(m_Window);
        SetVSyncEnabled(m_VSyncEnabled);

        if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0)
        {
            std::cerr << "Failed to initialize GLAD." << '\n';
            return false;
        }

        int glMajorVersion = 0;
        int glMinorVersion = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &glMajorVersion);
        glGetIntegerv(GL_MINOR_VERSION, &glMinorVersion);
        std::cout << "OpenGL context: " << glMajorVersion << '.' << glMinorVersion << '\n';

        glGenVertexArrays(1, &m_VertexArray);
        glBindVertexArray(m_VertexArray);

        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(m_Window, &width, &height);
        OnResize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
        return true;
    }

    void OpenGLRenderBackend::Shutdown()
    {
        if (m_SceneFramebuffer != 0)
        {
            glDeleteFramebuffers(1, &m_SceneFramebuffer);
            m_SceneFramebuffer = 0;
        }
        if (m_SceneDepthRenderbuffer != 0)
        {
            glDeleteRenderbuffers(1, &m_SceneDepthRenderbuffer);
            m_SceneDepthRenderbuffer = 0;
        }
        if (m_SceneColorTexture != 0)
        {
            glDeleteTextures(1, &m_SceneColorTexture);
            m_SceneColorTexture = 0;
        }
        m_SceneOutputWidth = 0;
        m_SceneOutputHeight = 0;

        for (auto& [_, descriptorSet] : m_DescriptorSets)
        {
            for (auto& [__, uniformBuffer] : descriptorSet.uniformBuffers)
            {
                if (uniformBuffer.buffer != 0)
                {
                    glDeleteBuffers(1, &uniformBuffer.buffer);
                }
            }
            for (auto& [__, binding] : descriptorSet.sampledTextures)
            {
                if (binding.owned && binding.texture != 0)
                {
                    glDeleteTextures(1, &binding.texture);
                }
            }
        }
        m_DescriptorSets.clear();

        for (auto& [_, framebuffer] : m_Framebuffers)
        {
            if (framebuffer.framebuffer != 0)
            {
                glDeleteFramebuffers(1, &framebuffer.framebuffer);
            }
        }
        m_Framebuffers.clear();
        m_RenderTargets.clear();

        for (auto& [_, texture] : m_Textures)
        {
            if (texture.texture != 0)
            {
                glDeleteTextures(1, &texture.texture);
            }
        }
        m_Textures.clear();

        for (auto& [_, mesh] : m_Meshes)
        {
            if (mesh.vertexBuffer != 0)
            {
                glDeleteBuffers(1, &mesh.vertexBuffer);
            }
            if (mesh.indexBuffer != 0)
            {
                glDeleteBuffers(1, &mesh.indexBuffer);
            }
        }
        m_Meshes.clear();

        for (auto& [_, pipeline] : m_Pipelines)
        {
            if (pipeline.program != 0)
            {
                glDeleteProgram(pipeline.program);
            }
        }
        m_Pipelines.clear();

        m_RenderPasses.clear();

        if (m_VertexArray != 0)
        {
            glDeleteVertexArrays(1, &m_VertexArray);
            m_VertexArray = 0;
        }
    }

    RenderPassHandle OpenGLRenderBackend::CreateRenderPass(const RenderPassDesc& desc)
    {
        const RenderPassHandle handle = AllocateHandle();
        m_RenderPasses.emplace(handle, GLRenderPassResource { desc });
        return handle;
    }

    void OpenGLRenderBackend::DestroyRenderPass(const RenderPassHandle handle)
    {
        m_RenderPasses.erase(handle);
        if (m_CurrentRenderPass == handle)
        {
            m_CurrentRenderPass = InvalidResourceHandle;
        }
    }

    PipelineStateHandle OpenGLRenderBackend::CreatePipelineState(const PipelineStateDesc& desc)
    {
        try
        {
            const std::string vertexSource = ReadTextFile(ResolveShaderPath("opengl", desc.shaders.vertexPath));
            const std::string fragmentSource = ReadTextFile(ResolveShaderPath("opengl", desc.shaders.fragmentPath));

            const unsigned int vertexShader = CompileShader(GL_VERTEX_SHADER, vertexSource);
            const unsigned int fragmentShader = CompileShader(GL_FRAGMENT_SHADER, fragmentSource);

            const unsigned int program = glCreateProgram();
            glAttachShader(program, vertexShader);
            glAttachShader(program, fragmentShader);
            glLinkProgram(program);

            glDeleteShader(vertexShader);
            glDeleteShader(fragmentShader);

            int linked = 0;
            glGetProgramiv(program, GL_LINK_STATUS, &linked);
            if (linked == GL_FALSE)
            {
                int infoLogLength = 0;
                glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLogLength);
                std::array<char, 2048> infoLog {};
                glGetProgramInfoLog(program, infoLogLength, nullptr, infoLog.data());
                std::cerr << "OpenGL program link error: " << infoLog.data() << '\n';
                glDeleteProgram(program);
                return InvalidResourceHandle;
            }

            const PipelineStateHandle handle = AllocateHandle();
            m_Pipelines.emplace(handle, GLPipelineStateResource { desc, program });
            return handle;
        }
        catch (const std::exception& e)
        {
            std::cerr << "OpenGL pipeline creation failed: " << e.what() << '\n';
            return InvalidResourceHandle;
        }
    }

    void OpenGLRenderBackend::DestroyPipelineState(const PipelineStateHandle handle)
    {
        const auto it = m_Pipelines.find(handle);
        if (it == m_Pipelines.end())
        {
            return;
        }

        if (it->second.program != 0)
        {
            glDeleteProgram(it->second.program);
        }
        m_Pipelines.erase(it);

        if (m_CurrentPipeline == handle)
        {
            m_CurrentPipeline = InvalidResourceHandle;
        }
    }

    MeshHandle OpenGLRenderBackend::CreateMesh(const MeshDesc& desc)
    {
        if (desc.vertexData.empty() || desc.indexData.empty())
        {
            return InvalidResourceHandle;
        }

        GLMeshResource mesh;
        mesh.indexFormat = desc.indexFormat;
        mesh.indexCount = static_cast<std::uint32_t>(desc.indexData.size() / IndexFormatSize(desc.indexFormat));

        glGenBuffers(1, &mesh.vertexBuffer);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(desc.vertexData.size()),
            desc.vertexData.data(),
            GL_STATIC_DRAW);

        glGenBuffers(1, &mesh.indexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indexBuffer);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(desc.indexData.size()),
            desc.indexData.data(),
            GL_STATIC_DRAW);

        const MeshHandle handle = AllocateHandle();
        m_Meshes.emplace(handle, mesh);
        return handle;
    }

    void OpenGLRenderBackend::DestroyMesh(const MeshHandle handle)
    {
        const auto it = m_Meshes.find(handle);
        if (it == m_Meshes.end())
        {
            return;
        }

        if (it->second.vertexBuffer != 0)
        {
            glDeleteBuffers(1, &it->second.vertexBuffer);
        }
        if (it->second.indexBuffer != 0)
        {
            glDeleteBuffers(1, &it->second.indexBuffer);
        }

        m_Meshes.erase(it);
    }

    TextureHandle OpenGLRenderBackend::CreateTexture(const TextureDesc& desc)
    {
        if (desc.width == 0 || desc.height == 0)
        {
            return InvalidResourceHandle;
        }

        if (desc.format != GpuTextureFormat::RGBA8)
        {
            return InvalidResourceHandle;
        }

        const std::size_t expectedSize = static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height) * 4ULL;
        if (desc.pixelData.size() != expectedSize)
        {
            return InvalidResourceHandle;
        }

        unsigned int texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            InternalTextureFormatToGL(desc.format, desc.srgb),
            static_cast<GLsizei>(desc.width),
            static_cast<GLsizei>(desc.height),
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            desc.pixelData.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        const TextureHandle handle = AllocateHandle();
        m_Textures.emplace(handle, GLTextureResource { texture, desc.width, desc.height, desc.format, desc.srgb });
        return handle;
    }

    void OpenGLRenderBackend::UpdateTexture(const TextureHandle handle, const TextureDesc& desc)
    {
        const auto it = m_Textures.find(handle);
        if (it == m_Textures.end() || desc.width == 0 || desc.height == 0)
        {
            return;
        }

        if (desc.format != GpuTextureFormat::RGBA8)
        {
            return;
        }

        const std::size_t expectedSize = static_cast<std::size_t>(desc.width) * static_cast<std::size_t>(desc.height) * 4ULL;
        if (desc.pixelData.size() != expectedSize)
        {
            return;
        }

        glBindTexture(GL_TEXTURE_2D, it->second.texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            InternalTextureFormatToGL(desc.format, desc.srgb),
            static_cast<GLsizei>(desc.width),
            static_cast<GLsizei>(desc.height),
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            desc.pixelData.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        it->second.width = desc.width;
        it->second.height = desc.height;
        it->second.format = desc.format;
        it->second.srgb = desc.srgb;
    }

    void OpenGLRenderBackend::DestroyTexture(const TextureHandle handle)
    {
        const auto textureIt = m_Textures.find(handle);
        if (textureIt == m_Textures.end())
        {
            return;
        }

        const unsigned int textureId = textureIt->second.texture;

        if (textureId != 0)
        {
            glDeleteTextures(1, &textureId);
        }
        m_Textures.erase(textureIt);

        for (auto& [_, descriptorSet] : m_DescriptorSets)
        {
            for (auto& [__, binding] : descriptorSet.sampledTextures)
            {
                if (!binding.owned && binding.textureHandle == handle)
                {
                    binding.texture = 0;
                    binding.textureHandle = InvalidResourceHandle;
                }
            }
        }

    }

    RenderTargetHandle OpenGLRenderBackend::CreateRenderTarget(const RenderTargetDesc& desc)
    {
        const std::uint32_t width = std::max<std::uint32_t>(desc.width, 1);
        const std::uint32_t height = std::max<std::uint32_t>(desc.height, 1);

        unsigned int texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            InternalTextureFormatToGL(desc.format, desc.srgb),
            static_cast<GLsizei>(width),
            static_cast<GLsizei>(height),
            0,
            GL_RGBA,
            desc.format == GpuTextureFormat::RGBA16F ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE,
            nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        const TextureHandle textureHandle = AllocateHandle();
        m_Textures.emplace(textureHandle, GLTextureResource { texture, width, height, desc.format, desc.srgb });

        const RenderTargetHandle handle = AllocateHandle();
        m_RenderTargets.emplace(handle, GLRenderTargetResource { desc, textureHandle });
        return handle;
    }

    void OpenGLRenderBackend::DestroyRenderTarget(const RenderTargetHandle handle)
    {
        const auto targetIt = m_RenderTargets.find(handle);
        if (targetIt == m_RenderTargets.end())
        {
            return;
        }

        std::vector<FramebufferHandle> framebuffersToDestroy;
        framebuffersToDestroy.reserve(m_Framebuffers.size());
        for (const auto& [framebufferHandle, framebuffer] : m_Framebuffers)
        {
            if (framebuffer.desc.colorTarget == handle)
            {
                framebuffersToDestroy.push_back(framebufferHandle);
            }
        }
        for (const FramebufferHandle framebufferHandle : framebuffersToDestroy)
        {
            DestroyFramebuffer(framebufferHandle);
        }

        DestroyTexture(targetIt->second.textureHandle);
        m_RenderTargets.erase(targetIt);
    }

    FramebufferHandle OpenGLRenderBackend::CreateFramebuffer(const FramebufferDesc& desc)
    {
        GLFramebufferResource framebuffer;
        framebuffer.desc = desc;
        framebuffer.presentToSwapchain = desc.presentToSwapchain;
        framebuffer.useSceneOutput = !desc.presentToSwapchain && desc.colorTarget == InvalidResourceHandle;

        if (desc.presentToSwapchain)
        {
            framebuffer.framebuffer = 0;
            framebuffer.width = m_ViewportWidth;
            framebuffer.height = m_ViewportHeight;
        }
        else if (desc.colorTarget != InvalidResourceHandle)
        {
            const auto targetIt = m_RenderTargets.find(desc.colorTarget);
            if (targetIt == m_RenderTargets.end())
            {
                return InvalidResourceHandle;
            }

            const auto textureIt = m_Textures.find(targetIt->second.textureHandle);
            if (textureIt == m_Textures.end() || textureIt->second.texture == 0)
            {
                return InvalidResourceHandle;
            }

            framebuffer.width = textureIt->second.width;
            framebuffer.height = textureIt->second.height;

            glGenFramebuffers(1, &framebuffer.framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer.framebuffer);
            glFramebufferTexture2D(
                GL_FRAMEBUFFER,
                GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D,
                textureIt->second.texture,
                0);
            glGenRenderbuffers(1, &framebuffer.depthRenderbuffer);
            glBindRenderbuffer(GL_RENDERBUFFER, framebuffer.depthRenderbuffer);
            glRenderbufferStorage(
                GL_RENDERBUFFER,
                GL_DEPTH_COMPONENT24,
                static_cast<GLsizei>(framebuffer.width),
                static_cast<GLsizei>(framebuffer.height));
            glFramebufferRenderbuffer(
                GL_FRAMEBUFFER,
                GL_DEPTH_ATTACHMENT,
                GL_RENDERBUFFER,
                framebuffer.depthRenderbuffer);
            glBindRenderbuffer(GL_RENDERBUFFER, 0);
            const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            if (status != GL_FRAMEBUFFER_COMPLETE)
            {
                if (framebuffer.depthRenderbuffer != 0)
                {
                    glDeleteRenderbuffers(1, &framebuffer.depthRenderbuffer);
                    framebuffer.depthRenderbuffer = 0;
                }
                if (framebuffer.framebuffer != 0)
                {
                    glDeleteFramebuffers(1, &framebuffer.framebuffer);
                }
                return InvalidResourceHandle;
            }
        }
        else
        {
            framebuffer.framebuffer = 0;
            framebuffer.width = m_SceneOutputWidth > 0 ? m_SceneOutputWidth : m_ViewportWidth;
            framebuffer.height = m_SceneOutputHeight > 0 ? m_SceneOutputHeight : m_ViewportHeight;
        }

        const FramebufferHandle handle = AllocateHandle();
        m_Framebuffers.emplace(handle, framebuffer);
        return handle;
    }

    void OpenGLRenderBackend::DestroyFramebuffer(const FramebufferHandle handle)
    {
        const auto framebufferIt = m_Framebuffers.find(handle);
        if (framebufferIt == m_Framebuffers.end())
        {
            return;
        }

        if (framebufferIt->second.framebuffer != 0)
        {
            glDeleteFramebuffers(1, &framebufferIt->second.framebuffer);
        }
        if (framebufferIt->second.depthRenderbuffer != 0)
        {
            glDeleteRenderbuffers(1, &framebufferIt->second.depthRenderbuffer);
        }
        m_Framebuffers.erase(framebufferIt);
    }

    DescriptorSetHandle OpenGLRenderBackend::CreateDescriptorSet(const PipelineStateHandle pipeline, const DescriptorSetDesc& desc)
    {
        if (m_Pipelines.find(pipeline) == m_Pipelines.end())
        {
            return InvalidResourceHandle;
        }

        GLDescriptorSetResource descriptorSet;
        descriptorSet.pipeline = pipeline;

        for (const auto& write : desc.buffers)
        {
            GLUniformBufferResource bufferResource;
            bufferResource.size = write.data.size();

            glGenBuffers(1, &bufferResource.buffer);
            glBindBuffer(GL_UNIFORM_BUFFER, bufferResource.buffer);
            glBufferData(
                GL_UNIFORM_BUFFER,
                static_cast<GLsizeiptr>(write.data.size()),
                write.data.data(),
                GL_DYNAMIC_DRAW);

            descriptorSet.uniformBuffers.emplace(write.binding, bufferResource);
        }

        for (const auto& imageWrite : desc.images)
        {
            GLSampledTextureBinding sampledBinding;

            if (imageWrite.texture != InvalidResourceHandle)
            {
                const auto textureIt = m_Textures.find(imageWrite.texture);
                if (textureIt == m_Textures.end())
                {
                    continue;
                }

                sampledBinding.texture = textureIt->second.texture;
                sampledBinding.textureHandle = imageWrite.texture;
                sampledBinding.owned = false;
            }
            else
            {
                if (imageWrite.width == 0 || imageWrite.height == 0)
                {
                    continue;
                }

                const std::size_t expectedSize =
                    static_cast<std::size_t>(imageWrite.width) * static_cast<std::size_t>(imageWrite.height) * 4ULL;
                if (imageWrite.pixels.size() != expectedSize)
                {
                    continue;
                }

                glGenTextures(1, &sampledBinding.texture);
                glBindTexture(GL_TEXTURE_2D, sampledBinding.texture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexImage2D(
                    GL_TEXTURE_2D,
                    0,
                    imageWrite.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
                    static_cast<GLsizei>(imageWrite.width),
                    static_cast<GLsizei>(imageWrite.height),
                    0,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    imageWrite.pixels.data());
                glBindTexture(GL_TEXTURE_2D, 0);
                sampledBinding.textureHandle = InvalidResourceHandle;
                sampledBinding.owned = true;
            }

            descriptorSet.sampledTextures[imageWrite.binding] = sampledBinding;
        }

        const DescriptorSetHandle handle = AllocateHandle();
        m_DescriptorSets.emplace(handle, std::move(descriptorSet));
        return handle;
    }

    void OpenGLRenderBackend::UpdateDescriptorSet(const DescriptorSetHandle handle, const DescriptorSetDesc& desc)
    {
        const auto setIt = m_DescriptorSets.find(handle);
        if (setIt == m_DescriptorSets.end())
        {
            return;
        }

        for (const auto& write : desc.buffers)
        {
            const auto bufferIt = setIt->second.uniformBuffers.find(write.binding);
            if (bufferIt == setIt->second.uniformBuffers.end())
            {
                continue;
            }

            glBindBuffer(GL_UNIFORM_BUFFER, bufferIt->second.buffer);
            if (!write.data.empty())
            {
                glBufferSubData(
                    GL_UNIFORM_BUFFER,
                    0,
                    static_cast<GLsizeiptr>(std::min(bufferIt->second.size, write.data.size())),
                    write.data.data());
            }
        }

        for (const auto& imageWrite : desc.images)
        {
            auto textureIt = setIt->second.sampledTextures.find(imageWrite.binding);
            if (textureIt == setIt->second.sampledTextures.end())
            {
                continue;
            }

            GLSampledTextureBinding& sampledBinding = textureIt->second;
            if (imageWrite.texture != InvalidResourceHandle)
            {
                const auto backendTextureIt = m_Textures.find(imageWrite.texture);
                if (backendTextureIt == m_Textures.end())
                {
                    continue;
                }

                if (sampledBinding.owned && sampledBinding.texture != 0)
                {
                    glDeleteTextures(1, &sampledBinding.texture);
                }

                sampledBinding.texture = backendTextureIt->second.texture;
                sampledBinding.textureHandle = imageWrite.texture;
                sampledBinding.owned = false;
                continue;
            }

            if (imageWrite.width == 0 || imageWrite.height == 0)
            {
                if (sampledBinding.owned && sampledBinding.texture != 0)
                {
                    glDeleteTextures(1, &sampledBinding.texture);
                }
                sampledBinding.texture = 0;
                sampledBinding.textureHandle = InvalidResourceHandle;
                sampledBinding.owned = false;
                continue;
            }

            const std::size_t expectedSize =
                static_cast<std::size_t>(imageWrite.width) * static_cast<std::size_t>(imageWrite.height) * 4ULL;
            if (imageWrite.pixels.size() != expectedSize)
            {
                continue;
            }

            if (!sampledBinding.owned)
            {
                glGenTextures(1, &sampledBinding.texture);
                sampledBinding.textureHandle = InvalidResourceHandle;
                sampledBinding.owned = true;
            }

            glBindTexture(GL_TEXTURE_2D, sampledBinding.texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glTexImage2D(
                GL_TEXTURE_2D,
                0,
                imageWrite.srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8,
                static_cast<GLsizei>(imageWrite.width),
                static_cast<GLsizei>(imageWrite.height),
                0,
                GL_RGBA,
                GL_UNSIGNED_BYTE,
                imageWrite.pixels.data());
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    void OpenGLRenderBackend::DestroyDescriptorSet(const DescriptorSetHandle handle)
    {
        const auto it = m_DescriptorSets.find(handle);
        if (it == m_DescriptorSets.end())
        {
            return;
        }

        for (auto& [_, buffer] : it->second.uniformBuffers)
        {
            if (buffer.buffer != 0)
            {
                glDeleteBuffers(1, &buffer.buffer);
            }
        }
        for (auto& [_, binding] : it->second.sampledTextures)
        {
            if (binding.owned && binding.texture != 0)
            {
                glDeleteTextures(1, &binding.texture);
            }
        }

        m_DescriptorSets.erase(it);
        if (m_CurrentDescriptorSet == handle)
        {
            m_CurrentDescriptorSet = InvalidResourceHandle;
        }
    }

    void OpenGLRenderBackend::BeginFrame()
    {
    }

    void OpenGLRenderBackend::BeginRenderPass(const RenderPassHandle renderPass)
    {
        const auto it = m_RenderPasses.find(renderPass);
        if (it == m_RenderPasses.end())
        {
            return;
        }

        unsigned int framebufferToBind = 0;
        std::uint32_t viewportWidth = m_ViewportWidth;
        std::uint32_t viewportHeight = m_ViewportHeight;

        if (it->second.desc.framebuffer != InvalidResourceHandle)
        {
            const auto framebufferIt = m_Framebuffers.find(it->second.desc.framebuffer);
            if (framebufferIt != m_Framebuffers.end())
            {
                const GLFramebufferResource& framebufferResource = framebufferIt->second;
                if (framebufferResource.presentToSwapchain)
                {
                    framebufferToBind = 0;
                    viewportWidth = m_ViewportWidth;
                    viewportHeight = m_ViewportHeight;
                }
                else if (framebufferResource.useSceneOutput)
                {
                    const bool hasSceneOutput =
                        m_SceneFramebuffer != 0 &&
                        m_SceneColorTexture != 0 &&
                        m_SceneOutputWidth > 0 &&
                        m_SceneOutputHeight > 0;
                    framebufferToBind = hasSceneOutput ? m_SceneFramebuffer : 0;
                    viewportWidth = hasSceneOutput ? m_SceneOutputWidth : m_ViewportWidth;
                    viewportHeight = hasSceneOutput ? m_SceneOutputHeight : m_ViewportHeight;
                }
                else
                {
                    framebufferToBind = framebufferResource.framebuffer;
                    viewportWidth = framebufferResource.width;
                    viewportHeight = framebufferResource.height;
                }
            }
        }
        else
        {
            const bool hasSceneOutput =
                m_SceneFramebuffer != 0 &&
                m_SceneColorTexture != 0 &&
                m_SceneOutputWidth > 0 &&
                m_SceneOutputHeight > 0;
            framebufferToBind = hasSceneOutput ? m_SceneFramebuffer : 0;
            viewportWidth = hasSceneOutput ? m_SceneOutputWidth : m_ViewportWidth;
            viewportHeight = hasSceneOutput ? m_SceneOutputHeight : m_ViewportHeight;
        }

        m_CurrentRenderPass = renderPass;
        glBindFramebuffer(GL_FRAMEBUFFER, framebufferToBind);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        if (m_UseSceneViewportRegion)
        {
            const std::uint32_t regionX = std::min(m_SceneViewportX, viewportWidth);
            const std::uint32_t regionY = std::min(m_SceneViewportY, viewportHeight);
            const std::uint32_t regionWidth = std::min(m_SceneViewportWidth, viewportWidth - regionX);
            const std::uint32_t regionHeight = std::min(m_SceneViewportHeight, viewportHeight - regionY);
            glEnable(GL_SCISSOR_TEST);
            glScissor(
                static_cast<GLint>(regionX),
                static_cast<GLint>(regionY),
                static_cast<GLsizei>(regionWidth),
                static_cast<GLsizei>(regionHeight));
            glViewport(
                static_cast<GLint>(regionX),
                static_cast<GLint>(regionY),
                static_cast<GLsizei>(regionWidth),
                static_cast<GLsizei>(regionHeight));
        }
        else
        {
            glDisable(GL_SCISSOR_TEST);
            glViewport(0, 0, static_cast<int>(viewportWidth), static_cast<int>(viewportHeight));
        }
        glClearColor(
            it->second.desc.clearColor.r,
            it->second.desc.clearColor.g,
            it->second.desc.clearColor.b,
            it->second.desc.clearColor.a);
        glClearDepth(1.0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void OpenGLRenderBackend::EndRenderPass()
    {
        glDisable(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_CurrentRenderPass = InvalidResourceHandle;
    }

    void OpenGLRenderBackend::BindPipeline(const PipelineStateHandle pipeline)
    {
        const auto it = m_Pipelines.find(pipeline);
        if (it == m_Pipelines.end())
        {
            return;
        }

        m_CurrentPipeline = pipeline;
        glUseProgram(it->second.program);

        if (it->second.desc.cullMode == CullMode::None)
        {
            glDisable(GL_CULL_FACE);
        }
        else
        {
            glEnable(GL_CULL_FACE);
            glCullFace(it->second.desc.cullMode == CullMode::Back ? GL_BACK : GL_FRONT);
        }

        glFrontFace(it->second.desc.frontFace == FrontFace::CounterClockwise ? GL_CCW : GL_CW);

        if (it->second.desc.depthStencilState.depthTestEnabled)
        {
            glEnable(GL_DEPTH_TEST);
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthMask(it->second.desc.depthStencilState.depthWriteEnabled ? GL_TRUE : GL_FALSE);

        if (it->second.desc.blendState.enabled)
        {
            glEnable(GL_BLEND);
            glBlendEquationSeparate(
                BlendOpToGL(it->second.desc.blendState.colorOp),
                BlendOpToGL(it->second.desc.blendState.alphaOp));
            glBlendFuncSeparate(
                BlendFactorToGL(it->second.desc.blendState.srcColorFactor),
                BlendFactorToGL(it->second.desc.blendState.dstColorFactor),
                BlendFactorToGL(it->second.desc.blendState.srcAlphaFactor),
                BlendFactorToGL(it->second.desc.blendState.dstAlphaFactor));
        }
        else
        {
            glDisable(GL_BLEND);
        }
    }

    void OpenGLRenderBackend::BindDescriptorSet(const DescriptorSetHandle descriptorSet)
    {
        const auto it = m_DescriptorSets.find(descriptorSet);
        if (it == m_DescriptorSets.end())
        {
            return;
        }

        m_CurrentDescriptorSet = descriptorSet;
        for (const auto& [binding, uniformBuffer] : it->second.uniformBuffers)
        {
            glBindBufferBase(GL_UNIFORM_BUFFER, binding, uniformBuffer.buffer);
        }
        for (const auto& [binding, sampledBinding] : it->second.sampledTextures)
        {
            if (sampledBinding.texture == 0)
            {
                continue;
            }

            glActiveTexture(GL_TEXTURE0 + binding);
            glBindTexture(GL_TEXTURE_2D, sampledBinding.texture);
        }
        glActiveTexture(GL_TEXTURE0);
    }

    void OpenGLRenderBackend::DrawMesh(const MeshHandle meshHandle)
    {
        const auto pipelineIt = m_Pipelines.find(m_CurrentPipeline);
        const auto meshIt = m_Meshes.find(meshHandle);
        if (pipelineIt == m_Pipelines.end() || meshIt == m_Meshes.end())
        {
            return;
        }

        const auto& pipeline = pipelineIt->second;
        const auto& mesh = meshIt->second;

        glBindVertexArray(m_VertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.indexBuffer);

        for (const auto& attribute : pipeline.desc.vertexLayout.attributes)
        {
            glEnableVertexAttribArray(attribute.location);
            glVertexAttribPointer(
                attribute.location,
                VertexFormatComponentCount(attribute.format),
                GL_FLOAT,
                GL_FALSE,
                static_cast<GLsizei>(pipeline.desc.vertexLayout.stride),
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(attribute.offset)));
        }

        const GLenum indexType = mesh.indexFormat == IndexFormat::UInt16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
        glDrawElements(
            PrimitiveTopologyToGL(pipeline.desc.topology),
            static_cast<GLsizei>(mesh.indexCount),
            indexType,
            nullptr);
    }

    void OpenGLRenderBackend::EndFrame()
    {
        glfwSwapBuffers(m_Window);
    }

    void OpenGLRenderBackend::OnResize(const std::uint32_t width, const std::uint32_t height)
    {
        m_ViewportWidth = width;
        m_ViewportHeight = height;
        glViewport(0, 0, static_cast<int>(width), static_cast<int>(height));
    }

    void OpenGLRenderBackend::WaitIdle()
    {
        glFinish();
    }

    RendererAPI OpenGLRenderBackend::GetAPI() const
    {
        return RendererAPI::OpenGL;
    }

    void OpenGLRenderBackend::SetVSyncEnabled(const bool enabled)
    {
        m_VSyncEnabled = enabled;
        if (m_Window != nullptr)
        {
            glfwMakeContextCurrent(m_Window);
            glfwSwapInterval(enabled ? 1 : 0);
        }
    }

    bool OpenGLRenderBackend::IsVSyncEnabled() const
    {
        return m_VSyncEnabled;
    }

    void OpenGLRenderBackend::SetSceneOutputSize(
        const std::uint32_t width,
        const std::uint32_t height)
    {
        const std::uint32_t targetWidth = std::max<std::uint32_t>(width, 1);
        const std::uint32_t targetHeight = std::max<std::uint32_t>(height, 1);

        if (m_SceneColorTexture != 0 &&
            m_SceneDepthRenderbuffer != 0 &&
            m_SceneFramebuffer != 0 &&
            m_SceneOutputWidth == targetWidth &&
            m_SceneOutputHeight == targetHeight)
        {
            return;
        }

        if (m_SceneFramebuffer != 0)
        {
            glDeleteFramebuffers(1, &m_SceneFramebuffer);
            m_SceneFramebuffer = 0;
        }
        if (m_SceneDepthRenderbuffer != 0)
        {
            glDeleteRenderbuffers(1, &m_SceneDepthRenderbuffer);
            m_SceneDepthRenderbuffer = 0;
        }
        if (m_SceneColorTexture != 0)
        {
            glDeleteTextures(1, &m_SceneColorTexture);
            m_SceneColorTexture = 0;
        }

        glGenTextures(1, &m_SceneColorTexture);
        glBindTexture(GL_TEXTURE_2D, m_SceneColorTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            static_cast<GLsizei>(targetWidth),
            static_cast<GLsizei>(targetHeight),
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            nullptr);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenFramebuffers(1, &m_SceneFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, m_SceneFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_SceneColorTexture, 0);
        glGenRenderbuffers(1, &m_SceneDepthRenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, m_SceneDepthRenderbuffer);
        glRenderbufferStorage(
            GL_RENDERBUFFER,
            GL_DEPTH_COMPONENT24,
            static_cast<GLsizei>(targetWidth),
            static_cast<GLsizei>(targetHeight));
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER,
            GL_DEPTH_ATTACHMENT,
            GL_RENDERBUFFER,
            m_SceneDepthRenderbuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        const GLenum framebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE)
        {
            if (m_SceneFramebuffer != 0)
            {
                glDeleteFramebuffers(1, &m_SceneFramebuffer);
                m_SceneFramebuffer = 0;
            }
            if (m_SceneDepthRenderbuffer != 0)
            {
                glDeleteRenderbuffers(1, &m_SceneDepthRenderbuffer);
                m_SceneDepthRenderbuffer = 0;
            }
            if (m_SceneColorTexture != 0)
            {
                glDeleteTextures(1, &m_SceneColorTexture);
                m_SceneColorTexture = 0;
            }
            m_SceneOutputWidth = 0;
            m_SceneOutputHeight = 0;
            return;
        }

        m_SceneOutputWidth = targetWidth;
        m_SceneOutputHeight = targetHeight;
    }

    void* OpenGLRenderBackend::GetSceneOutputImGuiTexture()
    {
        if (m_SceneColorTexture == 0)
        {
            return nullptr;
        }

        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(m_SceneColorTexture));
    }

    void* OpenGLRenderBackend::GetRenderTargetImGuiTexture(const RenderTargetHandle renderTarget)
    {
        const auto targetIt = m_RenderTargets.find(renderTarget);
        if (targetIt == m_RenderTargets.end())
        {
            return nullptr;
        }

        const auto textureIt = m_Textures.find(targetIt->second.textureHandle);
        if (textureIt == m_Textures.end() || textureIt->second.texture == 0)
        {
            return nullptr;
        }

        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(textureIt->second.texture));
    }

    TextureHandle OpenGLRenderBackend::GetRenderTargetTextureHandle(const RenderTargetHandle renderTarget) const
    {
        const auto targetIt = m_RenderTargets.find(renderTarget);
        if (targetIt == m_RenderTargets.end())
        {
            return InvalidTextureHandle;
        }

        return targetIt->second.textureHandle;
    }

    void OpenGLRenderBackend::SetSceneViewportRegion(
        const std::uint32_t x,
        const std::uint32_t y,
        const std::uint32_t width,
        const std::uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            m_UseSceneViewportRegion = false;
            return;
        }

        m_UseSceneViewportRegion = true;
        m_SceneViewportX = x;
        m_SceneViewportY = y;
        m_SceneViewportWidth = width;
        m_SceneViewportHeight = height;
    }

    void OpenGLRenderBackend::ClearSceneViewportRegion()
    {
        m_UseSceneViewportRegion = false;
    }

    void* OpenGLRenderBackend::CreateImGuiTextureRGBA8(
        const std::uint32_t width,
        const std::uint32_t height,
        const std::uint8_t* pixels)
    {
        if (width == 0 || height == 0 || pixels == nullptr)
        {
            return nullptr;
        }

        unsigned int texture = 0;
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_RGBA8,
            static_cast<GLsizei>(width),
            static_cast<GLsizei>(height),
            0,
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            pixels);
        glBindTexture(GL_TEXTURE_2D, 0);

        return reinterpret_cast<void*>(static_cast<std::uintptr_t>(texture));
    }

    void OpenGLRenderBackend::DestroyImGuiTexture(void* textureHandle)
    {
        if (textureHandle == nullptr)
        {
            return;
        }

        const unsigned int texture = static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(textureHandle));
        if (texture != 0)
        {
            glDeleteTextures(1, &texture);
        }
    }

    unsigned int OpenGLRenderBackend::CompileShader(const unsigned int shaderType, const std::string& source)
    {
        const unsigned int shader = glCreateShader(shaderType);
        const char* sourcePointer = source.c_str();
        glShaderSource(shader, 1, &sourcePointer, nullptr);
        glCompileShader(shader);

        int isCompiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &isCompiled);
        if (isCompiled == GL_FALSE)
        {
            int infoLogLength = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLogLength);
            std::array<char, 2048> infoLog {};
            glGetShaderInfoLog(shader, infoLogLength, nullptr, infoLog.data());
            glDeleteShader(shader);
            throw std::runtime_error(std::string("OpenGL shader compile error: ") + infoLog.data());
        }

        return shader;
    }

    std::string OpenGLRenderBackend::ReadTextFile(const std::string& path)
    {
        std::ifstream file(path);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open text file: " + path);
        }

        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    std::string OpenGLRenderBackend::ResolveShaderPath(const std::string& folder, const std::string& shaderFileName)
    {
        const std::filesystem::path executableDir = GetExecutableDirectory();
        const std::vector<std::filesystem::path> candidatePaths = {
            std::filesystem::path(shaderFileName),
            std::filesystem::current_path() / "shaders" / folder / shaderFileName,
            executableDir / "shaders" / folder / shaderFileName,
            std::filesystem::current_path() / "assets" / "shaders" / folder / shaderFileName,
            executableDir / "assets" / "shaders" / folder / shaderFileName
        };

        for (const auto& candidate : candidatePaths)
        {
            if (std::filesystem::exists(candidate))
            {
                return candidate.string();
            }
        }

        throw std::runtime_error("Unable to locate shader file: " + shaderFileName);
    }

    int OpenGLRenderBackend::VertexFormatComponentCount(const VertexFormat format)
    {
        switch (format)
        {
        case VertexFormat::Float2:
            return 2;
        case VertexFormat::Float3:
            return 3;
        case VertexFormat::Float4:
            return 4;
        default:
            return 0;
        }
    }

    unsigned int OpenGLRenderBackend::PrimitiveTopologyToGL(const PrimitiveTopology topology)
    {
        switch (topology)
        {
        case PrimitiveTopology::TriangleList:
            return GL_TRIANGLES;
        default:
            return GL_TRIANGLES;
        }
    }

    unsigned int OpenGLRenderBackend::BlendFactorToGL(const BlendFactor factor)
    {
        switch (factor)
        {
        case BlendFactor::Zero:
            return GL_ZERO;
        case BlendFactor::One:
            return GL_ONE;
        case BlendFactor::SrcAlpha:
            return GL_SRC_ALPHA;
        case BlendFactor::OneMinusSrcAlpha:
            return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::DstColor:
            return GL_DST_COLOR;
        default:
            return GL_ONE;
        }
    }

    unsigned int OpenGLRenderBackend::BlendOpToGL(const BlendOp op)
    {
        switch (op)
        {
        case BlendOp::Add:
        default:
            return GL_FUNC_ADD;
        }
    }

    int OpenGLRenderBackend::InternalTextureFormatToGL(const GpuTextureFormat format, const bool srgb)
    {
        switch (format)
        {
        case GpuTextureFormat::RGBA16F:
            return GL_RGBA16F;
        case GpuTextureFormat::RGBA8:
        default:
            return srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        }
    }

    std::string OpenGLRenderBackend::GetExecutableDirectory()
    {
#ifdef _WIN32
        std::array<char, 4096> modulePath {};
        const unsigned long length = GetModuleFileNameA(nullptr, modulePath.data(), static_cast<unsigned long>(modulePath.size()));
        if (length > 0)
        {
            return std::filesystem::path(std::string(modulePath.data(), length)).parent_path().string();
        }
#endif
        return std::filesystem::current_path().string();
    }

    ResourceHandle OpenGLRenderBackend::AllocateHandle()
    {
        return m_NextHandle++;
    }
}
