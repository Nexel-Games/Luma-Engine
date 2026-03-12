#include "Physics/Backends/PhysXBackend.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#if defined(LUMA_ENABLE_PHYSX) && LUMA_ENABLE_PHYSX
#include <PxPhysicsAPI.h>
#include <characterkinematic/PxCapsuleController.h>
#include <characterkinematic/PxControllerManager.h>
#include <cooking/PxCooking.h>
#include <extensions/PxD6Joint.h>
#include <extensions/PxFixedJoint.h>
#include <extensions/PxJoint.h>
#include <extensions/PxJointLimit.h>
#include <extensions/PxPrismaticJoint.h>
#include <extensions/PxRevoluteJoint.h>
#include <geometry/PxMeshScale.h>
#endif
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "Luma/Asset/Core/MeshAssetIO.h"
#include "Luma/Core/App/Project.h"
#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Renderer/PrimitiveMeshFactory.h"
#include "Luma/Scene/CharacterControllerComponent.h"
#include "Luma/Scene/ColliderComponent.h"
#include "Luma/Scene/D6JointComponent.h"
#include "Luma/Scene/DestructibleComponent.h"
#include "Luma/Scene/FixedJointComponent.h"
#include "Luma/Scene/HingeJointComponent.h"
#include "Luma/Scene/JointComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/RigidBodyComponent.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SliderJointComponent.h"
#include "Luma/Scene/TransformComponent.h"
#include "Luma/Scene/VehicleComponent.h"
#include "Luma/Scene/VehicleInputComponent.h"
#include "Luma/Scene/WheelColliderComponent.h"

namespace Luma
{
    namespace
    {
        struct ColliderProxy
        {
            EntityID entity = entt::null;
            TransformComponent* transform = nullptr;
            RigidBodyComponent* rigidBody = nullptr;
            const ColliderComponent* collider = nullptr;
            std::array<float, 3> center { 0.0f, 0.0f, 0.0f };
            std::array<float, 3> extents { 0.5f, 0.5f, 0.5f };
            bool dynamicBody = false;
            bool trigger = false;
        };

        struct CachedMeshHalfExtents
        {
            bool valid = false;
            std::array<float, 3> halfExtents { 0.5f, 0.5f, 0.5f };
        };

        std::unordered_map<std::string, CachedMeshHalfExtents> g_MeshHalfExtentsCache;
        std::mutex g_MeshHalfExtentsCacheMutex;

        std::string ToLowerCopy(std::string text)
        {
            std::transform(
                text.begin(),
                text.end(),
                text.begin(),
                [](const unsigned char value)
                {
                    return static_cast<char>(std::tolower(value));
                });
            return text;
        }

        bool ReadFileBytes(const std::filesystem::path& filePath, std::vector<std::uint8_t>& outBytes)
        {
            outBytes.clear();
            std::ifstream file(filePath, std::ios::binary | std::ios::ate);
            if (!file)
            {
                return false;
            }

            const std::streamsize fileSize = file.tellg();
            if (fileSize <= 0)
            {
                return false;
            }
            file.seekg(0, std::ios::beg);

            outBytes.resize(static_cast<std::size_t>(fileSize));
            file.read(reinterpret_cast<char*>(outBytes.data()), fileSize);
            return file.good() || file.eof();
        }

        struct IntermediateSourceData
        {
            std::vector<std::uint8_t> payload;
            const std::uint8_t* sourceBytes = nullptr;
            std::size_t sourceSize = 0;
            std::string sourceName;
        };

        bool LoadIntermediateSourceData(const std::filesystem::path& filePath, IntermediateSourceData& outData)
        {
            outData = {};
            if (!ReadFileBytes(filePath, outData.payload))
            {
                return false;
            }

            const std::string_view payloadView(
                reinterpret_cast<const char*>(outData.payload.data()),
                outData.payload.size());

            std::size_t headerEnd = payloadView.find("\n\n");
            std::size_t separatorLength = 2;
            if (headerEnd == std::string_view::npos)
            {
                headerEnd = payloadView.find("\r\n\r\n");
                separatorLength = 4;
            }
            if (headerEnd == std::string_view::npos)
            {
                return false;
            }

            std::istringstream headerStream(std::string(payloadView.substr(0, headerEnd)));
            std::string line;
            while (std::getline(headerStream, line))
            {
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                if (line.rfind("Source=", 0) == 0)
                {
                    outData.sourceName = line.substr(std::strlen("Source="));
                    break;
                }
            }

            const std::size_t sourceOffset = headerEnd + separatorLength;
            if (sourceOffset >= outData.payload.size())
            {
                return false;
            }

            outData.sourceBytes = outData.payload.data() + sourceOffset;
            outData.sourceSize = outData.payload.size() - sourceOffset;
            return outData.sourceSize > 0;
        }

        std::array<float, 3> ComputePrimitiveHalfExtents(const PrimitiveType primitive)
        {
            const PrimitiveMeshData& meshData = PrimitiveMeshFactory::GetPrimitive(primitive);
            if (meshData.vertices.empty())
            {
                return { 0.5f, 0.5f, 0.5f };
            }

            std::array<float, 3> minBounds {
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max(),
                std::numeric_limits<float>::max()
            };
            std::array<float, 3> maxBounds {
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest(),
                std::numeric_limits<float>::lowest()
            };

            for (const PrimitiveVertex& vertex : meshData.vertices)
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    minBounds[axis] = std::min(minBounds[axis], vertex.position[axis]);
                    maxBounds[axis] = std::max(maxBounds[axis], vertex.position[axis]);
                }
            }

            return {
                std::max(0.001f, (maxBounds[0] - minBounds[0]) * 0.5f),
                std::max(0.001f, (maxBounds[1] - minBounds[1]) * 0.5f),
                std::max(0.001f, (maxBounds[2] - minBounds[2]) * 0.5f)
            };
        }

        bool IsSupportedMeshExtension(const std::filesystem::path& path)
        {
            const std::string extension = ToLowerCopy(path.extension().string());
            return extension == ".lumamesh" ||
                extension == ".obj" ||
                extension == ".fbx" ||
                extension == ".gltf" ||
                extension == ".glb";
        }

        std::filesystem::path ResolveMeshSourcePath(const std::string& meshSource)
        {
            return Assets::ResolveMeshAssetPath(meshSource);
        }

        bool LoadMeshHalfExtentsFromFile(
            const std::filesystem::path& meshPath,
            std::array<float, 3>& outHalfExtents)
        {
            std::string error;
            return Assets::GetMeshAssetHalfExtents(meshPath, outHalfExtents, error);
        }

        bool GetMeshHalfExtentsCached(
            const std::string& meshSource,
            std::array<float, 3>& outHalfExtents)
        {
            outHalfExtents = { 0.5f, 0.5f, 0.5f };
            if (meshSource.empty())
            {
                return false;
            }

            const std::string projectKey = Project::IsLoaded() ? Project::GetProjectRoot().string() : std::string {};
            const std::string cacheKey = projectKey + "|" + ToLowerCopy(meshSource);

            {
                const std::lock_guard<std::mutex> lock(g_MeshHalfExtentsCacheMutex);
                const auto found = g_MeshHalfExtentsCache.find(cacheKey);
                if (found != g_MeshHalfExtentsCache.end())
                {
                    outHalfExtents = found->second.halfExtents;
                    return found->second.valid;
                }
            }

            CachedMeshHalfExtents loaded {};
            loaded.valid = false;
            const std::filesystem::path resolvedPath = ResolveMeshSourcePath(meshSource);
            if (!resolvedPath.empty())
            {
                loaded.valid = LoadMeshHalfExtentsFromFile(resolvedPath, loaded.halfExtents);
            }

            {
                const std::lock_guard<std::mutex> lock(g_MeshHalfExtentsCacheMutex);
                g_MeshHalfExtentsCache[cacheKey] = loaded;
            }

            outHalfExtents = loaded.halfExtents;
            return loaded.valid;
        }

        float ComputeLength(const std::array<float, 3>& value)
        {
            return std::sqrt(value[0] * value[0] + value[1] * value[1] + value[2] * value[2]);
        }

        std::array<float, 3> Negate(const std::array<float, 3>& value)
        {
            return { -value[0], -value[1], -value[2] };
        }

        void ClampVectorMagnitude(std::array<float, 3>& value, const float maxMagnitude)
        {
            if (maxMagnitude <= 0.0f)
            {
                value = { 0.0f, 0.0f, 0.0f };
                return;
            }

            const float length = ComputeLength(value);
            if (length <= maxMagnitude || length <= 1.0e-6f)
            {
                return;
            }

            const float scale = maxMagnitude / length;
            value[0] *= scale;
            value[1] *= scale;
            value[2] *= scale;
        }

        int DominantAxis(const std::array<float, 3>& value)
        {
            float bestMagnitude = std::abs(value[0]);
            int axis = 0;
            for (int i = 1; i < 3; ++i)
            {
                const float magnitude = std::abs(value[i]);
                if (magnitude > bestMagnitude)
                {
                    bestMagnitude = magnitude;
                    axis = i;
                }
            }
            return axis;
        }

        bool ComputeAabbMinimumTranslation(
            const ColliderProxy& first,
            const ColliderProxy& second,
            std::array<float, 3>& outTranslationForFirst)
        {
            const float deltaX = second.center[0] - first.center[0];
            const float deltaY = second.center[1] - first.center[1];
            const float deltaZ = second.center[2] - first.center[2];

            const float overlapX = first.extents[0] + second.extents[0] - std::abs(deltaX);
            const float overlapY = first.extents[1] + second.extents[1] - std::abs(deltaY);
            const float overlapZ = first.extents[2] + second.extents[2] - std::abs(deltaZ);
            if (overlapX <= 0.0f || overlapY <= 0.0f || overlapZ <= 0.0f)
            {
                return false;
            }

            int axis = 0;
            float overlap = overlapX;
            if (overlapY < overlap)
            {
                overlap = overlapY;
                axis = 1;
            }
            if (overlapZ < overlap)
            {
                overlap = overlapZ;
                axis = 2;
            }

            outTranslationForFirst = { 0.0f, 0.0f, 0.0f };
            const float sign = (axis == 0 ? deltaX : (axis == 1 ? deltaY : deltaZ)) >= 0.0f ? -1.0f : 1.0f;
            outTranslationForFirst[axis] = sign * overlap;
            return true;
        }

        void ApplyTranslation(ColliderProxy& proxy, const std::array<float, 3>& translation)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                proxy.transform->position[axis] += translation[axis];
                proxy.center[axis] += translation[axis];
            }
            proxy.transform->dirty = true;
        }

        void ZeroVelocityOnResolvedAxis(RigidBodyComponent& rigidBody, const std::array<float, 3>& translation)
        {
            const int axis = DominantAxis(translation);
            rigidBody.linearVelocity[axis] = 0.0f;
        }

        ColliderProxy BuildColliderProxy(
            const EntityID entity,
            TransformComponent& transform,
            const ColliderComponent& collider,
            RigidBodyComponent* rigidBody,
            const MeshRendererComponent* meshRenderer)
        {
            ColliderProxy proxy {};
            proxy.entity = entity;
            proxy.transform = &transform;
            proxy.rigidBody = rigidBody;
            proxy.collider = &collider;
            proxy.trigger = collider.isTrigger;
            proxy.dynamicBody = rigidBody != nullptr &&
                rigidBody->active &&
                rigidBody->bodyType == RigidBodyType::Dynamic;

            const std::array<float, 3> absScale {
                std::max(0.001f, std::abs(transform.scale[0])),
                std::max(0.001f, std::abs(transform.scale[1])),
                std::max(0.001f, std::abs(transform.scale[2]))
            };

            for (int axis = 0; axis < 3; ++axis)
            {
                proxy.center[axis] = transform.position[axis] + collider.center[axis] * absScale[axis];
            }

            switch (collider.shape)
            {
            case ColliderShapeType::Sphere:
            {
                const float radius = std::max(0.001f, collider.sphereRadius) *
                    std::max(absScale[0], std::max(absScale[1], absScale[2]));
                proxy.extents = { radius, radius, radius };
                break;
            }
            case ColliderShapeType::Capsule:
            {
                const float radius = std::max(0.001f, collider.capsuleRadius) * std::max(absScale[0], absScale[2]);
                const float halfHeight = std::max(0.001f, collider.capsuleHalfHeight) * absScale[1];
                proxy.extents = { radius, halfHeight + radius, radius };
                break;
            }
            case ColliderShapeType::Cylinder:
            {
                const float radius = std::max(0.001f, collider.capsuleRadius) * std::max(absScale[0], absScale[2]);
                const float halfHeight = std::max(0.001f, collider.capsuleHalfHeight) * absScale[1];
                proxy.extents = { radius, halfHeight, radius };
                break;
            }
            case ColliderShapeType::Mesh:
            {
                if (meshRenderer != nullptr && meshRenderer->usePrimitive)
                {
                    const std::array<float, 3> primitiveHalfExtents =
                        ComputePrimitiveHalfExtents(meshRenderer->primitive);
                    proxy.extents = {
                        std::max(0.001f, primitiveHalfExtents[0] * absScale[0]),
                        std::max(
                            meshRenderer->primitive == PrimitiveType::Plane ? 0.02f : 0.001f,
                            primitiveHalfExtents[1] * absScale[1]),
                        std::max(0.001f, primitiveHalfExtents[2] * absScale[2])
                    };
                }
                else if (!collider.meshSource.empty())
                {
                    std::array<float, 3> meshHalfExtents {};
                    if (GetMeshHalfExtentsCached(collider.meshSource, meshHalfExtents))
                    {
                        proxy.extents = {
                            std::max(0.001f, meshHalfExtents[0] * absScale[0]),
                            std::max(0.001f, meshHalfExtents[1] * absScale[1]),
                            std::max(0.001f, meshHalfExtents[2] * absScale[2])
                        };
                    }
                    else
                    {
                        proxy.extents = {
                            std::max(0.001f, collider.boxHalfExtents[0] * absScale[0]),
                            std::max(0.001f, collider.boxHalfExtents[1] * absScale[1]),
                            std::max(0.001f, collider.boxHalfExtents[2] * absScale[2])
                        };
                    }
                }
                else
                {
                    proxy.extents = {
                        std::max(0.001f, collider.boxHalfExtents[0] * absScale[0]),
                        std::max(0.001f, collider.boxHalfExtents[1] * absScale[1]),
                        std::max(0.001f, collider.boxHalfExtents[2] * absScale[2])
                    };
                }
                break;
            }
            case ColliderShapeType::Box:
            default:
                proxy.extents = {
                    std::max(0.001f, collider.boxHalfExtents[0] * absScale[0]),
                    std::max(0.001f, collider.boxHalfExtents[1] * absScale[1]),
                    std::max(0.001f, collider.boxHalfExtents[2] * absScale[2])
                };
                break;
            }

            return proxy;
        }

#if defined(LUMA_ENABLE_PHYSX) && LUMA_ENABLE_PHYSX
        constexpr float kPi = 3.14159265359f;

        float DegreesToRadians(const float degrees)
        {
            return degrees * (kPi / 180.0f);
        }

        float RadiansToDegrees(const float radians)
        {
            return radians * (180.0f / kPi);
        }

        physx::PxVec3 ToPxVec3(const std::array<float, 3>& value)
        {
            return physx::PxVec3(value[0], value[1], value[2]);
        }

        std::array<float, 3> FromPxVec3(const physx::PxVec3& value)
        {
            return { value.x, value.y, value.z };
        }

        physx::PxExtendedVec3 ToPxExtendedVec3(const std::array<float, 3>& value)
        {
            return physx::PxExtendedVec3(
                static_cast<physx::PxExtended>(value[0]),
                static_cast<physx::PxExtended>(value[1]),
                static_cast<physx::PxExtended>(value[2]));
        }

        std::array<float, 3> FromPxExtendedVec3(const physx::PxExtendedVec3& value)
        {
            return {
                static_cast<float>(value.x),
                static_cast<float>(value.y),
                static_cast<float>(value.z)
            };
        }

        physx::PxQuat ToPxQuatDegrees(const std::array<float, 3>& rotationDegrees)
        {
            const physx::PxQuat qx(DegreesToRadians(rotationDegrees[0]), physx::PxVec3(1.0f, 0.0f, 0.0f));
            const physx::PxQuat qy(DegreesToRadians(rotationDegrees[1]), physx::PxVec3(0.0f, 1.0f, 0.0f));
            const physx::PxQuat qz(DegreesToRadians(rotationDegrees[2]), physx::PxVec3(0.0f, 0.0f, 1.0f));
            return qz * qy * qx;
        }

        std::array<float, 3> FromPxQuatDegrees(const physx::PxQuat& value)
        {
            const float sinrCosp = 2.0f * (value.w * value.x + value.y * value.z);
            const float cosrCosp = 1.0f - 2.0f * (value.x * value.x + value.y * value.y);
            const float roll = std::atan2(sinrCosp, cosrCosp);

            const float sinp = 2.0f * (value.w * value.y - value.z * value.x);
            const float pitch =
                std::abs(sinp) >= 1.0f ? std::copysign(kPi * 0.5f, sinp) : std::asin(sinp);

            const float sinyCosp = 2.0f * (value.w * value.z + value.x * value.y);
            const float cosyCosp = 1.0f - 2.0f * (value.y * value.y + value.z * value.z);
            const float yaw = std::atan2(sinyCosp, cosyCosp);

            return { RadiansToDegrees(roll), RadiansToDegrees(pitch), RadiansToDegrees(yaw) };
        }

        std::array<float, 3> AbsoluteScale(const TransformComponent& transform)
        {
            return {
                std::max(0.001f, std::abs(transform.scale[0])),
                std::max(0.001f, std::abs(transform.scale[1])),
                std::max(0.001f, std::abs(transform.scale[2]))
            };
        }

        float CharacterGravityMultiplier(const CharacterMovementMode movementMode)
        {
            switch (movementMode)
            {
            case CharacterMovementMode::Fly:
                return 0.0f;
            case CharacterMovementMode::Swim:
                return 0.25f;
            case CharacterMovementMode::Walk:
            default:
                return 1.0f;
            }
        }

        std::uint8_t ToMovementModeValue(const CharacterMovementMode movementMode)
        {
            return static_cast<std::uint8_t>(movementMode);
        }

        float BuildControllerRadius(const TransformComponent& transform, const CharacterControllerComponent& controller)
        {
            const std::array<float, 3> scale = AbsoluteScale(transform);
            return std::max(0.01f, controller.radius * std::max(scale[0], scale[2]));
        }

        float BuildControllerTotalHeight(const TransformComponent& transform, const CharacterControllerComponent& controller)
        {
            const std::array<float, 3> scale = AbsoluteScale(transform);
            return std::max(controller.radius * 2.0f + 0.01f, controller.height * scale[1]);
        }

        float BuildControllerCapsuleHeight(const float radius, const float totalHeight)
        {
            return std::max(0.01f, totalHeight - radius * 2.0f);
        }

        bool NearlyZeroVector(const std::array<float, 3>& value, const float epsilon = 1.0e-5f)
        {
            return std::abs(value[0]) <= epsilon &&
                std::abs(value[1]) <= epsilon &&
                std::abs(value[2]) <= epsilon;
        }

        std::array<float, 3> Add(const std::array<float, 3>& left, const std::array<float, 3>& right)
        {
            return { left[0] + right[0], left[1] + right[1], left[2] + right[2] };
        }

        std::array<float, 3> Subtract(const std::array<float, 3>& left, const std::array<float, 3>& right)
        {
            return { left[0] - right[0], left[1] - right[1], left[2] - right[2] };
        }

        std::array<float, 3> Multiply(const std::array<float, 3>& value, const float scalar)
        {
            return { value[0] * scalar, value[1] * scalar, value[2] * scalar };
        }

        float DotProduct(const std::array<float, 3>& left, const std::array<float, 3>& right)
        {
            return left[0] * right[0] + left[1] * right[1] + left[2] * right[2];
        }

        std::array<float, 3> CrossProduct(const std::array<float, 3>& left, const std::array<float, 3>& right)
        {
            return {
                left[1] * right[2] - left[2] * right[1],
                left[2] * right[0] - left[0] * right[2],
                left[0] * right[1] - left[1] * right[0]
            };
        }

        std::array<float, 3> NormalizeArrayOrFallback(
            const std::array<float, 3>& value,
            const std::array<float, 3>& fallback)
        {
            const float length = ComputeLength(value);
            if (length <= 1.0e-6f)
            {
                return fallback;
            }

            return { value[0] / length, value[1] / length, value[2] / length };
        }

        std::string PrimitiveCollisionKey(const PrimitiveType primitive)
        {
            return "primitive:" + std::to_string(static_cast<int>(primitive));
        }

        bool LoadCollisionMesh(
            const ColliderComponent& collider,
            const MeshRendererComponent* meshRenderer,
            PrimitiveMeshData& outMesh,
            std::string& outKey)
        {
            if (meshRenderer != nullptr && meshRenderer->usePrimitive)
            {
                outMesh = PrimitiveMeshFactory::GetPrimitive(meshRenderer->primitive);
                outKey = PrimitiveCollisionKey(meshRenderer->primitive);
                return !outMesh.vertices.empty() && !outMesh.indices.empty();
            }

            if (collider.meshSource.empty())
            {
                return false;
            }

            const std::filesystem::path resolvedPath = ResolveMeshSourcePath(collider.meshSource);
            if (resolvedPath.empty())
            {
                return false;
            }

            Assets::MeshAssetData meshAsset;
            std::string error;
            if (!Assets::LoadMeshAssetData(resolvedPath, meshAsset, error))
            {
                LUMA_LOG_WARN("Physics", "Failed to load collider mesh '" + resolvedPath.string() + "': " + error);
                return false;
            }

            outMesh = meshAsset.mesh;
            outKey = resolvedPath.generic_string();
            return !outMesh.vertices.empty() && !outMesh.indices.empty();
        }

        physx::PxRigidDynamicLockFlags BuildLockFlags(const RigidBodyComponent& rigidBody)
        {
            physx::PxRigidDynamicLockFlags flags;
            if (rigidBody.lockLinearAxes[0])
            {
                flags |= physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
            }
            if (rigidBody.lockLinearAxes[1])
            {
                flags |= physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
            }
            if (rigidBody.lockLinearAxes[2])
            {
                flags |= physx::PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
            }
            if (rigidBody.lockAngularAxes[0])
            {
                flags |= physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
            }
            if (rigidBody.lockAngularAxes[1])
            {
                flags |= physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
            }
            if (rigidBody.lockAngularAxes[2])
            {
                flags |= physx::PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
            }
            return flags;
        }

        bool NearlyEqual(const float left, const float right, const float epsilon = 1.0e-4f)
        {
            return std::abs(left - right) <= epsilon;
        }

        template <std::size_t Size>
        bool NearlyEqualArray(
            const std::array<float, Size>& left,
            const std::array<float, Size>& right,
            const float epsilon = 1.0e-4f)
        {
            for (std::size_t index = 0; index < Size; ++index)
            {
                if (!NearlyEqual(left[index], right[index], epsilon))
                {
                    return false;
                }
            }
            return true;
        }

        bool EqualMaterialDesc(const PhysicsMaterialDesc& left, const PhysicsMaterialDesc& right)
        {
            return NearlyEqual(left.staticFriction, right.staticFriction) &&
                NearlyEqual(left.dynamicFriction, right.dynamicFriction) &&
                NearlyEqual(left.restitution, right.restitution);
        }

        physx::PxVec3 NormalizeOrFallback(const physx::PxVec3& vector, const physx::PxVec3& fallback)
        {
            if (vector.magnitudeSquared() <= 1.0e-8f)
            {
                return fallback;
            }

            return vector.getNormalized();
        }

        physx::PxTransform BuildAxisJointFrame(
            const TransformComponent& transform,
            const std::array<float, 3>& axis)
        {
            const physx::PxQuat baseRotation = ToPxQuatDegrees(transform.rotation);
            const physx::PxVec3 worldAxis = NormalizeOrFallback(
                baseRotation.rotate(physx::PxVec3(axis[0], axis[1], axis[2])),
                baseRotation.rotate(physx::PxVec3(1.0f, 0.0f, 0.0f)));

            physx::PxVec3 upHint = baseRotation.rotate(physx::PxVec3(0.0f, 1.0f, 0.0f));
            if (std::abs(worldAxis.dot(upHint)) > 0.95f)
            {
                upHint = baseRotation.rotate(physx::PxVec3(0.0f, 0.0f, 1.0f));
            }

            physx::PxVec3 axisZ = NormalizeOrFallback(worldAxis.cross(upHint), physx::PxVec3(0.0f, 0.0f, 1.0f));
            physx::PxVec3 axisY = NormalizeOrFallback(axisZ.cross(worldAxis), physx::PxVec3(0.0f, 1.0f, 0.0f));
            const physx::PxMat33 basis(worldAxis, axisY, axisZ);
            return physx::PxTransform(ToPxVec3(transform.position), physx::PxQuat(basis));
        }

        physx::PxTransform BuildLocalJointFrame(
            physx::PxRigidActor* actor,
            const physx::PxTransform& jointWorldFrame)
        {
            return actor != nullptr ? actor->getGlobalPose().transformInv(jointWorldFrame) : jointWorldFrame;
        }

        physx::PxD6Motion::Enum ToPxD6Motion(const JointMotionMode motion)
        {
            switch (motion)
            {
            case JointMotionMode::Free:
                return physx::PxD6Motion::eFREE;
            case JointMotionMode::Limited:
                return physx::PxD6Motion::eLIMITED;
            case JointMotionMode::Locked:
            default:
                return physx::PxD6Motion::eLOCKED;
            }
        }

        void ApplyJointCommonSettings(physx::PxJoint& pxJoint, const JointComponent& joint)
        {
            pxJoint.setConstraintFlag(physx::PxConstraintFlag::eCOLLISION_ENABLED, joint.collideConnectedBodies);
            pxJoint.setBreakForce(
                joint.enableBreak ? std::max(0.0f, joint.breakForce) : PX_MAX_F32,
                joint.enableBreak ? std::max(0.0f, joint.breakTorque) : PX_MAX_F32);
        }

        void ApplyJointSolverIterations(
            physx::PxRigidActor* actorA,
            physx::PxRigidActor* actorB,
            const JointComponent& joint)
        {
            const auto applyToActor = [&](physx::PxRigidActor* actor)
            {
                if (auto* dynamicActor = actor != nullptr ? actor->is<physx::PxRigidDynamic>() : nullptr)
                {
                    dynamicActor->setSolverIterationCounts(
                        std::max<std::uint32_t>(1u, joint.solverPositionIterations),
                        std::max<std::uint32_t>(1u, joint.solverVelocityIterations));
                }
            };

            applyToActor(actorA);
            applyToActor(actorB);
        }
#endif
    }

    std::string_view PhysXBackend::GetName() const
    {
        return m_UsingNativePhysX ? "PhysX (Native)" : "PhysX (API Fallback)";
    }

    PhysicsBackendType PhysXBackend::GetType() const
    {
        return PhysicsBackendType::PhysX;
    }

    bool PhysXBackend::Initialize(const PhysicsSettings& settings)
    {
        m_Settings = settings;

#if defined(LUMA_ENABLE_PHYSX) && LUMA_ENABLE_PHYSX
        ShutdownNativePhysX();

        m_PhysXFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, m_PhysXAllocator, m_PhysXErrorCallback);
        if (m_PhysXFoundation == nullptr)
        {
            LUMA_LOG_ERROR("Physics", "Failed to create PhysX foundation.");
            return false;
        }

        const physx::PxTolerancesScale tolerances;
        m_PhysX = PxCreatePhysics(PX_PHYSICS_VERSION, *m_PhysXFoundation, tolerances, false, nullptr);
        if (m_PhysX == nullptr)
        {
            LUMA_LOG_ERROR("Physics", "Failed to create PhysX SDK.");
            ShutdownNativePhysX();
            return false;
        }

        m_PhysXCookingParams = physx::PxCookingParams(tolerances);
        m_PhysXCookingParams.meshPreprocessParams |= physx::PxMeshPreprocessingFlag::eWELD_VERTICES;
        m_PhysXCookingParams.meshWeldTolerance = 0.001f;

        m_PhysXDispatcher = physx::PxDefaultCpuDispatcherCreate(2);
        if (m_PhysXDispatcher == nullptr)
        {
            LUMA_LOG_ERROR("Physics", "Failed to create PhysX CPU dispatcher.");
            ShutdownNativePhysX();
            return false;
        }

        physx::PxSceneDesc sceneDesc(m_PhysX->getTolerancesScale());
        sceneDesc.gravity = ToPxVec3(m_Settings.gravity);
        sceneDesc.cpuDispatcher = m_PhysXDispatcher;
        sceneDesc.filterShader = physx::PxDefaultSimulationFilterShader;
        sceneDesc.flags |= physx::PxSceneFlag::eENABLE_CCD;
        sceneDesc.flags |= physx::PxSceneFlag::eENABLE_ACTIVE_ACTORS;

        m_PhysXScene = m_PhysX->createScene(sceneDesc);
        if (m_PhysXScene == nullptr)
        {
            LUMA_LOG_ERROR("Physics", "Failed to create PhysX scene.");
            ShutdownNativePhysX();
            return false;
        }

        m_ControllerManager = PxCreateControllerManager(*m_PhysXScene);
        if (m_ControllerManager == nullptr)
        {
            LUMA_LOG_ERROR("Physics", "Failed to create PhysX character controller manager.");
            ShutdownNativePhysX();
            return false;
        }

        m_DefaultMaterial = m_PhysX->createMaterial(0.6f, 0.6f, 0.0f);
        if (m_DefaultMaterial == nullptr)
        {
            LUMA_LOG_ERROR("Physics", "Failed to create PhysX default material.");
            ShutdownNativePhysX();
            return false;
        }

        if (!m_BlastDestructionService.Initialize(m_PhysX))
        {
#if defined(LUMA_ENABLE_BLAST) && LUMA_ENABLE_BLAST
            LUMA_LOG_WARN("Physics", "Blast destruction service is unavailable. Destructible runtime will stay inactive.");
#endif
        }

        m_UsingNativePhysX = true;
        LUMA_LOG_INFO("Physics", "Native PhysX backend initialized.");
#else
        m_UsingNativePhysX = false;
        LUMA_LOG_WARN("Physics", "PhysX native backend is disabled at build time. Using fallback solver through Physics API.");
#endif

        m_Initialized = true;
        return true;
    }

    void PhysXBackend::Shutdown()
    {
        m_Initialized = false;
        m_UsingNativePhysX = false;
        m_BlastDestructionService.Shutdown();

#if defined(LUMA_ENABLE_PHYSX) && LUMA_ENABLE_PHYSX
        ShutdownNativePhysX();
#endif
    }

    void PhysXBackend::Simulate(Scene& scene, const float fixedDeltaTimeSeconds)
    {
        if (!m_Initialized || fixedDeltaTimeSeconds <= 0.0f)
        {
            return;
        }

#if defined(LUMA_ENABLE_PHYSX) && LUMA_ENABLE_PHYSX
        if (m_UsingNativePhysX)
        {
            SimulateNative(scene, fixedDeltaTimeSeconds);
            return;
        }
#endif

        auto& registry = scene.GetRegistry();
        auto view = registry.view<TransformComponent, RigidBodyComponent, ColliderComponent>();
        for (const EntityID entity : view)
        {
            auto& transform = view.get<TransformComponent>(entity);
            auto& rigidBody = view.get<RigidBodyComponent>(entity);
            const auto& collider = view.get<ColliderComponent>(entity);

            if (!rigidBody.active || !collider.active)
            {
                continue;
            }

            if (rigidBody.bodyType == RigidBodyType::Static)
            {
                rigidBody.linearVelocity = { 0.0f, 0.0f, 0.0f };
                rigidBody.angularVelocity = { 0.0f, 0.0f, 0.0f };
                rigidBody.sleeping = true;
                continue;
            }

            if (rigidBody.bodyType == RigidBodyType::Dynamic)
            {
                if (rigidBody.enableGravity)
                {
                    rigidBody.linearVelocity[0] += m_Settings.gravity[0] * fixedDeltaTimeSeconds;
                    rigidBody.linearVelocity[1] += m_Settings.gravity[1] * fixedDeltaTimeSeconds;
                    rigidBody.linearVelocity[2] += m_Settings.gravity[2] * fixedDeltaTimeSeconds;
                }

                const float linearDamping = std::clamp(rigidBody.linearDamping, 0.0f, 100.0f);
                const float angularDamping = std::clamp(rigidBody.angularDamping, 0.0f, 100.0f);
                const float linearScale = std::max(0.0f, 1.0f - linearDamping * fixedDeltaTimeSeconds);
                const float angularScale = std::max(0.0f, 1.0f - angularDamping * fixedDeltaTimeSeconds);

                for (int axis = 0; axis < 3; ++axis)
                {
                    rigidBody.linearVelocity[axis] *= linearScale;
                    rigidBody.angularVelocity[axis] *= angularScale;
                }

                ClampVectorMagnitude(rigidBody.linearVelocity, std::max(0.0f, rigidBody.maxLinearVelocity));
                ClampVectorMagnitude(rigidBody.angularVelocity, std::max(0.0f, rigidBody.maxAngularVelocity));

                for (int axis = 0; axis < 3; ++axis)
                {
                    if (rigidBody.lockLinearAxes[axis])
                    {
                        rigidBody.linearVelocity[axis] = 0.0f;
                    }
                    if (rigidBody.lockAngularAxes[axis])
                    {
                        rigidBody.angularVelocity[axis] = 0.0f;
                    }
                }

                transform.position[0] += rigidBody.linearVelocity[0] * fixedDeltaTimeSeconds;
                transform.position[1] += rigidBody.linearVelocity[1] * fixedDeltaTimeSeconds;
                transform.position[2] += rigidBody.linearVelocity[2] * fixedDeltaTimeSeconds;

                transform.rotation[0] += rigidBody.angularVelocity[0] * fixedDeltaTimeSeconds;
                transform.rotation[1] += rigidBody.angularVelocity[1] * fixedDeltaTimeSeconds;
                transform.rotation[2] += rigidBody.angularVelocity[2] * fixedDeltaTimeSeconds;
                transform.dirty = true;
            }
            else
            {
                // Kinematic body: transform is authored externally.
                for (int axis = 0; axis < 3; ++axis)
                {
                    if (rigidBody.lockLinearAxes[axis])
                    {
                        rigidBody.linearVelocity[axis] = 0.0f;
                    }
                    if (rigidBody.lockAngularAxes[axis])
                    {
                        rigidBody.angularVelocity[axis] = 0.0f;
                    }
                }
            }

            const float linearSpeed = ComputeLength(rigidBody.linearVelocity);
            const float angularSpeed = ComputeLength(rigidBody.angularVelocity);
            const bool asleep = m_Settings.enableSleeping &&
                linearSpeed < 0.01f &&
                angularSpeed < 0.01f &&
                rigidBody.bodyType == RigidBodyType::Dynamic;
            rigidBody.sleeping = asleep;
        }

        std::vector<ColliderProxy> colliders;
        colliders.reserve(128);
        auto colliderView = registry.view<TransformComponent, ColliderComponent>();
        for (const EntityID entity : colliderView)
        {
            auto& transform = colliderView.get<TransformComponent>(entity);
            const auto& collider = colliderView.get<ColliderComponent>(entity);
            if (!collider.active)
            {
                continue;
            }

            RigidBodyComponent* rigidBody = registry.try_get<RigidBodyComponent>(entity);
            if (rigidBody != nullptr && !rigidBody->active)
            {
                continue;
            }
            const MeshRendererComponent* meshRenderer = registry.try_get<MeshRendererComponent>(entity);

            colliders.push_back(BuildColliderProxy(entity, transform, collider, rigidBody, meshRenderer));
        }

        for (std::size_t firstIndex = 0; firstIndex < colliders.size(); ++firstIndex)
        {
            ColliderProxy& first = colliders[firstIndex];
            for (std::size_t secondIndex = firstIndex + 1; secondIndex < colliders.size(); ++secondIndex)
            {
                ColliderProxy& second = colliders[secondIndex];

                if (!first.dynamicBody && !second.dynamicBody)
                {
                    continue;
                }
                if (first.trigger || second.trigger)
                {
                    continue;
                }

                std::array<float, 3> mtvForFirst {};
                if (!ComputeAabbMinimumTranslation(first, second, mtvForFirst))
                {
                    continue;
                }

                if (first.dynamicBody && second.dynamicBody)
                {
                    const std::array<float, 3> halfMtv {
                        mtvForFirst[0] * 0.5f,
                        mtvForFirst[1] * 0.5f,
                        mtvForFirst[2] * 0.5f
                    };
                    ApplyTranslation(first, halfMtv);
                    ApplyTranslation(second, Negate(halfMtv));
                    ZeroVelocityOnResolvedAxis(*first.rigidBody, halfMtv);
                    ZeroVelocityOnResolvedAxis(*second.rigidBody, halfMtv);
                    continue;
                }

                if (first.dynamicBody)
                {
                    ApplyTranslation(first, mtvForFirst);
                    ZeroVelocityOnResolvedAxis(*first.rigidBody, mtvForFirst);
                    continue;
                }

                if (second.dynamicBody)
                {
                    const std::array<float, 3> mtvForSecond = Negate(mtvForFirst);
                    ApplyTranslation(second, mtvForSecond);
                    ZeroVelocityOnResolvedAxis(*second.rigidBody, mtvForSecond);
                }
            }
        }
    }

#if defined(LUMA_ENABLE_PHYSX) && LUMA_ENABLE_PHYSX
    void PhysXBackend::ShutdownNativePhysX()
    {
        DestroyNativeJoints();
        DestroyNativeControllers();
        m_NativeVehicles.clear();
        m_VehicleTuningAssetCacheService.Clear();

        for (auto& [_, actorState] : m_NativeActors)
        {
            DestroyNativeActor(actorState);
        }
        m_NativeActors.clear();

        for (auto& [_, meshState] : m_CookedMeshes)
        {
            if (meshState.convexMesh != nullptr)
            {
                meshState.convexMesh->release();
            }
            if (meshState.triangleMesh != nullptr)
            {
                meshState.triangleMesh->release();
            }
        }
        m_CookedMeshes.clear();

        if (m_DefaultMaterial != nullptr)
        {
            m_DefaultMaterial->release();
            m_DefaultMaterial = nullptr;
        }
        if (m_ControllerManager != nullptr)
        {
            m_ControllerManager->release();
            m_ControllerManager = nullptr;
        }
        if (m_PhysXScene != nullptr)
        {
            m_PhysXScene->release();
            m_PhysXScene = nullptr;
        }
        if (m_PhysXDispatcher != nullptr)
        {
            m_PhysXDispatcher->release();
            m_PhysXDispatcher = nullptr;
        }
        if (m_PhysX != nullptr)
        {
            m_PhysX->release();
            m_PhysX = nullptr;
        }
        if (m_PhysXFoundation != nullptr)
        {
            m_PhysXFoundation->release();
            m_PhysXFoundation = nullptr;
        }
    }

    void PhysXBackend::DestroyNativeJoints()
    {
        for (auto& [_, joint] : m_NativeJoints)
        {
            if (joint != nullptr)
            {
                joint->release();
            }
        }
        m_NativeJoints.clear();
    }

    void PhysXBackend::DestroyNativeControllers()
    {
        for (auto& [_, controllerState] : m_NativeControllers)
        {
            DestroyNativeController(controllerState);
        }
        m_NativeControllers.clear();
    }

    void PhysXBackend::DestroyNativeActor(NativeActorState& actorState)
    {
        if (actorState.actor != nullptr)
        {
            if (m_PhysXScene != nullptr && actorState.actor->getScene() == m_PhysXScene)
            {
                m_PhysXScene->removeActor(*actorState.actor, false);
            }
            actorState.actor->release();
        }

        actorState = {};
    }

    void PhysXBackend::DestroyNativeController(NativeControllerState& controllerState)
    {
        if (controllerState.controller != nullptr)
        {
            controllerState.controller->release();
        }

        controllerState = {};
    }

    bool PhysXBackend::NeedsActorRebuild(
        const NativeActorState& actorState,
        const TransformComponent& transform,
        const ColliderComponent& collider,
        RigidBodyComponent* rigidBody,
        const MeshRendererComponent* meshRenderer) const
    {
        const RigidBodyType bodyType = rigidBody != nullptr ? rigidBody->bodyType : RigidBodyType::Static;
        const std::array<float, 3> absScale = AbsoluteScale(transform);
        if (actorState.bodyType != bodyType ||
            actorState.shape != collider.shape ||
            actorState.trigger != collider.isTrigger ||
            !NearlyEqualArray(actorState.scale, absScale) ||
            !NearlyEqualArray(actorState.center, collider.center) ||
            !EqualMaterialDesc(actorState.materialDesc, collider.material))
        {
            return true;
        }

        switch (collider.shape)
        {
        case ColliderShapeType::Sphere:
            return !NearlyEqual(actorState.sphereRadius, collider.sphereRadius);
        case ColliderShapeType::Capsule:
        case ColliderShapeType::Cylinder:
            return !NearlyEqual(actorState.capsuleRadius, collider.capsuleRadius) ||
                !NearlyEqual(actorState.capsuleHalfHeight, collider.capsuleHalfHeight);
        case ColliderShapeType::Mesh:
        {
            const bool usePrimitiveMesh = meshRenderer != nullptr && meshRenderer->usePrimitive;
            const PrimitiveType primitive = usePrimitiveMesh ? meshRenderer->primitive : PrimitiveType::Cube;
            return actorState.meshConvex != collider.meshConvex ||
                actorState.usePrimitiveMesh != usePrimitiveMesh ||
                actorState.primitive != primitive ||
                actorState.meshSource != collider.meshSource;
        }
        case ColliderShapeType::Box:
        default:
            return !NearlyEqualArray(actorState.boxHalfExtents, collider.boxHalfExtents);
        }
    }

    bool PhysXBackend::CreateNativeActor(
        const NativeActorKey entityKey,
        const TransformComponent& transform,
        const ColliderComponent& collider,
        RigidBodyComponent* rigidBody,
        const MeshRendererComponent* meshRenderer,
        NativeActorState& outActorState)
    {
        const RigidBodyType bodyType = rigidBody != nullptr ? rigidBody->bodyType : RigidBodyType::Static;
        const physx::PxTransform actorPose(
            ToPxVec3(transform.position),
            ToPxQuatDegrees(transform.rotation));

        physx::PxRigidActor* actor = nullptr;
        if (bodyType == RigidBodyType::Static)
        {
            actor = m_PhysX->createRigidStatic(actorPose);
        }
        else
        {
            auto* dynamicActor = m_PhysX->createRigidDynamic(actorPose);
            if (dynamicActor == nullptr)
            {
                return false;
            }

            if (bodyType == RigidBodyType::Kinematic)
            {
                dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eKINEMATIC, true);
            }

            if (rigidBody != nullptr)
            {
                dynamicActor->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !rigidBody->enableGravity);
                dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, rigidBody->enableCCD || m_Settings.enableCCD);
                dynamicActor->setLinearDamping(std::max(0.0f, rigidBody->linearDamping));
                dynamicActor->setAngularDamping(std::max(0.0f, rigidBody->angularDamping));
                dynamicActor->setMaxLinearVelocity(std::max(0.0f, rigidBody->maxLinearVelocity));
                dynamicActor->setMaxAngularVelocity(DegreesToRadians(std::max(0.0f, rigidBody->maxAngularVelocity)));
                dynamicActor->setRigidDynamicLockFlags(BuildLockFlags(*rigidBody));
                dynamicActor->setLinearVelocity(ToPxVec3(rigidBody->linearVelocity), true);
                dynamicActor->setAngularVelocity(
                    physx::PxVec3(
                        DegreesToRadians(rigidBody->angularVelocity[0]),
                        DegreesToRadians(rigidBody->angularVelocity[1]),
                        DegreesToRadians(rigidBody->angularVelocity[2])),
                    true);
                if (bodyType == RigidBodyType::Dynamic)
                {
                    physx::PxRigidBodyExt::setMassAndUpdateInertia(
                        *dynamicActor,
                        std::max(0.001f, rigidBody->mass));
                }
                if (!rigidBody->startAwake)
                {
                    dynamicActor->putToSleep();
                }
            }

            actor = dynamicActor;
        }

        if (actor == nullptr)
        {
            return false;
        }

        physx::PxMaterial* material = m_PhysX->createMaterial(
            std::max(0.0f, collider.material.staticFriction),
            std::max(0.0f, collider.material.dynamicFriction),
            std::clamp(collider.material.restitution, 0.0f, 1.0f));
        if (material == nullptr)
        {
            actor->release();
            return false;
        }

        const std::array<float, 3> absScale = AbsoluteScale(transform);
        const physx::PxTransform localShapePose(
            physx::PxVec3(
                collider.center[0] * absScale[0],
                collider.center[1] * absScale[1],
                collider.center[2] * absScale[2]),
            physx::PxIdentity);

        physx::PxShape* shape = nullptr;
        switch (collider.shape)
        {
        case ColliderShapeType::Sphere:
        {
            const float radius =
                std::max(0.001f, collider.sphereRadius) *
                std::max(absScale[0], std::max(absScale[1], absScale[2]));
            shape = m_PhysX->createShape(physx::PxSphereGeometry(radius), *material, true);
            break;
        }
        case ColliderShapeType::Capsule:
        {
            const float radius = std::max(0.001f, collider.capsuleRadius) * std::max(absScale[0], absScale[2]);
            const float halfHeight = std::max(0.001f, collider.capsuleHalfHeight) * absScale[1];
            shape = m_PhysX->createShape(physx::PxCapsuleGeometry(radius, halfHeight), *material, true);
            break;
        }
        case ColliderShapeType::Cylinder:
        {
            const float radius = std::max(0.001f, collider.capsuleRadius) * std::max(absScale[0], absScale[2]);
            const float halfHeight = std::max(0.001f, collider.capsuleHalfHeight) * absScale[1];
            shape = m_PhysX->createShape(physx::PxCapsuleGeometry(radius, halfHeight), *material, true);
            break;
        }
        case ColliderShapeType::Mesh:
        {
            PrimitiveMeshData collisionMesh;
            std::string meshKey;
            if (LoadCollisionMesh(collider, meshRenderer, collisionMesh, meshKey))
            {
                NativeCookedMeshState& meshState =
                    m_CookedMeshes[std::string(collider.meshConvex ? "convex:" : "tri:") + meshKey];
                if (collider.meshConvex)
                {
                    if (meshState.convexMesh == nullptr)
                    {
                        std::vector<physx::PxVec3> points;
                        points.reserve(collisionMesh.vertices.size());
                        for (const PrimitiveVertex& vertex : collisionMesh.vertices)
                        {
                            points.emplace_back(vertex.position[0], vertex.position[1], vertex.position[2]);
                        }

                        physx::PxConvexMeshDesc convexDesc;
                        convexDesc.points.count = static_cast<physx::PxU32>(points.size());
                        convexDesc.points.stride = sizeof(physx::PxVec3);
                        convexDesc.points.data = points.data();
                        convexDesc.flags = physx::PxConvexFlag::eCOMPUTE_CONVEX;

                        meshState.convexMesh = PxCreateConvexMesh(
                            m_PhysXCookingParams,
                            convexDesc,
                            m_PhysX->getPhysicsInsertionCallback());
                    }

                    if (meshState.convexMesh != nullptr)
                    {
                        shape = m_PhysX->createShape(
                            physx::PxConvexMeshGeometry(
                                meshState.convexMesh,
                                physx::PxMeshScale(
                                    physx::PxVec3(absScale[0], absScale[1], absScale[2]),
                                    physx::PxQuat(physx::PxIdentity))),
                            *material,
                            true);
                    }
                }
                else
                {
                    if (meshState.triangleMesh == nullptr)
                    {
                        std::vector<physx::PxVec3> points;
                        points.reserve(collisionMesh.vertices.size());
                        for (const PrimitiveVertex& vertex : collisionMesh.vertices)
                        {
                            points.emplace_back(vertex.position[0], vertex.position[1], vertex.position[2]);
                        }

                        physx::PxTriangleMeshDesc triangleDesc;
                        triangleDesc.points.count = static_cast<physx::PxU32>(points.size());
                        triangleDesc.points.stride = sizeof(physx::PxVec3);
                        triangleDesc.points.data = points.data();
                        triangleDesc.triangles.count = static_cast<physx::PxU32>(collisionMesh.indices.size() / 3u);
                        triangleDesc.triangles.stride = sizeof(std::uint32_t) * 3u;
                        triangleDesc.triangles.data = collisionMesh.indices.data();

                        meshState.triangleMesh = PxCreateTriangleMesh(
                            m_PhysXCookingParams,
                            triangleDesc,
                            m_PhysX->getPhysicsInsertionCallback());
                    }

                    if (meshState.triangleMesh != nullptr)
                    {
                        shape = m_PhysX->createShape(
                            physx::PxTriangleMeshGeometry(
                                meshState.triangleMesh,
                                physx::PxMeshScale(
                                    physx::PxVec3(absScale[0], absScale[1], absScale[2]),
                                    physx::PxQuat(physx::PxIdentity))),
                            *material,
                            true);
                    }
                }
            }

            if (shape == nullptr)
            {
                shape = m_PhysX->createShape(
                    physx::PxBoxGeometry(
                        std::max(0.001f, collider.boxHalfExtents[0] * absScale[0]),
                        std::max(0.001f, collider.boxHalfExtents[1] * absScale[1]),
                        std::max(0.001f, collider.boxHalfExtents[2] * absScale[2])),
                    *material,
                    true);
            }
            break;
        }
        case ColliderShapeType::Box:
        default:
            shape = m_PhysX->createShape(
                physx::PxBoxGeometry(
                    std::max(0.001f, collider.boxHalfExtents[0] * absScale[0]),
                    std::max(0.001f, collider.boxHalfExtents[1] * absScale[1]),
                    std::max(0.001f, collider.boxHalfExtents[2] * absScale[2])),
                *material,
                true);
            break;
        }

        if (shape == nullptr)
        {
            material->release();
            actor->release();
            return false;
        }

        shape->setLocalPose(localShapePose);
        if (collider.isTrigger)
        {
            shape->setFlag(physx::PxShapeFlag::eSIMULATION_SHAPE, false);
            shape->setFlag(physx::PxShapeFlag::eTRIGGER_SHAPE, true);
        }

        actor->attachShape(*shape);
        m_PhysXScene->addActor(*actor);

        outActorState = {};
        outActorState.actor = actor;
        outActorState.shapeHandle = shape;
        outActorState.materialHandle = material;
        outActorState.shape = collider.shape;
        outActorState.bodyType = bodyType;
        outActorState.trigger = collider.isTrigger;
        outActorState.meshConvex = collider.meshConvex;
        outActorState.usePrimitiveMesh = meshRenderer != nullptr && meshRenderer->usePrimitive;
        outActorState.primitive = outActorState.usePrimitiveMesh ? meshRenderer->primitive : PrimitiveType::Cube;
        outActorState.meshSource = collider.meshSource;
        outActorState.scale = absScale;
        outActorState.center = collider.center;
        outActorState.boxHalfExtents = collider.boxHalfExtents;
        outActorState.sphereRadius = collider.sphereRadius;
        outActorState.capsuleRadius = collider.capsuleRadius;
        outActorState.capsuleHalfHeight = collider.capsuleHalfHeight;
        outActorState.materialDesc = collider.material;
        outActorState.lastPosition = transform.position;
        outActorState.lastRotation = transform.rotation;

        shape->release();
        material->release();
        return true;
    }

    bool PhysXBackend::CreateNativeController(
        const NativeActorKey entityKey,
        const TransformComponent& transform,
        const CharacterControllerComponent& controller,
        NativeControllerState& outControllerState)
    {
        if (m_ControllerManager == nullptr || m_DefaultMaterial == nullptr)
        {
            return false;
        }

        const float radius = BuildControllerRadius(transform, controller);
        const float totalHeight = BuildControllerTotalHeight(transform, controller);

        physx::PxCapsuleControllerDesc desc;
        desc.material = m_DefaultMaterial;
        desc.position = ToPxExtendedVec3(transform.position);
        desc.radius = radius;
        desc.height = BuildControllerCapsuleHeight(radius, totalHeight);
        desc.stepOffset = std::min(std::max(0.0f, controller.stepOffset), totalHeight);
        desc.contactOffset = std::max(0.001f, controller.skinWidth);
        desc.slopeLimit = std::cos(DegreesToRadians(std::clamp(controller.slopeLimitDegrees, 0.0f, 89.0f)));
        desc.nonWalkableMode = physx::PxControllerNonWalkableMode::ePREVENT_CLIMBING_AND_FORCE_SLIDING;
        desc.climbingMode = physx::PxCapsuleClimbingMode::eCONSTRAINED;
        desc.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(entityKey));

        if (!desc.isValid())
        {
            return false;
        }

        physx::PxController* pxController = m_ControllerManager->createController(desc);
        if (pxController == nullptr)
        {
            return false;
        }

        if (physx::PxRigidDynamic* actor = pxController->getActor())
        {
            actor->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, true);
            actor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, m_Settings.enableCCD);

            physx::PxShape* shapes[8] {};
            const physx::PxU32 shapeCount = actor->getShapes(shapes, 8);
            const physx::PxFilterData filterData(controller.collisionLayer, controller.collisionMask, 0, 0);
            for (physx::PxU32 shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
            {
                shapes[shapeIndex]->setSimulationFilterData(filterData);
                shapes[shapeIndex]->setQueryFilterData(filterData);
            }
        }

        outControllerState = {};
        outControllerState.controller = pxController;
        outControllerState.radius = controller.radius;
        outControllerState.totalHeight = controller.height;
        outControllerState.stepOffset = controller.stepOffset;
        outControllerState.slopeLimitDegrees = controller.slopeLimitDegrees;
        outControllerState.skinWidth = controller.skinWidth;
        outControllerState.minMoveDistance = controller.minMoveDistance;
        outControllerState.gravityScale = controller.gravityScale;
        outControllerState.collisionLayer = controller.collisionLayer;
        outControllerState.collisionMask = controller.collisionMask;
        outControllerState.movementMode = ToMovementModeValue(controller.movementMode);
        outControllerState.lastPosition = transform.position;
        return true;
    }

    bool PhysXBackend::SyncNativeController(
        const NativeActorKey entityKey,
        TransformComponent& transform,
        CharacterControllerComponent& controller,
        RigidBodyComponent* rigidBody,
        const float fixedDeltaTimeSeconds)
    {
        auto existing = m_NativeControllers.find(entityKey);
        if (existing == m_NativeControllers.end())
        {
            NativeControllerState controllerState {};
            if (!CreateNativeController(entityKey, transform, controller, controllerState))
            {
                return false;
            }

            existing = m_NativeControllers.emplace(entityKey, std::move(controllerState)).first;
        }

        NativeControllerState& controllerState = existing->second;
        if (controllerState.controller == nullptr)
        {
            return false;
        }

        const float radius = BuildControllerRadius(transform, controller);
        const float totalHeight = BuildControllerTotalHeight(transform, controller);
        const float capsuleHeight = BuildControllerCapsuleHeight(radius, totalHeight);
        const float stepOffset = std::min(std::max(0.0f, controller.stepOffset), totalHeight);
        const float skinWidth = std::max(0.001f, controller.skinWidth);
        const float slopeLimitDegrees = std::clamp(controller.slopeLimitDegrees, 0.0f, 89.0f);
        const bool poseEdited = !NearlyEqualArray(controllerState.lastPosition, transform.position);

        if (controllerState.controller->getType() == physx::PxControllerShapeType::eCAPSULE)
        {
            auto* capsuleController = static_cast<physx::PxCapsuleController*>(controllerState.controller);
            if (!NearlyEqual(controllerState.radius, controller.radius) || !NearlyEqual(capsuleController->getRadius(), radius))
            {
                capsuleController->setRadius(radius);
            }
            if (!NearlyEqual(controllerState.totalHeight, controller.height) || !NearlyEqual(capsuleController->getHeight(), capsuleHeight))
            {
                capsuleController->setHeight(capsuleHeight);
            }
        }

        controllerState.controller->setStepOffset(stepOffset);
        controllerState.controller->setContactOffset(skinWidth);
        controllerState.controller->setSlopeLimit(std::cos(DegreesToRadians(slopeLimitDegrees)));
        controllerState.controller->setNonWalkableMode(physx::PxControllerNonWalkableMode::ePREVENT_CLIMBING_AND_FORCE_SLIDING);

        if (poseEdited)
        {
            controllerState.controller->setPosition(ToPxExtendedVec3(transform.position));
            controllerState.lastPosition = transform.position;
        }

        if (physx::PxRigidDynamic* actor = controllerState.controller->getActor())
        {
            actor->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, true);

            physx::PxShape* shapes[8] {};
            const physx::PxU32 shapeCount = actor->getShapes(shapes, 8);
            const physx::PxFilterData filterData(controller.collisionLayer, controller.collisionMask, 0, 0);
            for (physx::PxU32 shapeIndex = 0; shapeIndex < shapeCount; ++shapeIndex)
            {
                shapes[shapeIndex]->setSimulationFilterData(filterData);
                shapes[shapeIndex]->setQueryFilterData(filterData);
            }
        }

        controllerState.radius = controller.radius;
        controllerState.totalHeight = controller.height;
        controllerState.stepOffset = controller.stepOffset;
        controllerState.slopeLimitDegrees = controller.slopeLimitDegrees;
        controllerState.skinWidth = controller.skinWidth;
        controllerState.minMoveDistance = controller.minMoveDistance;
        controllerState.gravityScale = controller.gravityScale;
        controllerState.collisionLayer = controller.collisionLayer;
        controllerState.collisionMask = controller.collisionMask;
        controllerState.movementMode = ToMovementModeValue(controller.movementMode);

        if (fixedDeltaTimeSeconds <= 0.0f)
        {
            return true;
        }

        std::array<float, 3> velocity = controllerState.velocity;
        if (rigidBody != nullptr && rigidBody->active)
        {
            velocity = rigidBody->linearVelocity;
        }

        const float gravityMultiplier =
            CharacterGravityMultiplier(controller.movementMode) * std::max(0.0f, controller.gravityScale);
        if (gravityMultiplier > 0.0f)
        {
            velocity[0] += m_Settings.gravity[0] * fixedDeltaTimeSeconds * gravityMultiplier;
            velocity[1] += m_Settings.gravity[1] * fixedDeltaTimeSeconds * gravityMultiplier;
            velocity[2] += m_Settings.gravity[2] * fixedDeltaTimeSeconds * gravityMultiplier;
        }

        const physx::PxControllerFilters filters;
        const physx::PxControllerCollisionFlags collisionFlags = controllerState.controller->move(
            ToPxVec3({
                velocity[0] * fixedDeltaTimeSeconds,
                velocity[1] * fixedDeltaTimeSeconds,
                velocity[2] * fixedDeltaTimeSeconds
            }),
            std::max(0.0f, controller.minMoveDistance),
            fixedDeltaTimeSeconds,
            filters);

        const bool hitGround = collisionFlags.isSet(physx::PxControllerCollisionFlag::eCOLLISION_DOWN);
        const bool hitCeiling = collisionFlags.isSet(physx::PxControllerCollisionFlag::eCOLLISION_UP);

        controller.isGrounded = hitGround && controller.movementMode != CharacterMovementMode::Fly;

        if (hitGround && velocity[1] < 0.0f)
        {
            velocity[1] = 0.0f;
        }
        if (hitCeiling && velocity[1] > 0.0f)
        {
            velocity[1] = 0.0f;
        }
        if (controller.movementMode == CharacterMovementMode::Fly)
        {
            controller.isGrounded = false;
        }

        transform.position = FromPxExtendedVec3(controllerState.controller->getPosition());
        transform.dirty = true;
        controllerState.lastPosition = transform.position;
        controllerState.velocity = velocity;

        if (rigidBody != nullptr && rigidBody->active)
        {
            rigidBody->linearVelocity = velocity;
            rigidBody->angularVelocity = { 0.0f, 0.0f, 0.0f };
            rigidBody->sleeping = controller.isGrounded && NearlyZeroVector(velocity);
        }

        return true;
    }

    void PhysXBackend::SimulateNativeVehicles(Scene& scene, const float fixedDeltaTimeSeconds)
    {
        if (m_PhysXScene == nullptr || fixedDeltaTimeSeconds <= 0.0f)
        {
            return;
        }

        auto& registry = scene.GetRegistry();
        auto vehicleView = registry.view<TransformComponent, VehicleComponent>();
        std::unordered_set<NativeActorKey> activeVehicles;
        activeVehicles.reserve(m_NativeVehicles.size() + 8u);

        for (const EntityID entity : vehicleView)
        {
            auto& vehicleTransform = vehicleView.get<TransformComponent>(entity);
            auto& vehicle = vehicleView.get<VehicleComponent>(entity);
            VehicleComponent simVehicle = vehicle;
            m_VehicleTuningAssetCacheService.TryApply(vehicle.tuningAsset, simVehicle);
            const NativeActorKey vehicleKey = static_cast<NativeActorKey>(entity);
            if (!vehicle.active || !simVehicle.simulationEnabled || vehicle.chassisRigidBody == 0)
            {
                m_NativeVehicles.erase(vehicleKey);
                continue;
            }

            const EntityID chassisEntity = scene.FindByUUID(vehicle.chassisRigidBody);
            if (chassisEntity == entt::null || !registry.valid(chassisEntity))
            {
                m_NativeVehicles.erase(vehicleKey);
                continue;
            }

            const auto actorIt = m_NativeActors.find(static_cast<NativeActorKey>(chassisEntity));
            if (actorIt == m_NativeActors.end())
            {
                m_NativeVehicles.erase(vehicleKey);
                continue;
            }

            physx::PxRigidDynamic* chassisActor = actorIt->second.actor != nullptr
                ? actorIt->second.actor->is<physx::PxRigidDynamic>()
                : nullptr;
            TransformComponent* chassisTransform = registry.try_get<TransformComponent>(chassisEntity);
            RigidBodyComponent* chassisBody = registry.try_get<RigidBodyComponent>(chassisEntity);
            if (chassisActor == nullptr || chassisTransform == nullptr || chassisBody == nullptr)
            {
                m_NativeVehicles.erase(vehicleKey);
                continue;
            }

            activeVehicles.insert(vehicleKey);
            NativeVehicleState& vehicleState = m_NativeVehicles[vehicleKey];
            if (vehicleState.chassisId != vehicle.chassisRigidBody || vehicleState.wheels.size() != vehicle.wheelEntities.size())
            {
                vehicleState = {};
                vehicleState.chassisId = vehicle.chassisRigidBody;
                vehicleState.currentGear = 1;
                vehicleState.reverseGear = false;
                vehicleState.engineRPM = std::max(100.0f, vehicle.idleRPM);
            }

            const physx::PxTransform chassisPose = chassisActor->getGlobalPose();
            const std::array<float, 3> chassisPosition = FromPxVec3(chassisPose.p);
            const std::array<float, 3> chassisForward = NormalizeArrayOrFallback(
                FromPxVec3(chassisPose.q.rotate(physx::PxVec3(0.0f, 0.0f, 1.0f))),
                { 0.0f, 0.0f, 1.0f });
            const std::array<float, 3> chassisRight = NormalizeArrayOrFallback(
                FromPxVec3(chassisPose.q.rotate(physx::PxVec3(1.0f, 0.0f, 0.0f))),
                { 1.0f, 0.0f, 0.0f });
            const std::array<float, 3> chassisUp = NormalizeArrayOrFallback(
                FromPxVec3(chassisPose.q.rotate(physx::PxVec3(0.0f, 1.0f, 0.0f))),
                { 0.0f, 1.0f, 0.0f });

            float throttleInput = 0.0f;
            float steerInput = 0.0f;
            float brakeInput = 0.0f;
            float handbrakeInput = 0.0f;
            float clutchInput = 0.0f;
            bool gearUpRequested = false;
            bool gearDownRequested = false;
            bool resetRequested = false;
            if (auto* vehicleInput = registry.try_get<VehicleInputComponent>(entity);
                vehicleInput != nullptr && vehicleInput->active)
            {
                throttleInput = vehicleInput->throttle;
                steerInput = vehicleInput->steering;
                brakeInput = vehicleInput->brake;
                handbrakeInput = vehicleInput->handbrake;
                clutchInput = vehicleInput->clutch;
                gearUpRequested = vehicleInput->gearUpRequested;
                gearDownRequested = vehicleInput->gearDownRequested;
                resetRequested = vehicleInput->resetRequested;
                vehicleInput->gearUpRequested = false;
                vehicleInput->gearDownRequested = false;
                vehicleInput->resetRequested = false;
            }

            throttleInput = std::clamp(throttleInput, -1.0f, 1.0f);
            steerInput = std::clamp(steerInput, -1.0f, 1.0f);
            brakeInput = std::clamp(brakeInput, 0.0f, 1.0f);
            handbrakeInput = std::clamp(handbrakeInput, 0.0f, 1.0f);
            clutchInput = std::clamp(clutchInput, 0.0f, 1.0f);

            if (resetRequested)
            {
                physx::PxTransform resetPose = chassisActor->getGlobalPose();
                resetPose.q = physx::PxIdentity;
                resetPose.p.y += 1.0f;
                chassisActor->setGlobalPose(resetPose, true);
                chassisActor->setLinearVelocity(physx::PxVec3(0.0f, 0.0f, 0.0f), true);
                chassisActor->setAngularVelocity(physx::PxVec3(0.0f, 0.0f, 0.0f), true);
            }

            if (simVehicle.useCenterOfMassOverride)
            {
                chassisActor->setCMassLocalPose(physx::PxTransform(ToPxVec3(simVehicle.centerOfMassOffset), physx::PxIdentity));
            }

            const float steerTarget = std::clamp(steerInput * simVehicle.maxSteerAngleDegrees, -simVehicle.maxSteerAngleDegrees, simVehicle.maxSteerAngleDegrees);

            const std::array<float, 3> chassisLinearVelocity = FromPxVec3(chassisActor->getLinearVelocity());
            const float forwardSpeed = DotProduct(chassisLinearVelocity, chassisForward);
            const float speedMagnitude = ComputeLength(chassisLinearVelocity);
            const std::array<float, 3> dragForce = Multiply(chassisLinearVelocity, -std::max(0.0f, simVehicle.dragCoefficient) * speedMagnitude);
            const std::array<float, 3> rollingForce = Multiply(
                NormalizeArrayOrFallback(chassisLinearVelocity, { 0.0f, 0.0f, 0.0f }),
                -std::max(0.0f, simVehicle.rollingResistance) * speedMagnitude);
            const std::array<float, 3> downforce = Multiply(chassisUp, -std::max(0.0f, simVehicle.aeroDownforce) * speedMagnitude * speedMagnitude);
            if (!NearlyZeroVector(dragForce))
            {
                chassisActor->addForce(ToPxVec3(dragForce), physx::PxForceMode::eFORCE, true);
            }
            if (!NearlyZeroVector(rollingForce))
            {
                chassisActor->addForce(ToPxVec3(rollingForce), physx::PxForceMode::eFORCE, true);
            }
            if (!NearlyZeroVector(downforce))
            {
                chassisActor->addForce(ToPxVec3(downforce), physx::PxForceMode::eFORCE, true);
            }

            std::size_t drivenWheelCount = 0;
            std::size_t frontDrivenWheelCount = 0;
            std::size_t rearDrivenWheelCount = 0;
            for (const UUID wheelId : vehicle.wheelEntities)
            {
                const EntityID wheelEntity = scene.FindByUUID(wheelId);
                if (wheelEntity == entt::null || !registry.valid(wheelEntity) || !registry.all_of<WheelColliderComponent>(wheelEntity))
                {
                    continue;
                }
                const auto& wheel = registry.get<WheelColliderComponent>(wheelEntity);
                if (wheel.active && wheel.driven)
                {
                    ++drivenWheelCount;
                    if (wheel.axleType == VehicleAxleType::Front)
                    {
                        ++frontDrivenWheelCount;
                    }
                    else if (wheel.axleType == VehicleAxleType::Rear)
                    {
                        ++rearDrivenWheelCount;
                    }
                }
            }
            drivenWheelCount = std::max<std::size_t>(1u, drivenWheelCount);

            const float idleRPM = std::max(100.0f, simVehicle.idleRPM);
            const float maxRPM = std::max(idleRPM + 100.0f, simVehicle.maxRPM);
            if (simVehicle.gearRatios.empty())
            {
                simVehicle.gearRatios.push_back(3.5f);
            }
            vehicleState.currentGear = std::clamp(vehicleState.currentGear, 1, static_cast<int>(simVehicle.gearRatios.size()));
            const float absForwardSpeed = std::abs(forwardSpeed);
            const bool nearStationary = absForwardSpeed < 1.0f;

            if (simVehicle.automaticTransmission)
            {
                const bool requestReverse = throttleInput < -0.1f && nearStationary;
                const bool requestDrive = throttleInput > 0.1f && nearStationary;
                if (requestReverse)
                {
                    vehicleState.reverseGear = true;
                }
                else if (requestDrive)
                {
                    vehicleState.reverseGear = false;
                }
            }
            else
            {
                if (gearUpRequested)
                {
                    if (vehicleState.reverseGear)
                    {
                        if (nearStationary)
                        {
                            vehicleState.reverseGear = false;
                            vehicleState.currentGear = 1;
                        }
                    }
                    else if (vehicleState.currentGear < static_cast<int>(simVehicle.gearRatios.size()))
                    {
                        ++vehicleState.currentGear;
                    }
                }

                if (gearDownRequested)
                {
                    if (!vehicleState.reverseGear)
                    {
                        if (vehicleState.currentGear > 1)
                        {
                            --vehicleState.currentGear;
                        }
                        else if (nearStationary && (brakeInput > 0.25f || throttleInput < -0.1f || clutchInput > 0.25f))
                        {
                            vehicleState.reverseGear = true;
                        }
                    }
                }
            }

            const float signedWheelRPM = (forwardSpeed / (2.0f * kPi * 0.35f)) * 60.0f;
            const float gearRatio = vehicleState.reverseGear
                ? std::max(0.01f, simVehicle.reverseGearRatio)
                : std::max(0.01f, simVehicle.gearRatios[static_cast<std::size_t>(vehicleState.currentGear - 1)]);
            const float differentialRatio = std::max(0.01f, simVehicle.differentialRatio);
            const float clutchEngagement = simVehicle.automaticTransmission ? 1.0f : (1.0f - clutchInput);
            const float coupledEngineRPM = std::abs(signedWheelRPM) * gearRatio * differentialRatio;
            const float freeRevEngineRPM = idleRPM + std::max(0.0f, std::abs(throttleInput)) * (maxRPM - idleRPM) * 0.65f;
            float estimatedEngineRPM = freeRevEngineRPM * (1.0f - clutchEngagement) + coupledEngineRPM * clutchEngagement;
            estimatedEngineRPM = std::max(idleRPM, estimatedEngineRPM);
            vehicleState.engineRPM += (estimatedEngineRPM - vehicleState.engineRPM) * std::clamp(fixedDeltaTimeSeconds * 8.0f, 0.0f, 1.0f);
            vehicleState.engineRPM = std::clamp(vehicleState.engineRPM, idleRPM, maxRPM);

            if (!vehicleState.reverseGear && simVehicle.automaticTransmission && simVehicle.gearRatios.size() > 1)
            {
                if (vehicleState.engineRPM > std::clamp(simVehicle.shiftUpRPM, idleRPM, maxRPM) &&
                    vehicleState.currentGear < static_cast<int>(simVehicle.gearRatios.size()))
                {
                    ++vehicleState.currentGear;
                }
                else if (vehicleState.engineRPM < std::clamp(simVehicle.shiftDownRPM, idleRPM, maxRPM) &&
                    vehicleState.currentGear > 1)
                {
                    --vehicleState.currentGear;
                }
            }

            const float rpmNormalized = std::clamp(vehicleState.engineRPM / maxRPM, 0.0f, 1.0f);
            const float torqueCurveFactor = std::clamp(1.15f - rpmNormalized * 0.75f, 0.35f, 1.15f);
            const float throttleDirection = vehicleState.reverseGear ? -1.0f : 1.0f;
            const float throttleMagnitude = vehicleState.reverseGear ? std::max(0.0f, -throttleInput) : std::max(0.0f, throttleInput);
            const float throttleTorque =
                throttleDirection * throttleMagnitude * clutchEngagement * simVehicle.engineTorque * torqueCurveFactor * gearRatio * differentialRatio;
            const float brakeStrength = brakeInput * std::max(0.0f, simVehicle.brakeForce);
            const float handbrakeStrength = handbrakeInput * std::max(0.0f, simVehicle.handbrakeForce);
            const float frontBrakeStrength = brakeStrength * std::clamp(simVehicle.frontBrakeBias, 0.0f, 1.0f);
            const float rearBrakeStrength = brakeStrength * (1.0f - std::clamp(simVehicle.frontBrakeBias, 0.0f, 1.0f));
            const float frontDriveBias = std::clamp(simVehicle.frontDriveBias, 0.0f, 1.0f);
            const bool hasFrontDrive = frontDrivenWheelCount > 0;
            const bool hasRearDrive = rearDrivenWheelCount > 0;
            const float frontDriveTorque = hasFrontDrive
                ? throttleTorque * (hasRearDrive ? frontDriveBias : 1.0f)
                : 0.0f;
            const float rearDriveTorque = hasRearDrive
                ? throttleTorque * (hasFrontDrive ? (1.0f - frontDriveBias) : 1.0f)
                : 0.0f;

            for (std::size_t wheelIndex = 0; wheelIndex < vehicle.wheelEntities.size(); ++wheelIndex)
            {
                const UUID wheelId = vehicle.wheelEntities[wheelIndex];
                const EntityID wheelEntity = scene.FindByUUID(wheelId);
                if (wheelEntity == entt::null || !registry.valid(wheelEntity))
                {
                    continue;
                }

                auto* wheelTransform = registry.try_get<TransformComponent>(wheelEntity);
                auto* wheel = registry.try_get<WheelColliderComponent>(wheelEntity);
                if (wheelTransform == nullptr || wheel == nullptr || !wheel->active)
                {
                    continue;
                }

                TransformComponent* visualWheelTransform = wheelTransform;
                UUID visualWheelId = wheelId;
                if (wheel->visualWheelEntity != 0)
                {
                    const EntityID visualEntity = scene.FindByUUID(wheel->visualWheelEntity);
                    if (visualEntity != entt::null && registry.valid(visualEntity))
                    {
                        if (TransformComponent* visualTransform = registry.try_get<TransformComponent>(visualEntity))
                        {
                            visualWheelTransform = visualTransform;
                            visualWheelId = wheel->visualWheelEntity;
                        }
                    }
                }

                if (wheelIndex >= vehicleState.wheels.size())
                {
                    vehicleState.wheels.resize(wheelIndex + 1u);
                }

                NativeVehicleWheelState& wheelState = vehicleState.wheels[wheelIndex];
                const bool recacheWheel =
                    wheelState.wheelId != wheelId ||
                    wheelState.visualWheelId != visualWheelId ||
                    NearlyZeroVector(wheelState.localOffset);
                if (recacheWheel)
                {
                    wheelState = {};
                    wheelState.wheelId = wheelId;
                    wheelState.visualWheelId = visualWheelId;
                    const physx::PxVec3 localOffsetPx = !NearlyZeroVector(wheel->suspensionAttachPoint)
                        ? ToPxVec3(wheel->suspensionAttachPoint)
                        : chassisPose.q.rotateInv(ToPxVec3(Subtract(wheelTransform->position, chassisPosition)));
                    wheelState.localOffset = FromPxVec3(localOffsetPx);
                    wheelState.baseVisualRotation = visualWheelTransform->rotation;
                }

                const float steerResponse = std::clamp(simVehicle.steerSensitivity, 0.0f, 10.0f);
                const float steerLerp = std::clamp(steerResponse * fixedDeltaTimeSeconds * 6.0f, 0.0f, 1.0f);
                const float targetSteer = wheel->steerable ? steerTarget : 0.0f;
                wheelState.steerAngleDegrees += (targetSteer - wheelState.steerAngleDegrees) * steerLerp;

                const physx::PxVec3 attachmentOffset = chassisPose.q.rotate(ToPxVec3(wheelState.localOffset));
                const physx::PxVec3 attachment = chassisPose.p + attachmentOffset;
                const std::array<float, 3> localSuspensionAxis = NormalizeArrayOrFallback(wheel->suspensionAxis, { 0.0f, -1.0f, 0.0f });
                const physx::PxVec3 worldSuspensionAxisPx = chassisPose.q.rotate(ToPxVec3(localSuspensionAxis));
                const physx::PxVec3 up = -worldSuspensionAxisPx;
                const physx::PxVec3 right = ToPxVec3(chassisRight);
                const physx::PxQuat steerQuat(DegreesToRadians(wheelState.steerAngleDegrees), up);
                const std::array<float, 3> wheelForward = NormalizeArrayOrFallback(
                    FromPxVec3(steerQuat.rotate(ToPxVec3(chassisForward))),
                    chassisForward);
                const std::array<float, 3> wheelRight = NormalizeArrayOrFallback(
                    FromPxVec3(steerQuat.rotate(right)),
                    chassisRight);

                const float restLength = std::max(0.01f, wheel->suspensionRestLength > 0.0f ? wheel->suspensionRestLength : wheel->suspensionTravel);
                const float suspensionTravel = std::max(
                    0.01f,
                    (wheel->suspensionMaxCompression + wheel->suspensionMaxDroop) > 0.0f
                        ? (wheel->suspensionMaxCompression + wheel->suspensionMaxDroop)
                        : (wheel->suspensionTravel + simVehicle.suspensionTravel));
                const float wheelRadius = std::max(0.05f, wheel->radius);
                const physx::PxVec3 rayStart = attachment + up * wheelRadius;
                const physx::PxVec3 rayDirection = -up;
                const float rayLength = wheelRadius + suspensionTravel;

                physx::PxRaycastBuffer hitBuffer;
                physx::PxQueryFilterData filterData(physx::PxQueryFlag::eSTATIC | physx::PxQueryFlag::eDYNAMIC);
                const bool hit = m_PhysXScene->raycast(rayStart, rayDirection, rayLength, hitBuffer, physx::PxHitFlag::eDEFAULT, filterData);

                float compression = 0.0f;
                float suspensionLength = suspensionTravel;
                std::array<float, 3> wheelWorldPosition = Add(chassisPosition, FromPxVec3(attachmentOffset));
                if (hit && hitBuffer.hasBlock && hitBuffer.block.actor != chassisActor)
                {
                    suspensionLength = std::clamp(hitBuffer.block.distance - wheelRadius, 0.0f, suspensionTravel);
                    compression = std::clamp((restLength - suspensionLength) / std::max(0.01f, wheel->suspensionMaxCompression + restLength), 0.0f, 1.0f);
                    wheelWorldPosition = FromPxVec3(attachment + rayDirection * suspensionLength);

                    const physx::PxVec3 pointVelocityPx = physx::PxRigidBodyExt::getVelocityAtPos(*chassisActor, attachment);
                    const std::array<float, 3> pointVelocity = FromPxVec3(pointVelocityPx);
                    const float suspensionVelocity = DotProduct(pointVelocity, chassisUp);
                    const float springForce =
                        compression * std::max(0.0f, wheel->suspensionStiffness + simVehicle.suspensionStiffness) -
                        suspensionVelocity * std::max(0.0f, wheel->suspensionDamping + simVehicle.suspensionDamping);
                    if (springForce > 0.0f)
                    {
                        physx::PxRigidBodyExt::addForceAtPos(
                            *chassisActor,
                            up * springForce,
                            attachment,
                            physx::PxForceMode::eFORCE,
                            true);
                    }

                    const float lateralSpeed = DotProduct(pointVelocity, wheelRight);
                    const float longitudinalSpeed = DotProduct(pointVelocity, wheelForward);
                    const float tireGrip = std::max(0.0f, wheel->tireFriction * wheel->tireFrictionScale * simVehicle.tireFrictionScale);
                    const std::array<float, 3> lateralForce = Multiply(
                        wheelRight,
                        -lateralSpeed * tireGrip * std::max(150.0f, wheel->wheelMass * 45.0f));
                    physx::PxRigidBodyExt::addForceAtPos(
                        *chassisActor,
                        ToPxVec3(lateralForce),
                        attachment,
                        physx::PxForceMode::eFORCE,
                        true);

                    if (wheel->driven)
                    {
                        float axleDriveTorque = throttleTorque;
                        std::size_t axleDrivenCount = drivenWheelCount;
                        if (wheel->axleType == VehicleAxleType::Front && hasFrontDrive)
                        {
                            axleDriveTorque = frontDriveTorque;
                            axleDrivenCount = std::max<std::size_t>(1u, frontDrivenWheelCount);
                        }
                        else if (wheel->axleType == VehicleAxleType::Rear && hasRearDrive)
                        {
                            axleDriveTorque = rearDriveTorque;
                            axleDrivenCount = std::max<std::size_t>(1u, rearDrivenWheelCount);
                        }
                        const float drivePerWheel = axleDriveTorque / static_cast<float>(std::max<std::size_t>(1u, axleDrivenCount));
                        const std::array<float, 3> driveForce = Multiply(wheelForward, drivePerWheel * std::max(0.1f, compression));
                        physx::PxRigidBodyExt::addForceAtPos(
                            *chassisActor,
                            ToPxVec3(driveForce),
                            attachment,
                            physx::PxForceMode::eFORCE,
                            true);
                    }

                    if (brakeStrength > 0.0f || (wheel->handbrakeAffected && handbrakeStrength > 0.0f))
                    {
                        const float brakeDirection = longitudinalSpeed > 0.0f ? -1.0f : 1.0f;
                        const float axleBrakeStrength =
                            wheel->axleType == VehicleAxleType::Front ? frontBrakeStrength : rearBrakeStrength;
                        const float totalBrakeStrength = axleBrakeStrength + (wheel->handbrakeAffected ? handbrakeStrength : 0.0f);
                        const std::array<float, 3> brakeForce = Multiply(wheelForward, brakeDirection * totalBrakeStrength);
                        physx::PxRigidBodyExt::addForceAtPos(
                            *chassisActor,
                            ToPxVec3(brakeForce),
                            attachment,
                            physx::PxForceMode::eFORCE,
                            true);
                    }

                    wheelState.spinAngleDegrees +=
                        (longitudinalSpeed / std::max(0.05f, wheelRadius)) * fixedDeltaTimeSeconds * (180.0f / kPi);
                }

                visualWheelTransform->position = wheelWorldPosition;
                const std::array<float, 3> localSpinAxis = NormalizeArrayOrFallback(wheel->wheelRotationAxis, { 1.0f, 0.0f, 0.0f });
                const std::array<float, 3> localSteerAxis = NormalizeArrayOrFallback(Negate(localSuspensionAxis), { 0.0f, 1.0f, 0.0f });
                const physx::PxQuat baseVisualRotation = ToPxQuatDegrees(wheelState.baseVisualRotation);
                const physx::PxQuat steerVisualRotation(
                    DegreesToRadians(wheel->steerable ? wheelState.steerAngleDegrees : 0.0f),
                    ToPxVec3(localSteerAxis));
                const physx::PxQuat spinVisualRotation(
                    DegreesToRadians(wheelState.spinAngleDegrees),
                    ToPxVec3(localSpinAxis));
                visualWheelTransform->rotation = FromPxQuatDegrees(baseVisualRotation * steerVisualRotation * spinVisualRotation);
                visualWheelTransform->dirty = true;
            }

            if (vehicle.sleepWhenInactive && std::abs(throttleInput) < 1.0e-4f && std::abs(steerInput) < 1.0e-4f && brakeInput < 1.0e-4f)
            {
                chassisBody->sleeping = ComputeLength(chassisLinearVelocity) < 0.05f;
            }

            vehicleTransform.position = chassisTransform->position;
            vehicleTransform.rotation = chassisTransform->rotation;
            vehicleTransform.dirty = true;
        }

        for (auto it = m_NativeVehicles.begin(); it != m_NativeVehicles.end();)
        {
            if (!activeVehicles.contains(it->first))
            {
                it = m_NativeVehicles.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    bool PhysXBackend::SyncNativeActor(
        const NativeActorKey entityKey,
        TransformComponent& transform,
        const ColliderComponent& collider,
        RigidBodyComponent* rigidBody,
        const MeshRendererComponent* meshRenderer)
    {
        auto existing = m_NativeActors.find(entityKey);
        if (existing != m_NativeActors.end() &&
            NeedsActorRebuild(existing->second, transform, collider, rigidBody, meshRenderer))
        {
            DestroyNativeActor(existing->second);
            m_NativeActors.erase(existing);
            existing = m_NativeActors.end();
        }

        if (existing == m_NativeActors.end())
        {
            NativeActorState actorState {};
            if (!CreateNativeActor(entityKey, transform, collider, rigidBody, meshRenderer, actorState))
            {
                return false;
            }

            existing = m_NativeActors.emplace(entityKey, std::move(actorState)).first;
        }

        NativeActorState& actorState = existing->second;
        if (actorState.actor == nullptr)
        {
            return false;
        }

        const physx::PxTransform actorPose(
            ToPxVec3(transform.position),
            ToPxQuatDegrees(transform.rotation));
        const bool poseEdited =
            !NearlyEqualArray(actorState.lastPosition, transform.position) ||
            !NearlyEqualArray(actorState.lastRotation, transform.rotation);

        if (actorState.bodyType == RigidBodyType::Static)
        {
            if (poseEdited)
            {
                actorState.actor->setGlobalPose(actorPose);
                actorState.lastPosition = transform.position;
                actorState.lastRotation = transform.rotation;
            }
            return true;
        }

        auto* dynamicActor = actorState.actor->is<physx::PxRigidDynamic>();
        if (dynamicActor == nullptr)
        {
            return true;
        }

        if (rigidBody != nullptr)
        {
            dynamicActor->setActorFlag(physx::PxActorFlag::eDISABLE_GRAVITY, !rigidBody->enableGravity);
            dynamicActor->setRigidBodyFlag(physx::PxRigidBodyFlag::eENABLE_CCD, rigidBody->enableCCD || m_Settings.enableCCD);
            dynamicActor->setLinearDamping(std::max(0.0f, rigidBody->linearDamping));
            dynamicActor->setAngularDamping(std::max(0.0f, rigidBody->angularDamping));
            dynamicActor->setMaxLinearVelocity(std::max(0.0f, rigidBody->maxLinearVelocity));
            dynamicActor->setMaxAngularVelocity(DegreesToRadians(std::max(0.0f, rigidBody->maxAngularVelocity)));
            dynamicActor->setRigidDynamicLockFlags(BuildLockFlags(*rigidBody));
            dynamicActor->setLinearVelocity(ToPxVec3(rigidBody->linearVelocity), true);
            dynamicActor->setAngularVelocity(
                physx::PxVec3(
                    DegreesToRadians(rigidBody->angularVelocity[0]),
                    DegreesToRadians(rigidBody->angularVelocity[1]),
                    DegreesToRadians(rigidBody->angularVelocity[2])),
                true);
            if (actorState.bodyType == RigidBodyType::Dynamic)
            {
                physx::PxRigidBodyExt::setMassAndUpdateInertia(
                    *dynamicActor,
                    std::max(0.001f, rigidBody->mass));
            }
        }

        if (actorState.bodyType == RigidBodyType::Kinematic)
        {
            dynamicActor->setKinematicTarget(actorPose);
            actorState.lastPosition = transform.position;
            actorState.lastRotation = transform.rotation;
        }
        else if (poseEdited)
        {
            dynamicActor->setGlobalPose(actorPose, true);
            actorState.lastPosition = transform.position;
            actorState.lastRotation = transform.rotation;
        }

        return true;
    }

    void PhysXBackend::SimulateNative(Scene& scene, const float fixedDeltaTimeSeconds)
    {
        if (m_PhysXScene == nullptr || m_PhysX == nullptr || m_DefaultMaterial == nullptr)
        {
            return;
        }

        auto& registry = scene.GetRegistry();
        m_BlastDestructionService.SyncScene(scene);

        std::unordered_map<NativeActorKey, std::array<float, 3>> previousDestructibleVelocities;
        auto destructibleVelocityView = registry.view<DestructibleComponent, RigidBodyComponent>();
        previousDestructibleVelocities.reserve(destructibleVelocityView.size_hint());
        for (const EntityID entity : destructibleVelocityView)
        {
            const auto& destructible = destructibleVelocityView.get<DestructibleComponent>(entity);
            const auto& rigidBody = destructibleVelocityView.get<RigidBodyComponent>(entity);
            if (!destructible.active || !rigidBody.active)
            {
                continue;
            }

            previousDestructibleVelocities.emplace(
                static_cast<NativeActorKey>(entity),
                rigidBody.linearVelocity);
        }

        std::unordered_set<NativeActorKey> activeControllers;
        activeControllers.reserve(m_NativeControllers.size() + 8u);
        auto controllerView = registry.view<TransformComponent, CharacterControllerComponent>();
        for (const EntityID entity : controllerView)
        {
            auto& transform = controllerView.get<TransformComponent>(entity);
            auto& controller = controllerView.get<CharacterControllerComponent>(entity);
            const NativeActorKey entityKey = static_cast<NativeActorKey>(entity);
            if (!controller.active)
            {
                controller.isGrounded = false;
                auto existing = m_NativeControllers.find(entityKey);
                if (existing != m_NativeControllers.end())
                {
                    DestroyNativeController(existing->second);
                    m_NativeControllers.erase(existing);
                }
                continue;
            }

            auto existingActor = m_NativeActors.find(entityKey);
            if (existingActor != m_NativeActors.end())
            {
                DestroyNativeActor(existingActor->second);
                m_NativeActors.erase(existingActor);
            }

            activeControllers.insert(entityKey);
            SyncNativeController(
                entityKey,
                transform,
                controller,
                registry.try_get<RigidBodyComponent>(entity),
                0.0f);
        }

        for (auto controllerIt = m_NativeControllers.begin(); controllerIt != m_NativeControllers.end();)
        {
            if (!activeControllers.contains(controllerIt->first))
            {
                DestroyNativeController(controllerIt->second);
                controllerIt = m_NativeControllers.erase(controllerIt);
            }
            else
            {
                ++controllerIt;
            }
        }

        std::unordered_set<NativeActorKey> activeActors;
        activeActors.reserve(m_NativeActors.size() + 16u);
        auto colliderView = registry.view<TransformComponent, ColliderComponent>();
        for (const EntityID entity : colliderView)
        {
            auto& transform = colliderView.get<TransformComponent>(entity);
            const auto& collider = colliderView.get<ColliderComponent>(entity);
            const NativeActorKey entityKey = static_cast<NativeActorKey>(entity);
            if (activeControllers.contains(entityKey))
            {
                auto existing = m_NativeActors.find(entityKey);
                if (existing != m_NativeActors.end())
                {
                    DestroyNativeActor(existing->second);
                    m_NativeActors.erase(existing);
                }
                continue;
            }

            if (!collider.active)
            {
                auto existing = m_NativeActors.find(entityKey);
                if (existing != m_NativeActors.end())
                {
                    DestroyNativeActor(existing->second);
                    m_NativeActors.erase(existing);
                }
                continue;
            }

            RigidBodyComponent* rigidBody = registry.try_get<RigidBodyComponent>(entity);
            if (rigidBody != nullptr && !rigidBody->active)
            {
                auto existing = m_NativeActors.find(entityKey);
                if (existing != m_NativeActors.end())
                {
                    DestroyNativeActor(existing->second);
                    m_NativeActors.erase(existing);
                }
                continue;
            }

            const MeshRendererComponent* meshRenderer = registry.try_get<MeshRendererComponent>(entity);
            activeActors.insert(entityKey);
            SyncNativeActor(entityKey, transform, collider, rigidBody, meshRenderer);
        }

        for (auto actorIt = m_NativeActors.begin(); actorIt != m_NativeActors.end();)
        {
            if (!activeActors.contains(actorIt->first))
            {
                DestroyNativeActor(actorIt->second);
                actorIt = m_NativeActors.erase(actorIt);
            }
            else
            {
                ++actorIt;
            }
        }

        DestroyNativeJoints();

        auto resolveActorByUUID = [&](const UUID id, bool& missingReference) -> physx::PxRigidActor*
        {
            if (id == 0)
            {
                return nullptr;
            }

            const EntityID entity = scene.FindByUUID(id);
            if (entity == entt::null)
            {
                missingReference = true;
                return nullptr;
            }

            const auto actorIt = m_NativeActors.find(static_cast<NativeActorKey>(entity));
            if (actorIt == m_NativeActors.end() || actorIt->second.actor == nullptr)
            {
                missingReference = true;
                return nullptr;
            }

            return actorIt->second.actor;
        };

        auto configureJoint = [&](physx::PxJoint& pxJoint, const JointComponent& joint, physx::PxRigidActor* actorA, physx::PxRigidActor* actorB)
        {
            ApplyJointCommonSettings(pxJoint, joint);
            ApplyJointSolverIterations(actorA, actorB, joint);
            pxJoint.setConstraintFlag(physx::PxConstraintFlag::eDRIVE_LIMITS_ARE_FORCES, true);
        };

        auto fixedJointView = registry.view<TransformComponent, JointComponent, FixedJointComponent>();
        for (const EntityID entity : fixedJointView)
        {
            const auto& transform = fixedJointView.get<TransformComponent>(entity);
            const auto& joint = fixedJointView.get<JointComponent>(entity);
            if (!joint.active)
            {
                continue;
            }

            bool missingReference = false;
            physx::PxRigidActor* actorA = resolveActorByUUID(joint.connectedBodyA, missingReference);
            physx::PxRigidActor* actorB = resolveActorByUUID(joint.connectedBodyB, missingReference);
            if (missingReference || (actorA == nullptr && actorB == nullptr))
            {
                continue;
            }

            const physx::PxTransform jointWorldFrame(
                ToPxVec3(transform.position),
                ToPxQuatDegrees(transform.rotation));
            physx::PxFixedJoint* pxJoint = PxFixedJointCreate(
                *m_PhysX,
                actorA,
                BuildLocalJointFrame(actorA, jointWorldFrame),
                actorB,
                BuildLocalJointFrame(actorB, jointWorldFrame));
            if (pxJoint == nullptr)
            {
                continue;
            }

            configureJoint(*pxJoint, joint, actorA, actorB);
            m_NativeJoints.emplace(static_cast<NativeActorKey>(entity), pxJoint);
        }

        auto hingeView = registry.view<TransformComponent, JointComponent, HingeJointComponent>();
        for (const EntityID entity : hingeView)
        {
            const auto& transform = hingeView.get<TransformComponent>(entity);
            const auto& joint = hingeView.get<JointComponent>(entity);
            const auto& hinge = hingeView.get<HingeJointComponent>(entity);
            if (!joint.active)
            {
                continue;
            }

            bool missingReference = false;
            physx::PxRigidActor* actorA = resolveActorByUUID(joint.connectedBodyA, missingReference);
            physx::PxRigidActor* actorB = resolveActorByUUID(joint.connectedBodyB, missingReference);
            if (missingReference || (actorA == nullptr && actorB == nullptr))
            {
                continue;
            }

            const physx::PxTransform jointWorldFrame = BuildAxisJointFrame(transform, hinge.axis);
            physx::PxRevoluteJoint* pxJoint = PxRevoluteJointCreate(
                *m_PhysX,
                actorA,
                BuildLocalJointFrame(actorA, jointWorldFrame),
                actorB,
                BuildLocalJointFrame(actorB, jointWorldFrame));
            if (pxJoint == nullptr)
            {
                continue;
            }

            configureJoint(*pxJoint, joint, actorA, actorB);
            if (hinge.enableLimits)
            {
                pxJoint->setLimit(physx::PxJointAngularLimitPair(
                    DegreesToRadians(hinge.lowerLimitDegrees),
                    DegreesToRadians(hinge.upperLimitDegrees)));
                pxJoint->setRevoluteJointFlag(physx::PxRevoluteJointFlag::eLIMIT_ENABLED, true);
            }

            if (hinge.enableMotor)
            {
                pxJoint->setDriveVelocity(DegreesToRadians(hinge.motorVelocityDegreesPerSecond));
                pxJoint->setDriveForceLimit(std::max(0.0f, hinge.motorMaxForce));
                pxJoint->setRevoluteJointFlag(physx::PxRevoluteJointFlag::eDRIVE_ENABLED, true);
            }

            m_NativeJoints.emplace(static_cast<NativeActorKey>(entity), pxJoint);
        }

        auto sliderView = registry.view<TransformComponent, JointComponent, SliderJointComponent>();
        for (const EntityID entity : sliderView)
        {
            const auto& transform = sliderView.get<TransformComponent>(entity);
            const auto& joint = sliderView.get<JointComponent>(entity);
            const auto& slider = sliderView.get<SliderJointComponent>(entity);
            if (!joint.active)
            {
                continue;
            }

            bool missingReference = false;
            physx::PxRigidActor* actorA = resolveActorByUUID(joint.connectedBodyA, missingReference);
            physx::PxRigidActor* actorB = resolveActorByUUID(joint.connectedBodyB, missingReference);
            if (missingReference || (actorA == nullptr && actorB == nullptr))
            {
                continue;
            }

            const physx::PxTransform jointWorldFrame = BuildAxisJointFrame(transform, slider.axis);
            physx::PxD6Joint* pxJoint = PxD6JointCreate(
                *m_PhysX,
                actorA,
                BuildLocalJointFrame(actorA, jointWorldFrame),
                actorB,
                BuildLocalJointFrame(actorB, jointWorldFrame));
            if (pxJoint == nullptr)
            {
                continue;
            }

            configureJoint(*pxJoint, joint, actorA, actorB);
            pxJoint->setMotion(physx::PxD6Axis::eX, slider.enableLimits ? physx::PxD6Motion::eLIMITED : physx::PxD6Motion::eFREE);
            pxJoint->setMotion(physx::PxD6Axis::eY, physx::PxD6Motion::eLOCKED);
            pxJoint->setMotion(physx::PxD6Axis::eZ, physx::PxD6Motion::eLOCKED);
            pxJoint->setMotion(physx::PxD6Axis::eTWIST, physx::PxD6Motion::eLOCKED);
            pxJoint->setMotion(physx::PxD6Axis::eSWING1, physx::PxD6Motion::eLOCKED);
            pxJoint->setMotion(physx::PxD6Axis::eSWING2, physx::PxD6Motion::eLOCKED);

            if (slider.enableLimits)
            {
                pxJoint->setLinearLimit(
                    physx::PxD6Axis::eX,
                    physx::PxJointLinearLimitPair(
                        slider.lowerLimit,
                        slider.upperLimit,
                        physx::PxSpring(0.0f, 0.0f)));
            }

            if (slider.enableMotor)
            {
                const physx::PxD6JointDrive drive(
                    0.0f,
                    std::max(1.0f, slider.motorMaxForce * 0.05f),
                    std::max(0.0f, slider.motorMaxForce),
                    false);
                pxJoint->setDrive(physx::PxD6Drive::eX, drive);
                pxJoint->setDriveVelocity(
                    physx::PxVec3(slider.motorSpeed, 0.0f, 0.0f),
                    physx::PxVec3(0.0f));
            }

            m_NativeJoints.emplace(static_cast<NativeActorKey>(entity), pxJoint);
        }

        auto d6View = registry.view<TransformComponent, JointComponent, D6JointComponent>();
        for (const EntityID entity : d6View)
        {
            const auto& transform = d6View.get<TransformComponent>(entity);
            const auto& joint = d6View.get<JointComponent>(entity);
            const auto& d6 = d6View.get<D6JointComponent>(entity);
            if (!joint.active)
            {
                continue;
            }

            bool missingReference = false;
            physx::PxRigidActor* actorA = resolveActorByUUID(joint.connectedBodyA, missingReference);
            physx::PxRigidActor* actorB = resolveActorByUUID(joint.connectedBodyB, missingReference);
            if (missingReference || (actorA == nullptr && actorB == nullptr))
            {
                continue;
            }

            const physx::PxTransform jointWorldFrame(
                ToPxVec3(transform.position),
                ToPxQuatDegrees(transform.rotation));
            physx::PxD6Joint* pxJoint = PxD6JointCreate(
                *m_PhysX,
                actorA,
                BuildLocalJointFrame(actorA, jointWorldFrame),
                actorB,
                BuildLocalJointFrame(actorB, jointWorldFrame));
            if (pxJoint == nullptr)
            {
                continue;
            }

            configureJoint(*pxJoint, joint, actorA, actorB);
            pxJoint->setMotion(physx::PxD6Axis::eX, ToPxD6Motion(d6.linearMotion[0]));
            pxJoint->setMotion(physx::PxD6Axis::eY, ToPxD6Motion(d6.linearMotion[1]));
            pxJoint->setMotion(physx::PxD6Axis::eZ, ToPxD6Motion(d6.linearMotion[2]));
            pxJoint->setMotion(physx::PxD6Axis::eTWIST, ToPxD6Motion(d6.angularMotion[0]));
            pxJoint->setMotion(physx::PxD6Axis::eSWING1, ToPxD6Motion(d6.angularMotion[1]));
            pxJoint->setMotion(physx::PxD6Axis::eSWING2, ToPxD6Motion(d6.angularMotion[2]));

            const float linearLimit = std::max(0.0f, d6.linearLimit);
            if (d6.linearMotion[0] == JointMotionMode::Limited)
            {
                pxJoint->setLinearLimit(physx::PxD6Axis::eX, physx::PxJointLinearLimitPair(-linearLimit, linearLimit, physx::PxSpring(0.0f, 0.0f)));
            }
            if (d6.linearMotion[1] == JointMotionMode::Limited)
            {
                pxJoint->setLinearLimit(physx::PxD6Axis::eY, physx::PxJointLinearLimitPair(-linearLimit, linearLimit, physx::PxSpring(0.0f, 0.0f)));
            }
            if (d6.linearMotion[2] == JointMotionMode::Limited)
            {
                pxJoint->setLinearLimit(physx::PxD6Axis::eZ, physx::PxJointLinearLimitPair(-linearLimit, linearLimit, physx::PxSpring(0.0f, 0.0f)));
            }
            if (d6.angularMotion[0] == JointMotionMode::Limited)
            {
                pxJoint->setTwistLimit(physx::PxJointAngularLimitPair(
                    DegreesToRadians(d6.twistLowerLimitDegrees),
                    DegreesToRadians(d6.twistUpperLimitDegrees)));
            }
            if (d6.angularMotion[1] == JointMotionMode::Limited || d6.angularMotion[2] == JointMotionMode::Limited)
            {
                pxJoint->setSwingLimit(physx::PxJointLimitCone(
                    DegreesToRadians(d6.swingYLimitDegrees),
                    DegreesToRadians(d6.swingZLimitDegrees)));
            }

            physx::PxTransform drivePose(physx::PxIdentity);
            bool useDrivePose = false;
            if (d6.enableLinearDrive)
            {
                const physx::PxD6JointDrive drive(
                    std::max(0.0f, d6.linearDriveStiffness),
                    std::max(0.0f, d6.linearDriveDamping),
                    std::max(0.0f, d6.linearDriveForceLimit),
                    false);
                pxJoint->setDrive(physx::PxD6Drive::eX, drive);
                pxJoint->setDrive(physx::PxD6Drive::eY, drive);
                pxJoint->setDrive(physx::PxD6Drive::eZ, drive);
                drivePose.p = physx::PxVec3(
                    d6.linearDrivePositionTarget[0],
                    d6.linearDrivePositionTarget[1],
                    d6.linearDrivePositionTarget[2]);
                useDrivePose = true;
            }
            if (d6.enableAngularDrive)
            {
                pxJoint->setAngularDriveConfig(physx::PxD6AngularDriveConfig::eSLERP);
                pxJoint->setDrive(
                    physx::PxD6Drive::eSLERP,
                    physx::PxD6JointDrive(
                        std::max(0.0f, d6.angularDriveStiffness),
                        std::max(0.0f, d6.angularDriveDamping),
                        std::max(0.0f, d6.angularDriveForceLimit),
                        false));
                drivePose.q = ToPxQuatDegrees(d6.angularDrivePositionTarget);
                useDrivePose = true;
            }
            if (useDrivePose)
            {
                pxJoint->setDrivePosition(drivePose, true);
            }
            if (d6.enableLinearDrive || d6.enableAngularDrive)
            {
                pxJoint->setDriveVelocity(
                    physx::PxVec3(
                        d6.linearDriveVelocityTarget[0],
                        d6.linearDriveVelocityTarget[1],
                        d6.linearDriveVelocityTarget[2]),
                    physx::PxVec3(
                        DegreesToRadians(d6.angularDriveVelocityTarget[0]),
                        DegreesToRadians(d6.angularDriveVelocityTarget[1]),
                        DegreesToRadians(d6.angularDriveVelocityTarget[2])),
                    true);
            }

            m_NativeJoints.emplace(static_cast<NativeActorKey>(entity), pxJoint);
        }

        SimulateNativeVehicles(scene, fixedDeltaTimeSeconds);

        m_PhysXScene->simulate(fixedDeltaTimeSeconds);
        m_PhysXScene->fetchResults(true);

        if (m_ControllerManager != nullptr && !m_NativeControllers.empty())
        {
            if (m_ControllerManager->getNbControllers() > 1)
            {
                m_ControllerManager->computeInteractions(fixedDeltaTimeSeconds, nullptr);
            }

            for (const EntityID entity : controllerView)
            {
                auto& transform = controllerView.get<TransformComponent>(entity);
                auto& controller = controllerView.get<CharacterControllerComponent>(entity);
                if (!controller.active)
                {
                    continue;
                }

                SyncNativeController(
                    static_cast<NativeActorKey>(entity),
                    transform,
                    controller,
                    registry.try_get<RigidBodyComponent>(entity),
                    fixedDeltaTimeSeconds);
            }
        }

        for (auto& [entityKey, actorState] : m_NativeActors)
        {
            const EntityID entity = static_cast<EntityID>(entityKey);
            if (!registry.valid(entity) || actorState.actor == nullptr)
            {
                continue;
            }

            auto* transform = registry.try_get<TransformComponent>(entity);
            auto* rigidBody = registry.try_get<RigidBodyComponent>(entity);
            if (transform == nullptr)
            {
                continue;
            }

            const physx::PxTransform pose = actorState.actor->getGlobalPose();
            transform->position = FromPxVec3(pose.p);
            transform->rotation = FromPxQuatDegrees(pose.q);
            transform->dirty = true;
            actorState.lastPosition = transform->position;
            actorState.lastRotation = transform->rotation;

            if (rigidBody != nullptr)
            {
                if (const auto* dynamicActor = actorState.actor->is<physx::PxRigidDynamic>())
                {
                    rigidBody->linearVelocity = FromPxVec3(dynamicActor->getLinearVelocity());
                    const physx::PxVec3 angularVelocity = dynamicActor->getAngularVelocity();
                    rigidBody->angularVelocity = {
                        RadiansToDegrees(angularVelocity.x),
                        RadiansToDegrees(angularVelocity.y),
                        RadiansToDegrees(angularVelocity.z)
                    };
                    rigidBody->sleeping = dynamicActor->isSleeping();

                    const auto previousVelocity = previousDestructibleVelocities.find(entityKey);
                    if (previousVelocity != previousDestructibleVelocities.end())
                    {
                        m_BlastDestructionService.SubmitVelocityDamage(
                            scene,
                            entity,
                            previousVelocity->second,
                            rigidBody->linearVelocity,
                            fixedDeltaTimeSeconds);
                    }
                }
                else
                {
                    rigidBody->sleeping = true;
                }
            }
        }

        m_BlastDestructionService.ProcessPendingFractures(scene);
        m_BlastDestructionService.Tick(scene, fixedDeltaTimeSeconds);
    }
#endif
}
