#include "Luma/Scene/Entity.h"
#include "Luma/Scene/Components.h"
#include "Luma/Input/Input.h"
#include "Luma/Physics/PhysicsSystem.h"

using namespace Luma;

class Weapon
{
public:
    std::string weaponType = "Pistol";
    int ammo = 30;
    int maxAmmo = 30;
    float fireRate = 0.2f;
    float damage = 25.0f;
    float lastFireTime = 0.0f;
    bool canFire = true;
    
    void OnUpdate(Entity entity, float deltaTime)
    {
        // Check fire input
        if (Input::IsMouseButtonPressed(MouseButton::Left) && canFire && ammo > 0)
        {
            Fire(entity);
        }
        
        // Update fire rate
        float currentTime = static_cast<float>(glfwGetTime());
        if (currentTime - lastFireTime >= fireRate)
        {
            canFire = true;
        }
        else
        {
            canFire = false;
        }
        
        // Reload
        if (Input::IsKeyPressed(KeyCode::R) && ammo < maxAmmo)
        {
            Reload();
        }
    }
    
    void Fire(Entity owner)
    {
        if (!canFire || ammo <= 0)
            return;
        
        // Create projectile
        auto& transform = owner.GetComponent<TransformComponent>();
        Vec3 firePosition = transform.position + Vec3{0, 0.5f, 0};
        Vec3 fireDirection = GetForwardVector(transform.rotation);
        
        // Spawn projectile entity
        Entity projectile = CreateProjectile(firePosition, fireDirection);
        
        // Update stats
        ammo--;
        lastFireTime = static_cast<float>(glfwGetTime());
        canFire = false;
        
        // Add recoil effect
        auto& ownerTransform = owner.GetComponent<TransformComponent>();
        ownerTransform.rotation.x -= 2.0f; // Upward recoil
        ownerTransform.rotation.y += (rand() % 3 - 1) * 0.5f; // Side recoil
    }
    
    void Reload()
    {
        ammo = maxAmmo;
        // Play reload animation/sound
    }
    
private:
    Entity CreateProjectile(const Vec3& position, const Vec3& direction)
    {
        // This would create a projectile entity with physics
        // Implementation depends on Luma's entity system
        return Entity{};
    }
    
    Vec3 GetForwardVector(const Vec3& rotation)
    {
        float yaw = rotation.y * (kPi / 180.0f);
        float pitch = rotation.x * (kPi / 180.0f);
        
        Vec3 forward;
        forward.x = std::sin(yaw) * std::cos(pitch);
        forward.y = std::sin(pitch);
        forward.z = std::cos(yaw) * std::cos(pitch);
        
        return Normalize(forward);
    }
};
