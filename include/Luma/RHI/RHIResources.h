#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Luma
{
    using ResourceHandle = std::uint32_t;

    using RenderPassHandle = ResourceHandle;
    using PipelineStateHandle = ResourceHandle;
    using MeshHandle = ResourceHandle;
    using TextureHandle = ResourceHandle;
    using RenderTargetHandle = ResourceHandle;
    using FramebufferHandle = ResourceHandle;
    using DescriptorSetHandle = ResourceHandle;

    constexpr ResourceHandle InvalidResourceHandle = 0;
    constexpr TextureHandle InvalidTextureHandle = InvalidResourceHandle;

    struct Color
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
    };

    enum class PrimitiveTopology
    {
        TriangleList
    };

    enum class CullMode
    {
        None,
        Front,
        Back
    };

    enum class FrontFace
    {
        CounterClockwise,
        Clockwise
    };

    enum class VertexFormat
    {
        Float2,
        Float3,
        Float4
    };

    enum class IndexFormat
    {
        UInt16,
        UInt32
    };

    enum class DescriptorType
    {
        UniformBuffer,
        CombinedImageSampler
    };

    enum class GpuTextureFormat
    {
        RGBA8,
        RGBA16F
    };

    enum class BlendFactor
    {
        Zero,
        One,
        SrcAlpha,
        OneMinusSrcAlpha,
        DstColor
    };

    enum class BlendOp
    {
        Add
    };

    struct VertexAttributeDesc
    {
        std::uint32_t location = 0;
        VertexFormat format = VertexFormat::Float3;
        std::uint32_t offset = 0;
    };

    struct VertexLayoutDesc
    {
        std::uint32_t stride = 0;
        std::vector<VertexAttributeDesc> attributes;
    };

    struct DescriptorBindingDesc
    {
        std::uint32_t binding = 0;
        DescriptorType type = DescriptorType::UniformBuffer;
        std::uint32_t size = 0;
    };

    struct RenderPassDesc
    {
        std::string debugName;
        Color clearColor { 0.05f, 0.07f, 0.12f, 1.0f };
        FramebufferHandle framebuffer = InvalidResourceHandle;
    };

    struct ShaderProgramDesc
    {
        std::string vertexPath;
        std::string fragmentPath;
        bool vertexIsSpirv = false;
        bool fragmentIsSpirv = false;
    };

    struct TextureDesc
    {
        std::string debugName;
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        GpuTextureFormat format = GpuTextureFormat::RGBA8;
        bool srgb = true;
        std::vector<std::uint8_t> pixelData;
    };

    struct RenderTargetDesc
    {
        std::string debugName;
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        GpuTextureFormat format = GpuTextureFormat::RGBA8;
        bool srgb = true;
        Color clearColor { 0.05f, 0.07f, 0.12f, 1.0f };
    };

    struct FramebufferDesc
    {
        std::string debugName;
        RenderTargetHandle colorTarget = InvalidResourceHandle;
        bool presentToSwapchain = false;
    };

    struct PipelineStateDesc
    {
        std::string debugName;
        RenderPassHandle renderPass = InvalidResourceHandle;
        PrimitiveTopology topology = PrimitiveTopology::TriangleList;
        CullMode cullMode = CullMode::Back;
        FrontFace frontFace = FrontFace::CounterClockwise;
        struct DepthStencilStateDesc
        {
            bool depthTestEnabled = true;
            bool depthWriteEnabled = true;
        } depthStencilState;
        struct BlendStateDesc
        {
            bool enabled = false;
            BlendFactor srcColorFactor = BlendFactor::One;
            BlendFactor dstColorFactor = BlendFactor::Zero;
            BlendOp colorOp = BlendOp::Add;
            BlendFactor srcAlphaFactor = BlendFactor::One;
            BlendFactor dstAlphaFactor = BlendFactor::Zero;
            BlendOp alphaOp = BlendOp::Add;
        } blendState;
        VertexLayoutDesc vertexLayout;
        ShaderProgramDesc shaders;
        std::vector<DescriptorBindingDesc> descriptorBindings;
    };

    struct MeshDesc
    {
        std::vector<std::uint8_t> vertexData;
        std::vector<std::uint8_t> indexData;
        IndexFormat indexFormat = IndexFormat::UInt32;
    };

    struct DescriptorBufferWrite
    {
        std::uint32_t binding = 0;
        std::vector<std::uint8_t> data;
    };

    struct DescriptorImageWrite
    {
        std::uint32_t binding = 0;
        TextureHandle texture = InvalidResourceHandle;
        std::uint32_t width = 1;
        std::uint32_t height = 1;
        bool srgb = true;
        std::vector<std::uint8_t> pixels;
    };

    struct DescriptorSetDesc
    {
        std::vector<DescriptorBufferWrite> buffers;
        std::vector<DescriptorImageWrite> images;
    };

    inline std::uint32_t VertexFormatSize(const VertexFormat format)
    {
        switch (format)
        {
        case VertexFormat::Float2:
            return 8;
        case VertexFormat::Float3:
            return 12;
        case VertexFormat::Float4:
            return 16;
        default:
            return 0;
        }
    }

    inline std::uint32_t IndexFormatSize(const IndexFormat format)
    {
        switch (format)
        {
        case IndexFormat::UInt16:
            return 2;
        case IndexFormat::UInt32:
            return 4;
        default:
            return 0;
        }
    }
}
