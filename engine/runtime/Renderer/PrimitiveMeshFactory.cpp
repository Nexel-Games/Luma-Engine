#include "Luma/Renderer/PrimitiveMeshFactory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace Luma
{
    namespace
    {
        std::vector<std::uint8_t> ToByteVector(const void* data, const std::size_t sizeBytes)
        {
            std::vector<std::uint8_t> bytes(sizeBytes);
            if (data != nullptr && sizeBytes > 0)
            {
                std::memcpy(bytes.data(), data, sizeBytes);
            }
            return bytes;
        }
    }

    const PrimitiveMeshData& PrimitiveMeshFactory::GetPrimitive(const PrimitiveType type)
    {
        return GetPrimitive(type, 0);
    }

    const PrimitiveMeshData& PrimitiveMeshFactory::GetPrimitive(const PrimitiveType type, const std::uint32_t lod)
    {
        static const PrimitiveMeshData cube = GenerateCube();
        static const PrimitiveMeshData plane = GeneratePlane();
        static const std::array<PrimitiveMeshData, 4> sphereLods = {
            GenerateSphere(24, 16),
            GenerateSphere(18, 12),
            GenerateSphere(12, 8),
            GenerateSphere(8, 6)
        };
        static const std::array<PrimitiveMeshData, 4> cylinderLods = {
            GenerateCylinder(32),
            GenerateCylinder(20),
            GenerateCylinder(12),
            GenerateCylinder(8)
        };
        static const std::array<PrimitiveMeshData, 4> capsuleLods = {
            GenerateCapsule(24, 8),
            GenerateCapsule(18, 6),
            GenerateCapsule(12, 4),
            GenerateCapsule(8, 3)
        };
        static const std::array<PrimitiveMeshData, 4> coneLods = {
            GenerateCone(32),
            GenerateCone(20),
            GenerateCone(12),
            GenerateCone(8)
        };
        static const std::array<PrimitiveMeshData, 4> torusLods = {
            GenerateTorus(36, 18),
            GenerateTorus(24, 12),
            GenerateTorus(18, 10),
            GenerateTorus(12, 8)
        };
        const std::size_t lodIndex = static_cast<std::size_t>(std::min<std::uint32_t>(lod, 3));

        switch (type)
        {
        case PrimitiveType::Cylinder:
            return cylinderLods[lodIndex];
        case PrimitiveType::Capsule:
            return capsuleLods[lodIndex];
        case PrimitiveType::Cone:
            return coneLods[lodIndex];
        case PrimitiveType::Torus:
            return torusLods[lodIndex];
        case PrimitiveType::Plane:
            return plane;
        case PrimitiveType::Sphere:
            return sphereLods[lodIndex];
        case PrimitiveType::Cube:
        default:
            return cube;
        }
    }

    const PrimitiveMeshData& PrimitiveMeshFactory::GetSkySphere()
    {
        static const PrimitiveMeshData skySphere = GenerateSphere(128, 96);
        return skySphere;
    }

    MeshDesc PrimitiveMeshFactory::BuildMeshDesc(const PrimitiveMeshData& meshData)
    {
        MeshDesc meshDesc;
        meshDesc.indexFormat = IndexFormat::UInt32;
        meshDesc.vertexData = ToByteVector(
            meshData.vertices.data(),
            meshData.vertices.size() * sizeof(PrimitiveVertex));
        meshDesc.indexData = ToByteVector(
            meshData.indices.data(),
            meshData.indices.size() * sizeof(std::uint32_t));
        return meshDesc;
    }

    MeshDesc PrimitiveMeshFactory::BuildMeshDesc(const PrimitiveType type, const std::uint32_t lod)
    {
        return BuildMeshDesc(GetPrimitive(type, lod));
    }

    PrimitiveMeshData PrimitiveMeshFactory::GenerateCube()
    {
        PrimitiveMeshData data;

        constexpr float h = 0.5f;
        const std::array<PrimitiveVertex, 24> vertices = {
            PrimitiveVertex { { -h, -h,  h }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
            PrimitiveVertex { {  h, -h,  h }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
            PrimitiveVertex { {  h,  h,  h }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
            PrimitiveVertex { { -h,  h,  h }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },

            PrimitiveVertex { {  h, -h, -h }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
            PrimitiveVertex { { -h, -h, -h }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
            PrimitiveVertex { { -h,  h, -h }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
            PrimitiveVertex { {  h,  h, -h }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },

            PrimitiveVertex { { -h, -h, -h }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
            PrimitiveVertex { { -h, -h,  h }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
            PrimitiveVertex { { -h,  h,  h }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
            PrimitiveVertex { { -h,  h, -h }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },

            PrimitiveVertex { {  h, -h,  h }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
            PrimitiveVertex { {  h, -h, -h }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
            PrimitiveVertex { {  h,  h, -h }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
            PrimitiveVertex { {  h,  h,  h }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },

            PrimitiveVertex { { -h,  h,  h }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
            PrimitiveVertex { {  h,  h,  h }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
            PrimitiveVertex { {  h,  h, -h }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
            PrimitiveVertex { { -h,  h, -h }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } },

            PrimitiveVertex { { -h, -h, -h }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
            PrimitiveVertex { {  h, -h, -h }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
            PrimitiveVertex { {  h, -h,  h }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
            PrimitiveVertex { { -h, -h,  h }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } }
        };

        const std::array<std::uint32_t, 36> indices = {
            0, 1, 2, 0, 2, 3,
            4, 5, 6, 4, 6, 7,
            8, 9, 10, 8, 10, 11,
            12, 13, 14, 12, 14, 15,
            16, 17, 18, 16, 18, 19,
            20, 21, 22, 20, 22, 23
        };

        data.vertices.assign(vertices.begin(), vertices.end());
        data.indices.assign(indices.begin(), indices.end());
        return data;
    }

    PrimitiveMeshData PrimitiveMeshFactory::GeneratePlane()
    {
        PrimitiveMeshData data;

        constexpr float h = 0.5f;
        const std::array<PrimitiveVertex, 4> vertices = {
            PrimitiveVertex { { -h, 0.0f, -h }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f } },
            PrimitiveVertex { {  h, 0.0f, -h }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 0.0f } },
            PrimitiveVertex { {  h, 0.0f,  h }, { 1.0f, 1.0f, 1.0f }, { 1.0f, 1.0f } },
            PrimitiveVertex { { -h, 0.0f,  h }, { 1.0f, 1.0f, 1.0f }, { 0.0f, 1.0f } }
        };

        const std::array<std::uint32_t, 6> indices = { 0, 1, 2, 0, 2, 3 };

        data.vertices.assign(vertices.begin(), vertices.end());
        data.indices.assign(indices.begin(), indices.end());
        return data;
    }

    PrimitiveMeshData PrimitiveMeshFactory::GenerateSphere(const std::uint32_t segments, const std::uint32_t rings)
    {
        PrimitiveMeshData data;

        if (segments < 3 || rings < 2)
        {
            return data;
        }

        constexpr float radius = 0.5f;
        constexpr float kPi = 3.14159265359f;
        const std::uint32_t stride = segments + 1;
        data.vertices.reserve(static_cast<std::size_t>(stride) * static_cast<std::size_t>(rings + 1));

        for (std::uint32_t ring = 0; ring <= rings; ++ring)
        {
            const float v = static_cast<float>(ring) / static_cast<float>(rings);
            const float phi = v * kPi;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            for (std::uint32_t segment = 0; segment <= segments; ++segment)
            {
                const float u = static_cast<float>(segment) / static_cast<float>(segments);
                const float theta = u * 2.0f * kPi;
                const float sinTheta = std::sin(theta);
                const float cosTheta = std::cos(theta);

                PrimitiveVertex vertex;
                vertex.position = {
                    radius * sinPhi * cosTheta,
                    radius * cosPhi,
                    radius * sinPhi * sinTheta
                };
                vertex.color = { 1.0f, 1.0f, 1.0f };
                vertex.uv = { u, 1.0f - v };
                data.vertices.push_back(vertex);
            }
        }

        data.indices.reserve(static_cast<std::size_t>(segments) * static_cast<std::size_t>(rings) * 6);
        for (std::uint32_t ring = 0; ring < rings; ++ring)
        {
            for (std::uint32_t segment = 0; segment < segments; ++segment)
            {
                const std::uint32_t i0 = ring * stride + segment;
                const std::uint32_t i1 = i0 + 1;
                const std::uint32_t i2 = i0 + stride;
                const std::uint32_t i3 = i2 + 1;

                data.indices.push_back(i0);
                data.indices.push_back(i2);
                data.indices.push_back(i1);

                data.indices.push_back(i1);
                data.indices.push_back(i2);
                data.indices.push_back(i3);
            }
        }

        return data;
    }

    PrimitiveMeshData PrimitiveMeshFactory::GenerateCylinder(const std::uint32_t segments)
    {
        PrimitiveMeshData data;
        if (segments < 3)
        {
            return data;
        }

        constexpr float kPi = 3.14159265359f;
        constexpr float radius = 0.5f;
        constexpr float halfHeight = 0.5f;

        data.vertices.reserve(static_cast<std::size_t>(segments + 1) * 4 + 2);
        data.indices.reserve(static_cast<std::size_t>(segments) * 12);

        for (std::uint32_t segment = 0; segment <= segments; ++segment)
        {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float theta = u * 2.0f * kPi;
            const float x = std::cos(theta) * radius;
            const float z = std::sin(theta) * radius;

            PrimitiveVertex top;
            top.position = { x, halfHeight, z };
            top.color = { 1.0f, 1.0f, 1.0f };
            top.uv = { u, 0.0f };
            data.vertices.push_back(top);

            PrimitiveVertex bottom;
            bottom.position = { x, -halfHeight, z };
            bottom.color = { 1.0f, 1.0f, 1.0f };
            bottom.uv = { u, 1.0f };
            data.vertices.push_back(bottom);
        }

        for (std::uint32_t segment = 0; segment < segments; ++segment)
        {
            const std::uint32_t i0 = segment * 2;
            const std::uint32_t i1 = i0 + 1;
            const std::uint32_t i2 = i0 + 2;
            const std::uint32_t i3 = i0 + 3;

            data.indices.push_back(i0);
            data.indices.push_back(i1);
            data.indices.push_back(i2);

            data.indices.push_back(i2);
            data.indices.push_back(i1);
            data.indices.push_back(i3);
        }

        const std::uint32_t topCenterIndex = static_cast<std::uint32_t>(data.vertices.size());
        PrimitiveVertex topCenter;
        topCenter.position = { 0.0f, halfHeight, 0.0f };
        topCenter.color = { 1.0f, 1.0f, 1.0f };
        topCenter.uv = { 0.5f, 0.5f };
        data.vertices.push_back(topCenter);

        const std::uint32_t topRingStart = static_cast<std::uint32_t>(data.vertices.size());
        for (std::uint32_t segment = 0; segment <= segments; ++segment)
        {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float theta = u * 2.0f * kPi;
            const float x = std::cos(theta) * radius;
            const float z = std::sin(theta) * radius;

            PrimitiveVertex vertex;
            vertex.position = { x, halfHeight, z };
            vertex.color = { 1.0f, 1.0f, 1.0f };
            vertex.uv = { x / (radius * 2.0f) + 0.5f, z / (radius * 2.0f) + 0.5f };
            data.vertices.push_back(vertex);
        }

        for (std::uint32_t segment = 0; segment < segments; ++segment)
        {
            const std::uint32_t i0 = topRingStart + segment;
            const std::uint32_t i1 = i0 + 1;
            data.indices.push_back(topCenterIndex);
            data.indices.push_back(i0);
            data.indices.push_back(i1);
        }

        const std::uint32_t bottomCenterIndex = static_cast<std::uint32_t>(data.vertices.size());
        PrimitiveVertex bottomCenter;
        bottomCenter.position = { 0.0f, -halfHeight, 0.0f };
        bottomCenter.color = { 1.0f, 1.0f, 1.0f };
        bottomCenter.uv = { 0.5f, 0.5f };
        data.vertices.push_back(bottomCenter);

        const std::uint32_t bottomRingStart = static_cast<std::uint32_t>(data.vertices.size());
        for (std::uint32_t segment = 0; segment <= segments; ++segment)
        {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float theta = u * 2.0f * kPi;
            const float x = std::cos(theta) * radius;
            const float z = std::sin(theta) * radius;

            PrimitiveVertex vertex;
            vertex.position = { x, -halfHeight, z };
            vertex.color = { 1.0f, 1.0f, 1.0f };
            vertex.uv = { x / (radius * 2.0f) + 0.5f, z / (radius * 2.0f) + 0.5f };
            data.vertices.push_back(vertex);
        }

        for (std::uint32_t segment = 0; segment < segments; ++segment)
        {
            const std::uint32_t i0 = bottomRingStart + segment;
            const std::uint32_t i1 = i0 + 1;
            data.indices.push_back(bottomCenterIndex);
            data.indices.push_back(i1);
            data.indices.push_back(i0);
        }

        return data;
    }

    PrimitiveMeshData PrimitiveMeshFactory::GenerateCapsule(const std::uint32_t segments, const std::uint32_t hemisphereRings)
    {
        PrimitiveMeshData data;
        if (segments < 3 || hemisphereRings < 2)
        {
            return data;
        }

        constexpr float kPi = 3.14159265359f;
        constexpr float radius = 0.25f;
        constexpr float cylinderHalfHeight = 0.25f;
        constexpr float totalHalfHeight = radius + cylinderHalfHeight;

        struct Ring
        {
            float y = 0.0f;
            float ringRadius = 0.0f;
        };

        std::vector<Ring> rings;
        rings.reserve(static_cast<std::size_t>(hemisphereRings) * 2 + 2);

        rings.push_back({ totalHalfHeight, 0.0f });
        for (std::uint32_t ring = 1; ring < hemisphereRings; ++ring)
        {
            const float t = static_cast<float>(ring) / static_cast<float>(hemisphereRings);
            const float angle = t * (kPi * 0.5f);
            rings.push_back({
                cylinderHalfHeight + radius * std::cos(angle),
                radius * std::sin(angle)
            });
        }
        rings.push_back({ cylinderHalfHeight, radius });
        rings.push_back({ -cylinderHalfHeight, radius });
        for (std::uint32_t ring = 1; ring < hemisphereRings; ++ring)
        {
            const float t = static_cast<float>(ring) / static_cast<float>(hemisphereRings);
            const float angle = t * (kPi * 0.5f);
            rings.push_back({
                -cylinderHalfHeight - radius * std::sin(angle),
                radius * std::cos(angle)
            });
        }
        rings.push_back({ -totalHalfHeight, 0.0f });

        const std::uint32_t ringStride = segments + 1;
        data.vertices.reserve(static_cast<std::size_t>(rings.size()) * static_cast<std::size_t>(ringStride));
        data.indices.reserve((rings.size() - 1) * static_cast<std::size_t>(segments) * 6);

        for (std::size_t ringIndex = 0; ringIndex < rings.size(); ++ringIndex)
        {
            const Ring& ring = rings[ringIndex];
            for (std::uint32_t segment = 0; segment <= segments; ++segment)
            {
                const float u = static_cast<float>(segment) / static_cast<float>(segments);
                const float theta = u * 2.0f * kPi;
                const float x = std::cos(theta) * ring.ringRadius;
                const float z = std::sin(theta) * ring.ringRadius;

                PrimitiveVertex vertex;
                vertex.position = { x, ring.y, z };
                vertex.color = { 1.0f, 1.0f, 1.0f };
                vertex.uv = { u, 0.5f - (ring.y / (totalHalfHeight * 2.0f)) };
                data.vertices.push_back(vertex);
            }
        }

        for (std::size_t ringIndex = 0; ringIndex + 1 < rings.size(); ++ringIndex)
        {
            const std::uint32_t base = static_cast<std::uint32_t>(ringIndex * ringStride);
            const std::uint32_t nextBase = base + ringStride;
            for (std::uint32_t segment = 0; segment < segments; ++segment)
            {
                const std::uint32_t i0 = base + segment;
                const std::uint32_t i1 = i0 + 1;
                const std::uint32_t i2 = nextBase + segment;
                const std::uint32_t i3 = i2 + 1;

                data.indices.push_back(i0);
                data.indices.push_back(i2);
                data.indices.push_back(i1);

                data.indices.push_back(i1);
                data.indices.push_back(i2);
                data.indices.push_back(i3);
            }
        }

        return data;
    }

    PrimitiveMeshData PrimitiveMeshFactory::GenerateCone(const std::uint32_t segments)
    {
        PrimitiveMeshData data;
        if (segments < 3)
        {
            return data;
        }

        constexpr float kPi = 3.14159265359f;
        constexpr float radius = 0.5f;
        constexpr float halfHeight = 0.5f;
        const std::uint32_t apexIndex = 0;

        data.vertices.reserve(static_cast<std::size_t>(segments + 1) * 2 + 2);
        data.indices.reserve(static_cast<std::size_t>(segments) * 6);

        PrimitiveVertex apex;
        apex.position = { 0.0f, halfHeight, 0.0f };
        apex.color = { 1.0f, 1.0f, 1.0f };
        apex.uv = { 0.5f, 0.0f };
        data.vertices.push_back(apex);

        const std::uint32_t sideRingStart = static_cast<std::uint32_t>(data.vertices.size());
        for (std::uint32_t segment = 0; segment <= segments; ++segment)
        {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float theta = u * 2.0f * kPi;
            const float x = std::cos(theta) * radius;
            const float z = std::sin(theta) * radius;

            PrimitiveVertex vertex;
            vertex.position = { x, -halfHeight, z };
            vertex.color = { 1.0f, 1.0f, 1.0f };
            vertex.uv = { u, 1.0f };
            data.vertices.push_back(vertex);
        }

        for (std::uint32_t segment = 0; segment < segments; ++segment)
        {
            const std::uint32_t i0 = sideRingStart + segment;
            const std::uint32_t i1 = i0 + 1;
            data.indices.push_back(apexIndex);
            data.indices.push_back(i1);
            data.indices.push_back(i0);
        }

        const std::uint32_t baseCenterIndex = static_cast<std::uint32_t>(data.vertices.size());
        PrimitiveVertex baseCenter;
        baseCenter.position = { 0.0f, -halfHeight, 0.0f };
        baseCenter.color = { 1.0f, 1.0f, 1.0f };
        baseCenter.uv = { 0.5f, 0.5f };
        data.vertices.push_back(baseCenter);

        const std::uint32_t baseRingStart = static_cast<std::uint32_t>(data.vertices.size());
        for (std::uint32_t segment = 0; segment <= segments; ++segment)
        {
            const float u = static_cast<float>(segment) / static_cast<float>(segments);
            const float theta = u * 2.0f * kPi;
            const float x = std::cos(theta) * radius;
            const float z = std::sin(theta) * radius;

            PrimitiveVertex vertex;
            vertex.position = { x, -halfHeight, z };
            vertex.color = { 1.0f, 1.0f, 1.0f };
            vertex.uv = { x / (radius * 2.0f) + 0.5f, z / (radius * 2.0f) + 0.5f };
            data.vertices.push_back(vertex);
        }

        for (std::uint32_t segment = 0; segment < segments; ++segment)
        {
            const std::uint32_t i0 = baseRingStart + segment;
            const std::uint32_t i1 = i0 + 1;
            data.indices.push_back(baseCenterIndex);
            data.indices.push_back(i0);
            data.indices.push_back(i1);
        }

        return data;
    }

    PrimitiveMeshData PrimitiveMeshFactory::GenerateTorus(
        const std::uint32_t majorSegments,
        const std::uint32_t minorSegments)
    {
        PrimitiveMeshData data;
        if (majorSegments < 3 || minorSegments < 3)
        {
            return data;
        }

        constexpr float kPi = 3.14159265359f;
        constexpr float majorRadius = 0.35f;
        constexpr float minorRadius = 0.15f;

        const std::uint32_t minorStride = minorSegments + 1;
        data.vertices.reserve(static_cast<std::size_t>(majorSegments + 1) * static_cast<std::size_t>(minorStride));
        data.indices.reserve(static_cast<std::size_t>(majorSegments) * static_cast<std::size_t>(minorSegments) * 6);

        for (std::uint32_t major = 0; major <= majorSegments; ++major)
        {
            const float u = static_cast<float>(major) / static_cast<float>(majorSegments);
            const float phi = u * 2.0f * kPi;
            const float cosPhi = std::cos(phi);
            const float sinPhi = std::sin(phi);

            for (std::uint32_t minor = 0; minor <= minorSegments; ++minor)
            {
                const float v = static_cast<float>(minor) / static_cast<float>(minorSegments);
                const float theta = v * 2.0f * kPi;
                const float cosTheta = std::cos(theta);
                const float sinTheta = std::sin(theta);
                const float radial = majorRadius + minorRadius * cosTheta;

                PrimitiveVertex vertex;
                vertex.position = {
                    radial * cosPhi,
                    minorRadius * sinTheta,
                    radial * sinPhi
                };
                vertex.color = { 1.0f, 1.0f, 1.0f };
                vertex.uv = { u, v };
                data.vertices.push_back(vertex);
            }
        }

        for (std::uint32_t major = 0; major < majorSegments; ++major)
        {
            const std::uint32_t row = major * minorStride;
            const std::uint32_t nextRow = row + minorStride;
            for (std::uint32_t minor = 0; minor < minorSegments; ++minor)
            {
                const std::uint32_t i0 = row + minor;
                const std::uint32_t i1 = i0 + 1;
                const std::uint32_t i2 = nextRow + minor;
                const std::uint32_t i3 = i2 + 1;

                data.indices.push_back(i0);
                data.indices.push_back(i2);
                data.indices.push_back(i1);

                data.indices.push_back(i1);
                data.indices.push_back(i2);
                data.indices.push_back(i3);
            }
        }

        return data;
    }
}
