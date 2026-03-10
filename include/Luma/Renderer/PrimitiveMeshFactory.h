#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Luma/RHI/RHIResources.h"

namespace Luma
{
    enum class PrimitiveType : std::uint8_t
    {
        Cube = 0,
        Plane = 1,
        Sphere = 2,
        Cylinder = 3,
        Capsule = 4,
        Cone = 5,
        Torus = 6
    };

    struct PrimitiveVertex
    {
        std::array<float, 3> position { 0.0f, 0.0f, 0.0f };
        std::array<float, 3> color { 1.0f, 1.0f, 1.0f };
        std::array<float, 2> uv { 0.0f, 0.0f };
    };

    struct PrimitiveMeshData
    {
        std::vector<PrimitiveVertex> vertices;
        std::vector<std::uint32_t> indices;
    };

    class PrimitiveMeshFactory
    {
    public:
        static const PrimitiveMeshData& GetPrimitive(PrimitiveType type);
        static const PrimitiveMeshData& GetPrimitive(PrimitiveType type, std::uint32_t lod);
        static const PrimitiveMeshData& GetSkySphere();
        static MeshDesc BuildMeshDesc(const PrimitiveMeshData& meshData);
        static MeshDesc BuildMeshDesc(PrimitiveType type, std::uint32_t lod);

    private:
        static PrimitiveMeshData GenerateCube();
        static PrimitiveMeshData GeneratePlane();
        static PrimitiveMeshData GenerateSphere(std::uint32_t segments = 24, std::uint32_t rings = 16);
        static PrimitiveMeshData GenerateCylinder(std::uint32_t segments = 32);
        static PrimitiveMeshData GenerateCapsule(std::uint32_t segments = 24, std::uint32_t hemisphereRings = 8);
        static PrimitiveMeshData GenerateCone(std::uint32_t segments = 32);
        static PrimitiveMeshData GenerateTorus(std::uint32_t majorSegments = 36, std::uint32_t minorSegments = 18);
    };
}
