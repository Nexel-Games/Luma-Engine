#include "Physics/Backends/PhysXBackend.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
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
#endif
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "Luma/Asset/Core/MeshAssetIO.h"
#include "Luma/Core/App/Project.h"
#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Renderer/PrimitiveMeshFactory.h"
#include "Luma/Scene/ColliderComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/RigidBodyComponent.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/TransformComponent.h"

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
        // Native PhysX wiring can be enabled here once PhysX libs are linked in CMake.
        m_UsingNativePhysX = false;
        LUMA_LOG_WARN("Physics", "LUMA_ENABLE_PHYSX is set, but native PhysX linkage is not configured yet. Using fallback solver.");
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
    }

    void PhysXBackend::Simulate(Scene& scene, const float fixedDeltaTimeSeconds)
    {
        if (!m_Initialized || fixedDeltaTimeSeconds <= 0.0f)
        {
            return;
        }

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
}
