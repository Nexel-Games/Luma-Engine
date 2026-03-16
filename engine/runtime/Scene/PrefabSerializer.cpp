#include "Luma/Scene/PrefabSerializer.h"

#include <functional>

#include "Luma/Scene/AudioListenerComponent.h"
#include "Luma/Scene/AudioSourceComponent.h"
#include "Luma/Scene/BuoyancyComponent.h"
#include "Luma/Scene/CameraComponent.h"
#include "Luma/Scene/CharacterControllerComponent.h"
#include "Luma/Scene/ColliderComponent.h"
#include "Luma/Scene/D6JointComponent.h"
#include "Luma/Scene/DestructibleComponent.h"
#include "Luma/Scene/DirectionalLightComponent.h"
#include "Luma/Scene/FixedJointComponent.h"
#include "Luma/Scene/ForceFieldComponent.h"
#include "Luma/Scene/HingeJointComponent.h"
#include "Luma/Scene/IDComponent.h"
#include "Luma/Scene/JointComponent.h"
#include "Luma/Scene/LuaScriptComponent.h"
#include "Luma/Scene/MaterialComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/PhysicsEventsComponent.h"
#include "Luma/Scene/PointLightComponent.h"
#include "Luma/Scene/PostProcessComponent.h"
#include "Luma/Scene/PrefabInstanceComponent.h"
#include "Luma/Scene/RagdollComponent.h"
#include "Luma/Scene/RelationshipComponent.h"
#include "Luma/Scene/RigidBodyComponent.h"
#include "Luma/Scene/SceneSerializer.h"
#include "Luma/Scene/SliderJointComponent.h"
#include "Luma/Scene/SkyLightComponent.h"
#include "Luma/Scene/SpotLightComponent.h"
#include "Luma/Scene/TagComponent.h"
#include "Luma/Scene/TransformComponent.h"
#include "Luma/Scene/VehicleComponent.h"
#include "Luma/Scene/VehicleInputComponent.h"
#include "Luma/Scene/WheelColliderComponent.h"

namespace Luma
{
    namespace
    {
        template <typename T>
        void CopyComponentIfPresent(const Scene& sourceScene, const EntityID sourceEntity, Entity& targetEntity)
        {
            const auto& sourceRegistry = sourceScene.GetRegistry();
            if (sourceRegistry.all_of<T>(sourceEntity))
            {
                targetEntity.AddOrReplaceComponent<T>(sourceRegistry.get<T>(sourceEntity));
            }
        }

        EntityID CloneHierarchyRecursive(
            const Scene& sourceScene,
            const EntityID sourceEntity,
            Scene& targetScene,
            const std::string& prefabAsset,
            const bool preserveSourceIds,
            const bool markAsPrefabInstance,
            const bool isRoot)
        {
            const auto& sourceRegistry = sourceScene.GetRegistry();
            const auto& sourceTag = sourceRegistry.get<TagComponent>(sourceEntity);

            Entity targetEntity = targetScene.CreateEntity(sourceTag.name);
            auto& targetTag = targetEntity.GetComponent<TagComponent>();
            targetTag = sourceTag;
            targetEntity.GetComponent<TransformComponent>() = sourceRegistry.get<TransformComponent>(sourceEntity);

            if (preserveSourceIds && sourceRegistry.all_of<IDComponent>(sourceEntity))
            {
                targetScene.SetEntityUUID(targetEntity.GetHandle(), sourceRegistry.get<IDComponent>(sourceEntity).id);
            }

            CopyComponentIfPresent<MeshRendererComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<MaterialComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<CameraComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<LuaScriptComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<AudioSourceComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<AudioListenerComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<DirectionalLightComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<PointLightComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<SpotLightComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<SkyLightComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<PostProcessComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<RigidBodyComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<ColliderComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<PhysicsEventsComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<CharacterControllerComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<BuoyancyComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<ForceFieldComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<VehicleComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<VehicleInputComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<WheelColliderComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<JointComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<FixedJointComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<HingeJointComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<SliderJointComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<D6JointComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<RagdollComponent>(sourceScene, sourceEntity, targetEntity);
            CopyComponentIfPresent<DestructibleComponent>(sourceScene, sourceEntity, targetEntity);

            if (markAsPrefabInstance && sourceRegistry.all_of<IDComponent>(sourceEntity))
            {
                PrefabInstanceComponent prefabInstance {};
                prefabInstance.prefabAsset = prefabAsset;
                prefabInstance.sourceEntityId = sourceRegistry.get<IDComponent>(sourceEntity).id;
                prefabInstance.isRoot = isRoot;
                targetEntity.AddOrReplaceComponent<PrefabInstanceComponent>(std::move(prefabInstance));
            }

            if (sourceRegistry.all_of<RelationshipComponent>(sourceEntity))
            {
                const auto& relationship = sourceRegistry.get<RelationshipComponent>(sourceEntity);
                for (const EntityID child : relationship.children)
                {
                    const EntityID clonedChild = CloneHierarchyRecursive(
                        sourceScene,
                        child,
                        targetScene,
                        prefabAsset,
                        preserveSourceIds,
                        markAsPrefabInstance,
                        false);
                    if (clonedChild != entt::null)
                    {
                        targetScene.SetParent(clonedChild, targetEntity.GetHandle());
                    }
                }
            }

            return targetEntity.GetHandle();
        }
    }

    bool PrefabSerializer::SerializePrefab(
        const Scene& sourceScene,
        const EntityID rootEntity,
        const std::filesystem::path& prefabPath,
        std::string& outError)
    {
        outError.clear();
        const auto& sourceRegistry = sourceScene.GetRegistry();
        if (!sourceRegistry.valid(rootEntity) ||
            !sourceRegistry.all_of<IDComponent, TagComponent, TransformComponent, RelationshipComponent>(rootEntity))
        {
            outError = "Prefab root entity is invalid.";
            return false;
        }

        Scene prefabScene;
        CloneHierarchyRecursive(sourceScene, rootEntity, prefabScene, prefabPath.generic_string(), true, false, true);

        return SceneSerializer::Serialize(prefabScene, prefabPath, outError);
    }

    bool PrefabSerializer::InstantiatePrefab(
        Scene& targetScene,
        const std::filesystem::path& prefabPath,
        EntityID* outRootEntity,
        std::string& outError)
    {
        outError.clear();
        if (outRootEntity != nullptr)
        {
            *outRootEntity = entt::null;
        }

        Scene prefabScene;
        if (!SceneSerializer::Deserialize(prefabPath, prefabScene, outError))
        {
            return false;
        }

        const std::vector<EntityID> roots = prefabScene.GetRootEntities();
        if (roots.empty())
        {
            outError = "Prefab contains no root entity.";
            return false;
        }

        const std::string prefabReference = prefabPath.lexically_normal().generic_string();
        const EntityID rootEntity = CloneHierarchyRecursive(
            prefabScene,
            roots.front(),
            targetScene,
            prefabReference,
            false,
            true,
            true);

        targetScene.UpdateWorldTransforms();
        if (outRootEntity != nullptr)
        {
            *outRootEntity = rootEntity;
        }
        return rootEntity != entt::null;
    }
}
