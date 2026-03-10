#include "Luma/Scene/Entity.h"
#include "Luma/Scene/Components.h"
#include "Luma/Input/Input.h"
#include "Luma/Physics/PhysicsSystem.h"
#include "Luma/Renderer/Camera.h"

using namespace Luma;

class FirstPersonController
{
public:
    float moveSpeed = 5.0f;
    float lookSpeed = 0.1f;
    float jumpHeight = 3.0f;
    float gravity = -9.81f;
    bool isGrounded = false;
    
    Vec3 velocity = Vec3{0, 0, 0};
    Vec2 mouseDelta = Vec2{0, 0};
    
    void OnUpdate(Entity entity, float deltaTime)
    {
        auto& transform = entity.GetComponent<TransformComponent>();
        auto& character = entity.GetComponent<CharacterControllerComponent>();
        
        // Handle mouse look
        mouseDelta.x = Input::GetMouseDelta().x * lookSpeed;
        mouseDelta.y = Input::GetMouseDelta().y * lookSpeed;
        
        // Update rotation (pitch and yaw)
        transform.rotation.y += mouseDelta.x;
        transform.rotation.x += mouseDelta.y;
        
        // Clamp pitch to prevent over-rotation
        transform.rotation.x = std::clamp(transform.rotation.x, -89.0f, 89.0f);
        
        // Handle movement
        Vec3 moveDirection = Vec3{0, 0, 0};
        
        if (Input::IsKeyPressed(KeyCode::W))
            moveDirection.z += 1.0f;
        if (Input::IsKeyPressed(KeyCode::S))
            moveDirection.z -= 1.0f;
        if (Input::IsKeyPressed(KeyCode::A))
            moveDirection.x -= 1.0f;
        if (Input::IsKeyPressed(KeyCode::D))
            moveDirection.x += 1.0f;
        
        // Apply rotation to movement direction
        float yaw = transform.rotation.y * (kPi / 180.0f);
        float cosYaw = std::cos(yaw);
        float sinYaw = std::sin(yaw);
        
        Vec3 rotatedMove;
        rotatedMove.x = moveDirection.x * cosYaw - moveDirection.z * sinYaw;
        rotatedMove.z = moveDirection.x * sinYaw + moveDirection.z * cosYaw;
        rotatedMove.y = moveDirection.y;
        
        // Normalize and apply speed
        if (Length(rotatedMove) > 0.0f)
        {
            rotatedMove = Normalize(rotatedMove) * moveSpeed;
        }
        
        // Handle jumping
        if (Input::IsKeyPressed(KeyCode::Space) && isGrounded)
        {
            velocity.y = std::sqrt(2.0f * jumpHeight * std::abs(gravity));
            isGrounded = false;
        }
        
        // Apply gravity
        velocity.y += gravity * deltaTime;
        
        // Update position
        transform.position += rotatedMove * deltaTime;
        transform.position.y += velocity.y * deltaTime;
        
        // Simple ground collision check
        if (transform.position.y <= 1.8f)
        {
            transform.position.y = 1.8f;
            velocity.y = 0.0f;
            isGrounded = true;
        }
    }
};
