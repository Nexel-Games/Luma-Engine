#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <unordered_map>

#include "Luma/Physics/IPhysicsBackend.h"
#include "Luma/Physics/Destruction/BlastDestructionService.h"
#include "Luma/Renderer/PrimitiveMeshFactory.h"
#include "Luma/Scene/UUID.h"
#include "Luma/Scene/VehicleTuningAsset.h"

#if defined(LUMA_ENABLE_PHYSX) && LUMA_ENABLE_PHYSX
#include <PxPhysicsAPI.h>
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

namespace Luma
{
    struct ColliderComponent;
    struct CharacterControllerComponent;
    struct MeshRendererComponent;
    struct RigidBodyComponent;
    struct TransformComponent;
    struct VehicleComponent;
    struct WheelColliderComponent;

    class PhysXBackend final : public IPhysicsBackend
    {
    public:
        std::string_view GetName() const override;
        PhysicsBackendType GetType() const override;

        bool Initialize(const PhysicsSettings& settings) override;
        void Shutdown() override;
        void Simulate(Scene& scene, float fixedDeltaTimeSeconds) override;

    private:
#if defined(LUMA_ENABLE_PHYSX) && LUMA_ENABLE_PHYSX
        struct NativeActorState
        {
            physx::PxRigidActor* actor = nullptr;
            physx::PxShape* shapeHandle = nullptr;
            physx::PxMaterial* materialHandle = nullptr;
            ColliderShapeType shape = ColliderShapeType::Box;
            RigidBodyType bodyType = RigidBodyType::Static;
            bool trigger = false;
            bool meshConvex = true;
            bool usePrimitiveMesh = false;
            PrimitiveType primitive = PrimitiveType::Cube;
            std::string meshSource;
            std::array<float, 3> scale { 1.0f, 1.0f, 1.0f };
            std::array<float, 3> center { 0.0f, 0.0f, 0.0f };
            std::array<float, 3> boxHalfExtents { 0.5f, 0.5f, 0.5f };
            float sphereRadius = 0.5f;
            float capsuleRadius = 0.5f;
            float capsuleHalfHeight = 0.5f;
            PhysicsMaterialDesc materialDesc {};
            std::array<float, 3> lastPosition { 0.0f, 0.0f, 0.0f };
            std::array<float, 3> lastRotation { 0.0f, 0.0f, 0.0f };
        };

        struct NativeCookedMeshState
        {
            physx::PxConvexMesh* convexMesh = nullptr;
            physx::PxTriangleMesh* triangleMesh = nullptr;
        };

        struct NativeControllerState
        {
            physx::PxController* controller = nullptr;
            float radius = 0.35f;
            float totalHeight = 1.8f;
            float stepOffset = 0.35f;
            float slopeLimitDegrees = 45.0f;
            float skinWidth = 0.05f;
            float minMoveDistance = 0.001f;
            float gravityScale = 1.0f;
            std::uint32_t collisionLayer = 1;
            std::uint32_t collisionMask = 0xFFFFFFFFu;
            std::uint8_t movementMode = 0;
            std::array<float, 3> lastPosition { 0.0f, 0.0f, 0.0f };
            std::array<float, 3> velocity { 0.0f, 0.0f, 0.0f };
        };

        struct NativeVehicleWheelState
        {
            UUID wheelId = 0;
            UUID visualWheelId = 0;
            std::array<float, 3> localOffset { 0.0f, 0.0f, 0.0f };
            std::array<float, 3> baseVisualRotation { 0.0f, 0.0f, 0.0f };
            float steerAngleDegrees = 0.0f;
            float spinAngleDegrees = 0.0f;
        };

        struct NativeVehicleState
        {
            UUID chassisId = 0;
            int currentGear = 1;
            bool reverseGear = false;
            float engineRPM = 900.0f;
            std::vector<NativeVehicleWheelState> wheels;
        };

        using NativeActorKey = std::uint32_t;

        void ShutdownNativePhysX();
        void DestroyNativeJoints();
        void DestroyNativeControllers();
        bool SyncNativeActor(
            NativeActorKey entityKey,
            TransformComponent& transform,
            const ColliderComponent& collider,
            RigidBodyComponent* rigidBody,
            const MeshRendererComponent* meshRenderer);
        bool SyncNativeController(
            NativeActorKey entityKey,
            TransformComponent& transform,
            CharacterControllerComponent& controller,
            RigidBodyComponent* rigidBody,
            float fixedDeltaTimeSeconds);
        bool CreateNativeActor(
            NativeActorKey entityKey,
            const TransformComponent& transform,
            const ColliderComponent& collider,
            RigidBodyComponent* rigidBody,
            const MeshRendererComponent* meshRenderer,
            NativeActorState& outActorState);
        bool CreateNativeController(
            NativeActorKey entityKey,
            const TransformComponent& transform,
            const CharacterControllerComponent& controller,
            NativeControllerState& outControllerState);
        void DestroyNativeActor(NativeActorState& actorState);
        void DestroyNativeController(NativeControllerState& controllerState);
        bool NeedsActorRebuild(
            const NativeActorState& actorState,
            const TransformComponent& transform,
            const ColliderComponent& collider,
            RigidBodyComponent* rigidBody,
            const MeshRendererComponent* meshRenderer) const;
        void SimulateNativeVehicles(Scene& scene, float fixedDeltaTimeSeconds);
        void SimulateNative(Scene& scene, float fixedDeltaTimeSeconds);

        physx::PxDefaultAllocator m_PhysXAllocator;
        physx::PxDefaultErrorCallback m_PhysXErrorCallback;
        physx::PxFoundation* m_PhysXFoundation = nullptr;
        physx::PxPhysics* m_PhysX = nullptr;
        physx::PxCookingParams m_PhysXCookingParams { physx::PxTolerancesScale() };
        physx::PxDefaultCpuDispatcher* m_PhysXDispatcher = nullptr;
        physx::PxScene* m_PhysXScene = nullptr;
        physx::PxControllerManager* m_ControllerManager = nullptr;
        physx::PxMaterial* m_DefaultMaterial = nullptr;
        std::unordered_map<NativeActorKey, NativeActorState> m_NativeActors;
        std::unordered_map<NativeActorKey, NativeControllerState> m_NativeControllers;
        std::unordered_map<NativeActorKey, NativeVehicleState> m_NativeVehicles;
        std::unordered_map<NativeActorKey, physx::PxJoint*> m_NativeJoints;
        std::unordered_map<std::string, NativeCookedMeshState> m_CookedMeshes;
        VehicleTuningAssetCacheService m_VehicleTuningAssetCacheService;
#endif

        PhysicsSettings m_Settings {};
        BlastDestructionService m_BlastDestructionService;
        bool m_Initialized = false;
        bool m_UsingNativePhysX = false;
    };
}
