#include "Luma/Editor/Scene/EntityTemplateCreationService.h"

#include "Luma/Scene/AudioListenerComponent.h"
#include "Luma/Scene/AudioSourceComponent.h"
#include "Luma/Scene/CameraComponent.h"
#include "Luma/Scene/ColliderComponent.h"
#include "Luma/Scene/DirectionalLightComponent.h"
#include "Luma/Scene/MaterialComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/SkyLightComponent.h"
#include "Luma/Scene/TransformComponent.h"

namespace Luma::Editor
{
    namespace
    {
        std::string BaseNameForTemplate(const EntityTemplateKind templateKind)
        {
            switch (templateKind)
            {
            case EntityTemplateKind::Empty:
                return "Entity";
            case EntityTemplateKind::Cube:
                return "Cube";
            case EntityTemplateKind::Plane:
                return "Plane";
            case EntityTemplateKind::Sphere:
                return "Sphere";
            case EntityTemplateKind::Cylinder:
                return "Cylinder";
            case EntityTemplateKind::Capsule:
                return "Capsule";
            case EntityTemplateKind::Cone:
                return "Cone";
            case EntityTemplateKind::Torus:
                return "Torus";
            case EntityTemplateKind::Camera:
                return "Camera";
            case EntityTemplateKind::DirectionalLight:
                return "Directional Light";
            case EntityTemplateKind::SkyLight:
                return "Sky Light";
            case EntityTemplateKind::Player:
                return "Player";
            case EntityTemplateKind::AudioSource:
                return "Audio Source";
            default:
                return "Entity";
            }
        }
    }

    EntityID EntityTemplateCreationService::CreateEntityFromTemplate(
        const EntityTemplateCreationContext& context,
        const EntityTemplateKind templateKind,
        const EntityID parentEntity) const
    {
        if (context.scene == nullptr)
        {
            return entt::null;
        }

        const std::string baseName = BaseNameForTemplate(templateKind);
        const std::string entityName =
            context.generateUniqueEntityName ? context.generateUniqueEntityName(baseName) : baseName;
        Entity entity = context.scene->CreateEntity(entityName);
        auto& registry = context.scene->GetRegistry();
        bool autoPrimitiveCollider = false;
        PrimitiveType autoPrimitiveType = PrimitiveType::Cube;

        if (templateKind == EntityTemplateKind::Camera)
        {
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.position = { 0.0f, 0.8f, 5.5f };
            transform.rotation = { 0.0f, -90.0f, 0.0f };
            transform.dirty = true;
            entity.AddComponent<CameraComponent>();
            entity.AddComponent<AudioListenerComponent>();
        }
        else if (templateKind == EntityTemplateKind::AudioSource)
        {
            entity.AddComponent<AudioSourceComponent>();
        }
        else if (templateKind == EntityTemplateKind::DirectionalLight)
        {
            entity.AddComponent<DirectionalLightComponent>();
        }
        else if (templateKind == EntityTemplateKind::SkyLight)
        {
            auto& skyLight = entity.AddComponent<SkyLightComponent>();
            if (context.initializeSkyLightDefaults)
            {
                context.initializeSkyLightDefaults(skyLight);
            }
        }
        else if (templateKind == EntityTemplateKind::Cube)
        {
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.position = { 0.0f, 0.5f, 0.0f };
            transform.scale = { 1.0f, 1.0f, 1.0f };
            transform.dirty = true;

            auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
            meshRenderer.visible = true;
            meshRenderer.usePrimitive = true;
            meshRenderer.primitive = PrimitiveType::Cube;
            meshRenderer.color = { 0.72f, 0.82f, 0.95f, 1.0f };
            auto& material = entity.AddComponent<MaterialComponent>();
            if (context.initializeDefaultMaterial)
            {
                context.initializeDefaultMaterial(material);
            }
            autoPrimitiveCollider = true;
            autoPrimitiveType = meshRenderer.primitive;
        }
        else if (templateKind == EntityTemplateKind::Plane)
        {
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.position = { 0.0f, 0.0f, 0.0f };
            transform.scale = { 5.0f, 1.0f, 5.0f };
            transform.dirty = true;

            auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
            meshRenderer.visible = true;
            meshRenderer.usePrimitive = true;
            meshRenderer.primitive = PrimitiveType::Plane;
            meshRenderer.color = { 0.72f, 0.74f, 0.78f, 1.0f };
            auto& material = entity.AddComponent<MaterialComponent>();
            if (context.initializeDefaultMaterial)
            {
                context.initializeDefaultMaterial(material);
            }
            autoPrimitiveCollider = true;
            autoPrimitiveType = meshRenderer.primitive;

            auto& collider = entity.AddComponent<ColliderComponent>();
            collider.shape = ColliderShapeType::Box;
            collider.boxHalfExtents = { 0.5f, 0.02f, 0.5f };
            collider.center = { 0.0f, 0.0f, 0.0f };
            collider.active = true;
        }
        else if (templateKind == EntityTemplateKind::Sphere)
        {
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.position = { 0.0f, 0.5f, 0.0f };
            transform.scale = { 1.0f, 1.0f, 1.0f };
            transform.dirty = true;

            auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
            meshRenderer.visible = true;
            meshRenderer.usePrimitive = true;
            meshRenderer.primitive = PrimitiveType::Sphere;
            meshRenderer.color = { 0.94f, 0.78f, 0.62f, 1.0f };
            auto& material = entity.AddComponent<MaterialComponent>();
            if (context.initializeDefaultMaterial)
            {
                context.initializeDefaultMaterial(material);
            }
            autoPrimitiveCollider = true;
            autoPrimitiveType = meshRenderer.primitive;

            auto& collider = entity.AddComponent<ColliderComponent>();
            collider.shape = ColliderShapeType::Mesh;
            collider.meshConvex = true;
            collider.meshSource = "PrimitiveSphere";
            collider.boxHalfExtents = { 0.5f, 0.5f, 0.5f };
            collider.active = true;
        }
        else if (templateKind == EntityTemplateKind::Cylinder)
        {
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.position = { 0.0f, 0.5f, 0.0f };
            transform.scale = { 1.0f, 1.0f, 1.0f };
            transform.dirty = true;

            auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
            meshRenderer.visible = true;
            meshRenderer.usePrimitive = true;
            meshRenderer.primitive = PrimitiveType::Cylinder;
            meshRenderer.color = { 0.66f, 0.86f, 0.92f, 1.0f };
            auto& material = entity.AddComponent<MaterialComponent>();
            if (context.initializeDefaultMaterial)
            {
                context.initializeDefaultMaterial(material);
            }
            autoPrimitiveCollider = true;
            autoPrimitiveType = meshRenderer.primitive;
        }
        else if (templateKind == EntityTemplateKind::Capsule)
        {
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.position = { 0.0f, 0.5f, 0.0f };
            transform.scale = { 1.0f, 1.0f, 1.0f };
            transform.dirty = true;

            auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
            meshRenderer.visible = true;
            meshRenderer.usePrimitive = true;
            meshRenderer.primitive = PrimitiveType::Capsule;
            meshRenderer.color = { 0.72f, 0.90f, 0.76f, 1.0f };
            auto& material = entity.AddComponent<MaterialComponent>();
            if (context.initializeDefaultMaterial)
            {
                context.initializeDefaultMaterial(material);
            }
            autoPrimitiveCollider = true;
            autoPrimitiveType = meshRenderer.primitive;
        }
        else if (templateKind == EntityTemplateKind::Cone)
        {
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.position = { 0.0f, 0.5f, 0.0f };
            transform.scale = { 1.0f, 1.0f, 1.0f };
            transform.dirty = true;

            auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
            meshRenderer.visible = true;
            meshRenderer.usePrimitive = true;
            meshRenderer.primitive = PrimitiveType::Cone;
            meshRenderer.color = { 0.90f, 0.80f, 0.56f, 1.0f };
            auto& material = entity.AddComponent<MaterialComponent>();
            if (context.initializeDefaultMaterial)
            {
                context.initializeDefaultMaterial(material);
            }
            autoPrimitiveCollider = true;
            autoPrimitiveType = meshRenderer.primitive;
        }
        else if (templateKind == EntityTemplateKind::Torus)
        {
            auto& transform = entity.GetComponent<TransformComponent>();
            transform.position = { 0.0f, 0.6f, 0.0f };
            transform.scale = { 1.0f, 1.0f, 1.0f };
            transform.dirty = true;

            auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
            meshRenderer.visible = true;
            meshRenderer.usePrimitive = true;
            meshRenderer.primitive = PrimitiveType::Torus;
            meshRenderer.color = { 0.84f, 0.72f, 0.96f, 1.0f };
            auto& material = entity.AddComponent<MaterialComponent>();
            if (context.initializeDefaultMaterial)
            {
                context.initializeDefaultMaterial(material);
            }
            autoPrimitiveCollider = true;
            autoPrimitiveType = meshRenderer.primitive;
        }

        if (autoPrimitiveCollider && context.ensurePrimitiveCollider)
        {
            context.ensurePrimitiveCollider(entity.GetHandle(), autoPrimitiveType);
        }

        if (parentEntity != entt::null && registry.valid(parentEntity))
        {
            context.scene->SetParent(entity.GetHandle(), parentEntity);
        }

        if (context.selectSingleEntity)
        {
            context.selectSingleEntity(entity.GetHandle());
        }
        return entity.GetHandle();
    }
}
