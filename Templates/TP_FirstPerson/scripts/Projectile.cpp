#include "Luma/Scene/Entity.h"
#include "Luma/Scene/Components.h"
#include "Luma/Physics/PhysicsSystem.h"

using namespace Luma;

class Projectile
{
public:
    float speed = 50.0f;
    float damage = 25.0f;
    float lifetime = 3.0f;
    float currentLifetime = 0.0f;
    Vec3 velocity;
    bool hasHit = false;
    
    void OnCreate(Entity entity, const Vec3& position, const Vec3& direction)
    {
        auto& transform = entity.GetComponent<TransformComponent>();
        transform.position = position;
        
        velocity = direction * speed;
        currentLifetime = 0.0f;
        hasHit = false;
        
        // Add visual effect
        auto& renderer = entity.AddComponent<MeshRendererComponent>();
        renderer.mesh = "Sphere";
        renderer.material = "ProjectileMaterial";
        renderer.visible = true;
        
        // Add physics
        auto& collider = entity.AddComponent<ColliderComponent>();
        collider.shape = ColliderShape::Sphere;
        collider.radius = 0.1f;
        collider.isTrigger = true;
    }
    
    void OnUpdate(Entity entity, float deltaTime)
    {
        if (hasHit)
            return;
        
        auto& transform = entity.GetComponent<TransformComponent>();
        
        // Update position
        transform.position += velocity * deltaTime;
        
        // Update lifetime
        currentLifetime += deltaTime;
        if (currentLifetime >= lifetime)
        {
            DestroyProjectile(entity);
            return;
        }
        
        // Check for collisions
        CheckCollisions(entity);
    }
    
    void CheckCollisions(Entity projectile)
    {
        // This would check for collisions with other entities
        // Implementation depends on Luma's physics system
        // For now, we'll just check ground collision
        auto& transform = projectile.GetComponent<TransformComponent>();
        
        if (transform.position.y <= 0.1f)
        {
            OnHit(projectile);
        }
    }
    
    void OnHit(Entity projectile)
    {
        if (hasHit)
            return;
        
        hasHit = true;
        
        // Create impact effect
        CreateImpactEffect(projectile.GetComponent<TransformComponent>().position);
        
        // Apply damage to hit entity (if any)
        // This would require raycasting or collision detection
        
        // Destroy projectile
        DestroyProjectile(projectile);
    }
    
    void DestroyProjectile(Entity entity)
    {
        // Remove entity from scene
        // Implementation depends on Luma's entity management
    }
    
private:
    void CreateImpactEffect(const Vec3& position)
    {
        // Create visual impact effect (particles, decal, etc.)
        // This would create a temporary effect entity
    }
};
