#include "Luma/Editor/Viewport/EditorViewportDebugOverlay.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_map>

#include "Luma/Renderer/PrimitiveMeshFactory.h"
#include "Luma/Scene/BuoyancyComponent.h"
#include "Luma/Scene/CameraComponent.h"
#include "Luma/Scene/CharacterControllerComponent.h"
#include "Luma/Scene/ColliderComponent.h"
#include "Luma/Scene/D6JointComponent.h"
#include "Luma/Scene/FixedJointComponent.h"
#include "Luma/Scene/ForceFieldComponent.h"
#include "Luma/Scene/HingeJointComponent.h"
#include "Luma/Scene/IDComponent.h"
#include "Luma/Scene/JointComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/PointLightComponent.h"
#include "Luma/Scene/PhysicsEventsComponent.h"
#include "Luma/Scene/RagdollComponent.h"
#include "Luma/Scene/RelationshipComponent.h"
#include "Luma/Scene/RigidBodyComponent.h"
#include "Luma/Scene/SliderJointComponent.h"
#include "Luma/Scene/TransformComponent.h"
#include "Luma/Scene/VehicleComponent.h"
#include "Luma/Scene/WheelColliderComponent.h"

namespace Luma::Editor
{
    namespace
    {
        struct Vec3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        struct Mat4
        {
            std::array<float, 16> elements = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
        };

        struct Vec4
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 1.0f;
        };

        Vec3 operator+(const Vec3& lhs, const Vec3& rhs)
        {
            return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
        }

        Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
        {
            return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
        }

        Vec3 operator*(const Vec3& value, const float scalar)
        {
            return { value.x * scalar, value.y * scalar, value.z * scalar };
        }

        float Dot(const Vec3& lhs, const Vec3& rhs)
        {
            return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
        }

        Vec3 Cross(const Vec3& lhs, const Vec3& rhs)
        {
            return {
                lhs.y * rhs.z - lhs.z * rhs.y,
                lhs.z * rhs.x - lhs.x * rhs.z,
                lhs.x * rhs.y - lhs.y * rhs.x
            };
        }

        Vec3 Normalize(const Vec3& value)
        {
            const float lengthSq = Dot(value, value);
            if (lengthSq <= 1.0e-8f)
            {
                return {};
            }

            const float invLength = 1.0f / std::sqrt(lengthSq);
            return {
                value.x * invLength,
                value.y * invLength,
                value.z * invLength
            };
        }

        Vec4 Multiply(const Mat4& matrix, const Vec4& vector)
        {
            return {
                matrix.elements[0] * vector.x + matrix.elements[4] * vector.y + matrix.elements[8] * vector.z +
                    matrix.elements[12] * vector.w,
                matrix.elements[1] * vector.x + matrix.elements[5] * vector.y + matrix.elements[9] * vector.z +
                    matrix.elements[13] * vector.w,
                matrix.elements[2] * vector.x + matrix.elements[6] * vector.y + matrix.elements[10] * vector.z +
                    matrix.elements[14] * vector.w,
                matrix.elements[3] * vector.x + matrix.elements[7] * vector.y + matrix.elements[11] * vector.z +
                    matrix.elements[15] * vector.w
            };
        }

        Mat4 Multiply(const Mat4& lhs, const Mat4& rhs)
        {
            Mat4 result {};
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    float value = 0.0f;
                    for (int k = 0; k < 4; ++k)
                    {
                        value += lhs.elements[k * 4 + row] * rhs.elements[column * 4 + k];
                    }
                    result.elements[column * 4 + row] = value;
                }
            }
            return result;
        }

        Mat4 BuildPerspective(const float fovRadians, const float aspectRatio, const float nearPlane, const float farPlane)
        {
            Mat4 result {};
            result.elements.fill(0.0f);

            const float tanHalfFov = std::tan(fovRadians * 0.5f);
            if (std::abs(tanHalfFov) <= 1.0e-6f || std::abs(aspectRatio) <= 1.0e-6f)
            {
                result.elements = {
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f
                };
                return result;
            }

            const float f = 1.0f / tanHalfFov;
            result.elements[0] = f / aspectRatio;
            result.elements[5] = f;
            result.elements[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
            result.elements[11] = -1.0f;
            result.elements[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
            return result;
        }

        Mat4 BuildLookAt(const Vec3& eye, const Vec3& center, const Vec3& worldUp)
        {
            const Vec3 forward = Normalize(center - eye);
            const Vec3 right = Normalize(Cross(forward, worldUp));
            const Vec3 up = Cross(right, forward);

            Mat4 result {};
            result.elements = {
                right.x, up.x, -forward.x, 0.0f,
                right.y, up.y, -forward.y, 0.0f,
                right.z, up.z, -forward.z, 0.0f,
                -Dot(right, eye), -Dot(up, eye), Dot(forward, eye), 1.0f
            };
            return result;
        }

        Vec3 RotateByEulerDegrees(const Vec3& point, const std::array<float, 3>& degrees)
        {
            constexpr float kPi = 3.14159265359f;
            const float radiansX = degrees[0] * (kPi / 180.0f);
            const float radiansY = degrees[1] * (kPi / 180.0f);
            const float radiansZ = degrees[2] * (kPi / 180.0f);

            Vec3 p = point;

            const Vec3 rotatedX {
                p.x,
                p.y * std::cos(radiansX) - p.z * std::sin(radiansX),
                p.y * std::sin(radiansX) + p.z * std::cos(radiansX)
            };
            p = rotatedX;

            const Vec3 rotatedY {
                p.x * std::cos(radiansY) + p.z * std::sin(radiansY),
                p.y,
                -p.x * std::sin(radiansY) + p.z * std::cos(radiansY)
            };
            p = rotatedY;

            const Vec3 rotatedZ {
                p.x * std::cos(radiansZ) - p.y * std::sin(radiansZ),
                p.x * std::sin(radiansZ) + p.y * std::cos(radiansZ),
                p.z
            };
            return rotatedZ;
        }
    }

    void EditorViewportDebugOverlay::Draw(const EditorViewportDebugOverlayContext& context)
    {
        if (context.scene == nullptr ||
            context.selectionState == nullptr ||
            context.editorCamera == nullptr ||
            context.drawList == nullptr)
        {
            return;
        }

        auto& registry = context.scene->GetRegistry();

        float fovDegrees = 60.0f;
        float nearPlane = 0.1f;
        float farPlane = 2000.0f;
        Vec3 eye {
            context.editorCamera->position[0],
            context.editorCamera->position[1],
            context.editorCamera->position[2]
        };
        float yawDegrees = context.editorCamera->yaw;
        float pitchDegrees = context.editorCamera->pitch;
        if (context.lensSourceEntity != entt::null &&
            registry.valid(context.lensSourceEntity) &&
            registry.all_of<TransformComponent, CameraComponent>(context.lensSourceEntity))
        {
            const auto& camera = registry.get<CameraComponent>(context.lensSourceEntity);
            const auto& transform = registry.get<TransformComponent>(context.lensSourceEntity);
            fovDegrees = std::clamp(camera.fovDegrees, 10.0f, 170.0f);
            nearPlane = std::max(camera.nearClip, 0.001f);
            farPlane = std::max(camera.farClip, nearPlane + 0.1f);
            eye = { transform.worldPosition[0], transform.worldPosition[1], transform.worldPosition[2] };
            pitchDegrees = transform.worldRotation[0];
            yawDegrees = transform.worldRotation[1];
        }

        constexpr float kPi = 3.14159265359f;
        const float yawRadians = yawDegrees * (kPi / 180.0f);
        const float pitchRadians = pitchDegrees * (kPi / 180.0f);
        const Vec3 forward = Normalize({
            std::cos(yawRadians) * std::cos(pitchRadians),
            std::sin(pitchRadians),
            std::sin(yawRadians) * std::cos(pitchRadians)
        });
        const Mat4 view = BuildLookAt(eye, eye + forward, Vec3 { 0.0f, 1.0f, 0.0f });
        const float aspectRatio = std::max(context.renderAreaSize.x, 1.0f) / std::max(context.renderAreaSize.y, 1.0f);
        const float fovRadians = fovDegrees * (kPi / 180.0f);
        const Mat4 projection = BuildPerspective(fovRadians, aspectRatio, nearPlane, farPlane);
        const Mat4 viewProjection = Multiply(projection, view);

        auto projectWorldToScreen = [&](const Vec3& worldPosition, ImVec2& screenPosition) -> bool
        {
            const Vec4 clip = Multiply(viewProjection, Vec4 { worldPosition.x, worldPosition.y, worldPosition.z, 1.0f });
            if (std::abs(clip.w) <= 1.0e-6f)
            {
                return false;
            }

            const float ndcX = clip.x / clip.w;
            const float ndcY = clip.y / clip.w;
            const float ndcZ = clip.z / clip.w;
            if (ndcZ < -1.0f || ndcZ > 1.0f)
            {
                return false;
            }

            screenPosition.x = context.viewportMin.x + (ndcX * 0.5f + 0.5f) * context.renderAreaSize.x;
            screenPosition.y = context.viewportMin.y + (1.0f - (ndcY * 0.5f + 0.5f)) * context.renderAreaSize.y;
            return std::isfinite(screenPosition.x) && std::isfinite(screenPosition.y);
        };

        auto drawSegment = [&](const Vec3& worldA, const Vec3& worldB, const ImU32 color, const float thickness)
        {
            ImVec2 screenA {};
            ImVec2 screenB {};
            if (!projectWorldToScreen(worldA, screenA) || !projectWorldToScreen(worldB, screenB))
            {
                return;
            }
            context.drawList->AddLine(screenA, screenB, color, thickness);
        };

        auto drawWireCircle = [&](const Vec3& center, const Vec3& axisA, const Vec3& axisB, const float radius, const ImU32 color)
        {
            constexpr float kTwoPi = 6.28318530718f;
            constexpr int kSegments = 24;
            for (int index = 0; index < kSegments; ++index)
            {
                const float t0 = (static_cast<float>(index) / static_cast<float>(kSegments)) * kTwoPi;
                const float t1 = (static_cast<float>(index + 1) / static_cast<float>(kSegments)) * kTwoPi;
                const Vec3 pointA = center + axisA * (std::cos(t0) * radius) + axisB * (std::sin(t0) * radius);
                const Vec3 pointB = center + axisA * (std::cos(t1) * radius) + axisB * (std::sin(t1) * radius);
                drawSegment(pointA, pointB, color, 1.5f);
            }
        };

        auto drawArrow = [&](const Vec3& origin, const Vec3& direction, const float length, const ImU32 color, const float thickness)
        {
            Vec3 dir = Normalize(direction);
            if (Dot(dir, dir) <= 1.0e-6f)
            {
                return;
            }

            const Vec3 end = origin + dir * length;
            drawSegment(origin, end, color, thickness);

            Vec3 tangent = Normalize(Cross(dir, std::abs(dir.y) > 0.95f ? Vec3 { 1.0f, 0.0f, 0.0f } : Vec3 { 0.0f, 1.0f, 0.0f }));
            if (Dot(tangent, tangent) <= 1.0e-6f)
            {
                tangent = { 1.0f, 0.0f, 0.0f };
            }

            const float headLength = std::max(0.06f, length * 0.22f);
            const float headWidth = headLength * 0.50f;
            const Vec3 headBase = end - dir * headLength;
            drawSegment(end, headBase + tangent * headWidth, color, thickness);
            drawSegment(end, headBase - tangent * headWidth, color, thickness);
        };

        auto drawCross = [&](const Vec3& center, const float radius, const ImU32 color, const float thickness)
        {
            drawSegment(center + Vec3 { radius, 0.0f, 0.0f }, center - Vec3 { radius, 0.0f, 0.0f }, color, thickness);
            drawSegment(center + Vec3 { 0.0f, radius, 0.0f }, center - Vec3 { 0.0f, radius, 0.0f }, color, thickness);
            drawSegment(center + Vec3 { 0.0f, 0.0f, radius }, center - Vec3 { 0.0f, 0.0f, radius }, color, thickness);
        };

        auto drawBillboardIcon = [&](const Vec3& worldPosition, void* texture, const float size, const ImU32 fallbackColor)
        {
            ImVec2 screenPosition {};
            if (!projectWorldToScreen(worldPosition, screenPosition))
            {
                return;
            }

            const ImVec2 halfSize(size * 0.5f, size * 0.5f);
            if (texture != nullptr)
            {
                context.drawList->AddImage(
                    reinterpret_cast<ImTextureID>(texture),
                    ImVec2(screenPosition.x - halfSize.x, screenPosition.y - halfSize.y),
                    ImVec2(screenPosition.x + halfSize.x, screenPosition.y + halfSize.y));
            }
            else
            {
                context.drawList->AddCircleFilled(screenPosition, halfSize.x, fallbackColor, 16);
            }
        };

        auto worldPositionOf = [](const TransformComponent& transform) -> Vec3
        {
            return { transform.worldPosition[0], transform.worldPosition[1], transform.worldPosition[2] };
        };

        auto computeAbsScale = [](const TransformComponent& transform) -> std::array<float, 3>
        {
            return {
                std::max(0.001f, std::abs(transform.worldScale[0])),
                std::max(0.001f, std::abs(transform.worldScale[1])),
                std::max(0.001f, std::abs(transform.worldScale[2]))
            };
        };

        auto computeAxes = [&](const TransformComponent& transform, Vec3& axisX, Vec3& axisY, Vec3& axisZ)
        {
            axisX = Normalize(RotateByEulerDegrees(Vec3 { 1.0f, 0.0f, 0.0f }, transform.worldRotation));
            axisY = Normalize(RotateByEulerDegrees(Vec3 { 0.0f, 1.0f, 0.0f }, transform.worldRotation));
            axisZ = Normalize(RotateByEulerDegrees(Vec3 { 0.0f, 0.0f, 1.0f }, transform.worldRotation));
            if (Dot(axisX, axisX) <= 1.0e-6f)
            {
                axisX = { 1.0f, 0.0f, 0.0f };
            }
            if (Dot(axisY, axisY) <= 1.0e-6f)
            {
                axisY = { 0.0f, 1.0f, 0.0f };
            }
            if (Dot(axisZ, axisZ) <= 1.0e-6f)
            {
                axisZ = { 0.0f, 0.0f, 1.0f };
            }
        };

        auto drawWireCapsule = [&](const Vec3& center,
                                   const Vec3& axisX,
                                   const Vec3& axisY,
                                   const Vec3& axisZ,
                                   const float radius,
                                   const float halfHeight,
                                   const ImU32 color,
                                   const float thickness)
        {
            const Vec3 topCenter = center + axisY * halfHeight;
            const Vec3 bottomCenter = center - axisY * halfHeight;
            drawWireCircle(topCenter, axisX, axisZ, radius, color);
            drawWireCircle(bottomCenter, axisX, axisZ, radius, color);
            drawSegment(topCenter + axisX * radius, bottomCenter + axisX * radius, color, thickness);
            drawSegment(topCenter - axisX * radius, bottomCenter - axisX * radius, color, thickness);
            drawSegment(topCenter + axisZ * radius, bottomCenter + axisZ * radius, color, thickness);
            drawSegment(topCenter - axisZ * radius, bottomCenter - axisZ * radius, color, thickness);

            auto drawHemisphereMeridian = [&](const Vec3& capCenter, const Vec3& radialAxis, const bool topCap)
            {
                constexpr int kArcSegments = 18;
                for (int i = 0; i < kArcSegments; ++i)
                {
                    const float t0 = static_cast<float>(i) / static_cast<float>(kArcSegments);
                    const float t1 = static_cast<float>(i + 1) / static_cast<float>(kArcSegments);
                    const float a0 = t0 * kPi;
                    const float a1 = t1 * kPi;

                    const float ySign = topCap ? 1.0f : -1.0f;
                    const Vec3 p0 = capCenter +
                        radialAxis * (std::cos(a0) * radius) +
                        axisY * (ySign * std::sin(a0) * radius);
                    const Vec3 p1 = capCenter +
                        radialAxis * (std::cos(a1) * radius) +
                        axisY * (ySign * std::sin(a1) * radius);
                    drawSegment(p0, p1, color, thickness);
                }
            };

            drawHemisphereMeridian(topCenter, axisX, true);
            drawHemisphereMeridian(topCenter, axisZ, true);
            drawHemisphereMeridian(bottomCenter, axisX, false);
            drawHemisphereMeridian(bottomCenter, axisZ, false);
        };

        auto drawWireCone = [&](const Vec3& center,
                                const Vec3& axisX,
                                const Vec3& axisY,
                                const Vec3& axisZ,
                                const float radius,
                                const float halfHeight,
                                const ImU32 color,
                                const float thickness)
        {
            constexpr float kTwoPi = 6.28318530718f;
            constexpr int kSegments = 20;
            const Vec3 apex = center + axisY * halfHeight;
            const Vec3 baseCenter = center - axisY * halfHeight;
            drawWireCircle(baseCenter, axisX, axisZ, radius, color);

            for (int segment = 0; segment < kSegments; segment += 4)
            {
                const float t = (static_cast<float>(segment) / static_cast<float>(kSegments)) * kTwoPi;
                const Vec3 rimPoint = baseCenter +
                    axisX * (std::cos(t) * radius) +
                    axisZ * (std::sin(t) * radius);
                drawSegment(apex, rimPoint, color, thickness);
            }
        };

        auto drawWireTorus = [&](const Vec3& center,
                                 const Vec3& axisX,
                                 const Vec3& axisY,
                                 const Vec3& axisZ,
                                 const float majorRadius,
                                 const float minorRadius,
                                 const ImU32 color)
        {
            drawWireCircle(center, axisX, axisZ, majorRadius, color);
            drawWireCircle(center + axisX * majorRadius, axisY, axisX, minorRadius, color);
            drawWireCircle(center - axisX * majorRadius, axisY, axisX, minorRadius, color);
            drawWireCircle(center + axisZ * majorRadius, axisY, axisZ, minorRadius, color);
            drawWireCircle(center - axisZ * majorRadius, axisY, axisZ, minorRadius, color);
        };

        auto clampFrustumPreviewDistance = [](const float nearPlane, const float farPlane)
        {
            const float minPreview = std::max(nearPlane + 0.2f, 1.25f);
            const float maxPreview = std::max(minPreview, 3.5f);
            return std::clamp(farPlane, minPreview, maxPreview);
        };

        auto colorForJointMotion = [&](const JointMotionMode mode, const bool selected) -> ImU32
        {
            switch (mode)
            {
            case JointMotionMode::Locked:
                return selected ? IM_COL32(255, 143, 143, 255) : IM_COL32(231, 109, 109, 230);
            case JointMotionMode::Limited:
                return selected ? IM_COL32(255, 232, 138, 255) : IM_COL32(232, 205, 102, 230);
            case JointMotionMode::Free:
                return selected ? IM_COL32(156, 255, 156, 255) : IM_COL32(122, 228, 122, 230);
            default:
                return IM_COL32(200, 200, 200, 220);
            }
        };

        auto isSelected = [&](const EntityID entity)
        {
            return context.selectionState->IsSelected(entity);
        };

        std::unordered_map<UUID, EntityID> entityByUuid;
        const auto idView = registry.view<IDComponent>();
        for (const EntityID entity : idView)
        {
            entityByUuid[idView.get<IDComponent>(entity).id] = entity;
        }

        auto resolveEntityByUUID = [&](const UUID id) -> EntityID
        {
            if (id == 0)
            {
                return entt::null;
            }
            const auto found = entityByUuid.find(id);
            if (found == entityByUuid.end())
            {
                return entt::null;
            }
            return found->second;
        };

        const auto cameraView = registry.view<TransformComponent, CameraComponent>();
        for (const EntityID entity : cameraView)
        {
            const auto& transform = cameraView.get<TransformComponent>(entity);
            const auto& camera = cameraView.get<CameraComponent>(entity);
            if (!camera.active)
            {
                continue;
            }

            const bool selected = isSelected(entity);
            const bool isLensSource = entity == context.lensSourceEntity;
            if (!selected && !isLensSource)
            {
                continue;
            }

            constexpr float kPi = 3.14159265359f;
            const float yawRadians = transform.worldRotation[1] * (kPi / 180.0f);
            const float pitchRadians = transform.worldRotation[0] * (kPi / 180.0f);
            const Vec3 forward = Normalize({
                std::cos(yawRadians) * std::cos(pitchRadians),
                std::sin(pitchRadians),
                std::sin(yawRadians) * std::cos(pitchRadians)
            });
            Vec3 up = { 0.0f, 1.0f, 0.0f };
            Vec3 right = Normalize(Cross(forward, up));
            if (Dot(forward, forward) <= 1.0e-6f)
            {
                continue;
            }
            if (Dot(right, right) <= 1.0e-6f)
            {
                right = { 1.0f, 0.0f, 0.0f };
            }
            up = Normalize(Cross(right, forward));

            const Vec3 origin = worldPositionOf(transform) + forward * 0.18f;
            const ImU32 color = selected || isLensSource
                ? IM_COL32(255, 214, 92, 255)
                : IM_COL32(255, 196, 92, 170);
            const float thickness = selected || isLensSource ? 1.9f : 1.3f;
            const float nearPlane = std::max(camera.nearClip, 0.001f);
            const float farPlane = std::max(camera.farClip, nearPlane + 0.1f);
            const float previewFarPlane = clampFrustumPreviewDistance(nearPlane, farPlane);
            const float aspectRatio = camera.useViewportAspectRatio
                ? (std::max(context.renderAreaSize.x, 1.0f) / std::max(context.renderAreaSize.y, 1.0f))
                : std::max(camera.aspectRatio, 0.001f);

            auto drawQuad = [&](const std::array<Vec3, 4>& quad)
            {
                drawSegment(quad[0], quad[1], color, thickness);
                drawSegment(quad[1], quad[2], color, thickness);
                drawSegment(quad[2], quad[3], color, thickness);
                drawSegment(quad[3], quad[0], color, thickness);
            };

            if (camera.projection == CameraProjectionMode::Orthographic)
            {
                const float halfHeight = std::max(camera.orthographicSize, 0.01f);
                const float halfWidth = halfHeight * aspectRatio;
                const Vec3 nearCenter = origin + forward * nearPlane;
                const Vec3 farCenter = origin + forward * previewFarPlane;

                const std::array<Vec3, 4> nearCorners = {
                    nearCenter + up * halfHeight - right * halfWidth,
                    nearCenter + up * halfHeight + right * halfWidth,
                    nearCenter - up * halfHeight + right * halfWidth,
                    nearCenter - up * halfHeight - right * halfWidth
                };
                const std::array<Vec3, 4> farCorners = {
                    farCenter + up * halfHeight - right * halfWidth,
                    farCenter + up * halfHeight + right * halfWidth,
                    farCenter - up * halfHeight + right * halfWidth,
                    farCenter - up * halfHeight - right * halfWidth
                };

                drawQuad(nearCorners);
                drawQuad(farCorners);
                for (std::size_t index = 0; index < nearCorners.size(); ++index)
                {
                    drawSegment(nearCorners[index], farCorners[index], color, thickness);
                }
            }
            else
            {
                const float fovRadians = std::clamp(camera.fovDegrees, 10.0f, 170.0f) * (kPi / 180.0f);
                const float tanHalfFov = std::tan(fovRadians * 0.5f);
                if (!std::isfinite(tanHalfFov) || tanHalfFov <= 0.0f)
                {
                    continue;
                }

                const float nearHalfHeight = tanHalfFov * nearPlane;
                const float nearHalfWidth = nearHalfHeight * aspectRatio;
                const float farHalfHeight = tanHalfFov * previewFarPlane;
                const float farHalfWidth = farHalfHeight * aspectRatio;

                const Vec3 nearCenter = origin + forward * nearPlane;
                const Vec3 farCenter = origin + forward * previewFarPlane;

                const std::array<Vec3, 4> nearCorners = {
                    nearCenter + up * nearHalfHeight - right * nearHalfWidth,
                    nearCenter + up * nearHalfHeight + right * nearHalfWidth,
                    nearCenter - up * nearHalfHeight + right * nearHalfWidth,
                    nearCenter - up * nearHalfHeight - right * nearHalfWidth
                };
                const std::array<Vec3, 4> farCorners = {
                    farCenter + up * farHalfHeight - right * farHalfWidth,
                    farCenter + up * farHalfHeight + right * farHalfWidth,
                    farCenter - up * farHalfHeight + right * farHalfWidth,
                    farCenter - up * farHalfHeight - right * farHalfWidth
                };

                for (const Vec3& corner : farCorners)
                {
                    drawSegment(origin, corner, color, thickness);
                }
                drawQuad(nearCorners);
                drawQuad(farCorners);
                for (std::size_t index = 0; index < nearCorners.size(); ++index)
                {
                    drawSegment(nearCorners[index], farCorners[index], color, thickness);
                }
            }
        }

        const auto pointLightView = registry.view<TransformComponent, PointLightComponent>();
        for (const EntityID entity : pointLightView)
        {
            const auto& transform = pointLightView.get<TransformComponent>(entity);
            const auto& pointLight = pointLightView.get<PointLightComponent>(entity);
            if (!pointLight.active)
            {
                continue;
            }

            const bool selected = isSelected(entity);
            const Vec3 center = worldPositionOf(transform);
            const ImU32 color = selected ? IM_COL32(255, 226, 138, 255) : IM_COL32(255, 210, 112, 220);
            const float iconSize = selected ? 22.0f : 18.0f;
            drawBillboardIcon(center, context.pointLightIconTexture, iconSize, color);
            drawCross(center, selected ? 0.16f : 0.11f, color, selected ? 1.8f : 1.3f);

            if (!selected)
            {
                continue;
            }

            const float radius = std::max(pointLight.range, 0.05f);
            drawWireCircle(center, Vec3 { 1.0f, 0.0f, 0.0f }, Vec3 { 0.0f, 1.0f, 0.0f }, radius, color);
            drawWireCircle(center, Vec3 { 1.0f, 0.0f, 0.0f }, Vec3 { 0.0f, 0.0f, 1.0f }, radius, color);
            drawWireCircle(center, Vec3 { 0.0f, 1.0f, 0.0f }, Vec3 { 0.0f, 0.0f, 1.0f }, radius, color);
        }

        const auto colliderView = registry.view<TransformComponent, ColliderComponent>();
        for (const EntityID entity : colliderView)
        {
            const auto& transform = colliderView.get<TransformComponent>(entity);
            const auto& collider = colliderView.get<ColliderComponent>(entity);
            if (!collider.active)
            {
                continue;
            }

            const std::array<float, 3> absScale {
                std::max(0.001f, std::abs(transform.worldScale[0])),
                std::max(0.001f, std::abs(transform.worldScale[1])),
                std::max(0.001f, std::abs(transform.worldScale[2]))
            };

            const Vec3 worldPosition {
                transform.worldPosition[0],
                transform.worldPosition[1],
                transform.worldPosition[2]
            };
            const Vec3 scaledCenterOffset {
                collider.center[0] * absScale[0],
                collider.center[1] * absScale[1],
                collider.center[2] * absScale[2]
            };
            const Vec3 center = worldPosition + RotateByEulerDegrees(scaledCenterOffset, transform.worldRotation);

            Vec3 axisX = Normalize(RotateByEulerDegrees(Vec3 { 1.0f, 0.0f, 0.0f }, transform.worldRotation));
            Vec3 axisY = Normalize(RotateByEulerDegrees(Vec3 { 0.0f, 1.0f, 0.0f }, transform.worldRotation));
            Vec3 axisZ = Normalize(RotateByEulerDegrees(Vec3 { 0.0f, 0.0f, 1.0f }, transform.worldRotation));
            if (Dot(axisX, axisX) <= 1.0e-6f)
            {
                axisX = { 1.0f, 0.0f, 0.0f };
            }
            if (Dot(axisY, axisY) <= 1.0e-6f)
            {
                axisY = { 0.0f, 1.0f, 0.0f };
            }
            if (Dot(axisZ, axisZ) <= 1.0e-6f)
            {
                axisZ = { 0.0f, 0.0f, 1.0f };
            }

            const bool selected = isSelected(entity);
            const ImU32 color = collider.isTrigger
                ? (selected ? IM_COL32(255, 230, 148, 255) : IM_COL32(255, 181, 90, 230))
                : (selected ? IM_COL32(122, 248, 255, 255) : IM_COL32(84, 208, 255, 220));
            const MeshRendererComponent* meshRenderer = registry.try_get<MeshRendererComponent>(entity);

            const bool meshAsSphere =
                collider.shape == ColliderShapeType::Mesh &&
                meshRenderer != nullptr &&
                meshRenderer->usePrimitive &&
                meshRenderer->primitive == PrimitiveType::Sphere;
            const bool meshAsCapsule =
                collider.shape == ColliderShapeType::Mesh &&
                meshRenderer != nullptr &&
                meshRenderer->usePrimitive &&
                meshRenderer->primitive == PrimitiveType::Capsule;
            const bool meshAsCylinder =
                collider.shape == ColliderShapeType::Mesh &&
                meshRenderer != nullptr &&
                meshRenderer->usePrimitive &&
                meshRenderer->primitive == PrimitiveType::Cylinder;
            const bool meshAsCone =
                collider.shape == ColliderShapeType::Mesh &&
                meshRenderer != nullptr &&
                meshRenderer->usePrimitive &&
                meshRenderer->primitive == PrimitiveType::Cone;
            const bool meshAsTorus =
                collider.shape == ColliderShapeType::Mesh &&
                meshRenderer != nullptr &&
                meshRenderer->usePrimitive &&
                meshRenderer->primitive == PrimitiveType::Torus;
            const bool drawAsCapsule = collider.shape == ColliderShapeType::Capsule || meshAsCapsule;
            const bool drawAsCylinder = collider.shape == ColliderShapeType::Cylinder || meshAsCylinder;
            const bool drawAsCone = meshAsCone;
            const bool drawAsTorus = meshAsTorus;
            const bool drawAsBox = collider.shape == ColliderShapeType::Box ||
                (collider.shape == ColliderShapeType::Mesh &&
                    !meshAsSphere &&
                    !meshAsCapsule &&
                    !meshAsCylinder &&
                    !meshAsCone &&
                    !meshAsTorus);
            const bool drawAsSphere = collider.shape == ColliderShapeType::Sphere || meshAsSphere;

            if (drawAsBox)
            {
                Vec3 extents {
                    std::max(0.001f, collider.boxHalfExtents[0] * absScale[0]),
                    std::max(0.001f, collider.boxHalfExtents[1] * absScale[1]),
                    std::max(0.001f, collider.boxHalfExtents[2] * absScale[2])
                };
                if (collider.shape == ColliderShapeType::Mesh && meshRenderer != nullptr && meshRenderer->usePrimitive)
                {
                    if (meshRenderer->primitive == PrimitiveType::Cube)
                    {
                        extents = { 0.5f * absScale[0], 0.5f * absScale[1], 0.5f * absScale[2] };
                    }
                    else if (meshRenderer->primitive == PrimitiveType::Plane)
                    {
                        extents = {
                            0.5f * absScale[0],
                            std::max(0.02f, 0.02f * absScale[1]),
                            0.5f * absScale[2]
                        };
                    }
                    else
                    {
                        const PrimitiveMeshData& primitiveMesh = PrimitiveMeshFactory::GetPrimitive(meshRenderer->primitive);
                        if (!primitiveMesh.vertices.empty())
                        {
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
                            for (const PrimitiveVertex& vertex : primitiveMesh.vertices)
                            {
                                minBounds[0] = std::min(minBounds[0], vertex.position[0]);
                                minBounds[1] = std::min(minBounds[1], vertex.position[1]);
                                minBounds[2] = std::min(minBounds[2], vertex.position[2]);
                                maxBounds[0] = std::max(maxBounds[0], vertex.position[0]);
                                maxBounds[1] = std::max(maxBounds[1], vertex.position[1]);
                                maxBounds[2] = std::max(maxBounds[2], vertex.position[2]);
                            }

                            extents = {
                                std::max(0.001f, (maxBounds[0] - minBounds[0]) * 0.5f * absScale[0]),
                                std::max(0.001f, (maxBounds[1] - minBounds[1]) * 0.5f * absScale[1]),
                                std::max(0.001f, (maxBounds[2] - minBounds[2]) * 0.5f * absScale[2])
                            };
                        }
                    }
                }

                constexpr std::array<std::array<int, 2>, 12> kEdges = {
                    std::array<int, 2> { 0, 1 }, { 0, 2 }, { 0, 4 }, { 1, 3 },
                    { 1, 5 }, { 2, 3 }, { 2, 6 }, { 3, 7 },
                    { 4, 5 }, { 4, 6 }, { 5, 7 }, { 6, 7 }
                };
                std::array<Vec3, 8> corners {};
                int cornerIndex = 0;
                for (int sx = -1; sx <= 1; sx += 2)
                {
                    for (int sy = -1; sy <= 1; sy += 2)
                    {
                        for (int sz = -1; sz <= 1; sz += 2)
                        {
                            const Vec3 localCorner {
                                extents.x * static_cast<float>(sx),
                                extents.y * static_cast<float>(sy),
                                extents.z * static_cast<float>(sz)
                            };
                            corners[static_cast<std::size_t>(cornerIndex)] =
                                center + RotateByEulerDegrees(localCorner, transform.worldRotation);
                            ++cornerIndex;
                        }
                    }
                }
                for (const auto& edge : kEdges)
                {
                    drawSegment(corners[static_cast<std::size_t>(edge[0])], corners[static_cast<std::size_t>(edge[1])], color, selected ? 1.8f : 1.4f);
                }
            }
            else if (drawAsSphere)
            {
                float radius = std::max(0.001f, collider.sphereRadius * std::max(absScale[0], std::max(absScale[1], absScale[2])));
                if (meshAsSphere)
                {
                    radius = 0.5f * std::max(absScale[0], std::max(absScale[1], absScale[2]));
                }
                drawWireCircle(center, axisX, axisY, radius, color);
                drawWireCircle(center, axisX, axisZ, radius, color);
                drawWireCircle(center, axisY, axisZ, radius, color);
            }
            else if (drawAsCapsule)
            {
                float radius = std::max(0.001f, collider.capsuleRadius * std::max(absScale[0], absScale[2]));
                float halfHeight = std::max(0.001f, collider.capsuleHalfHeight * absScale[1]);
                if (meshAsCapsule)
                {
                    radius = 0.5f * std::max(absScale[0], absScale[2]);
                    halfHeight = std::max(0.001f, 0.5f * absScale[1] - radius);
                }
                drawWireCapsule(center, axisX, axisY, axisZ, radius, halfHeight, color, selected ? 1.8f : 1.4f);
            }
            else if (drawAsCylinder)
            {
                float radius = std::max(0.001f, collider.capsuleRadius * std::max(absScale[0], absScale[2]));
                float halfHeight = std::max(0.001f, collider.capsuleHalfHeight * absScale[1]);
                if (meshAsCylinder)
                {
                    radius = 0.5f * std::max(absScale[0], absScale[2]);
                    halfHeight = 0.5f * absScale[1];
                }

                const Vec3 topCenter = center + axisY * halfHeight;
                const Vec3 bottomCenter = center - axisY * halfHeight;
                drawWireCircle(topCenter, axisX, axisZ, radius, color);
                drawWireCircle(bottomCenter, axisX, axisZ, radius, color);
                drawSegment(topCenter + axisX * radius, bottomCenter + axisX * radius, color, selected ? 1.8f : 1.4f);
                drawSegment(topCenter - axisX * radius, bottomCenter - axisX * radius, color, selected ? 1.8f : 1.4f);
                drawSegment(topCenter + axisZ * radius, bottomCenter + axisZ * radius, color, selected ? 1.8f : 1.4f);
                drawSegment(topCenter - axisZ * radius, bottomCenter - axisZ * radius, color, selected ? 1.8f : 1.4f);
            }
            else if (drawAsCone)
            {
                const float radius = 0.5f * std::max(absScale[0], absScale[2]);
                const float halfHeight = 0.5f * absScale[1];
                drawWireCone(center, axisX, axisY, axisZ, radius, halfHeight, color, selected ? 1.8f : 1.4f);
            }
            else if (drawAsTorus)
            {
                const float majorRadius = 0.38f * std::max(absScale[0], absScale[2]);
                const float minorRadius = 0.14f * std::max(absScale[0], std::max(absScale[1], absScale[2]));
                drawWireTorus(center, axisX, axisY, axisZ, majorRadius, minorRadius, color);
            }
        }

        const auto rigidBodyView = registry.view<TransformComponent, RigidBodyComponent>();
        for (const EntityID entity : rigidBodyView)
        {
            const auto& transform = rigidBodyView.get<TransformComponent>(entity);
            const auto& rigidBody = rigidBodyView.get<RigidBodyComponent>(entity);
            if (!rigidBody.active)
            {
                continue;
            }

            const bool selected = isSelected(entity);
            const ImU32 color = selected ? IM_COL32(154, 245, 172, 255) : IM_COL32(118, 212, 140, 232);
            const Vec3 center = worldPositionOf(transform);
            const Vec3 linearVelocity {
                rigidBody.linearVelocity[0],
                rigidBody.linearVelocity[1],
                rigidBody.linearVelocity[2]
            };
            if (Dot(linearVelocity, linearVelocity) > 1.0e-5f)
            {
                drawArrow(center, linearVelocity, std::clamp(std::sqrt(Dot(linearVelocity, linearVelocity)) * 0.15f, 0.35f, 2.5f), color, selected ? 1.8f : 1.4f);
            }
            else
            {
                drawCross(center, 0.05f, color, selected ? 1.7f : 1.3f);
            }
        }

        const auto characterView = registry.view<TransformComponent, CharacterControllerComponent>();
        for (const EntityID entity : characterView)
        {
            const auto& transform = characterView.get<TransformComponent>(entity);
            const auto& controller = characterView.get<CharacterControllerComponent>(entity);
            if (!controller.active)
            {
                continue;
            }

            const auto absScale = computeAbsScale(transform);
            Vec3 axisX {};
            Vec3 axisY {};
            Vec3 axisZ {};
            computeAxes(transform, axisX, axisY, axisZ);
            const Vec3 center = worldPositionOf(transform);
            const bool selected = isSelected(entity);
            const float radius = std::max(0.02f, controller.radius * std::max(absScale[0], absScale[2]));
            const float halfBody = std::max(0.05f, (controller.height * absScale[1] * 0.5f) - radius);
            const ImU32 color = controller.isGrounded
                ? (selected ? IM_COL32(121, 255, 148, 255) : IM_COL32(82, 224, 124, 230))
                : (selected ? IM_COL32(153, 225, 255, 255) : IM_COL32(108, 196, 240, 220));
            drawWireCapsule(center, axisX, axisY, axisZ, radius, halfBody, color, selected ? 1.8f : 1.4f);
            drawArrow(center, axisY, std::max(0.2f, controller.stepOffset), color, selected ? 1.8f : 1.4f);
        }

        const auto wheelView = registry.view<TransformComponent, WheelColliderComponent>();
        for (const EntityID entity : wheelView)
        {
            const auto& transform = wheelView.get<TransformComponent>(entity);
            const auto& wheel = wheelView.get<WheelColliderComponent>(entity);
            if (!wheel.active)
            {
                continue;
            }

            Vec3 axisX {};
            Vec3 axisY {};
            Vec3 axisZ {};
            computeAxes(transform, axisX, axisY, axisZ);
            const auto absScale = computeAbsScale(transform);
            const Vec3 center = worldPositionOf(transform);
            const float radius = std::max(0.02f, wheel.radius * std::max(absScale[1], absScale[2]));
            const float halfWidth = std::max(0.01f, wheel.width * absScale[0] * 0.5f);
            const Vec3 leftCenter = center - axisX * halfWidth;
            const Vec3 rightCenter = center + axisX * halfWidth;
            const bool selected = isSelected(entity);
            const ImU32 color = selected ? IM_COL32(255, 206, 128, 255) : IM_COL32(255, 172, 96, 230);
            drawWireCircle(leftCenter, axisY, axisZ, radius, color);
            drawWireCircle(rightCenter, axisY, axisZ, radius, color);
            drawSegment(leftCenter + axisY * radius, rightCenter + axisY * radius, color, selected ? 1.8f : 1.4f);
            drawSegment(leftCenter - axisY * radius, rightCenter - axisY * radius, color, selected ? 1.8f : 1.4f);
            drawArrow(center, axisY, std::max(0.05f, wheel.suspensionTravel), color, selected ? 1.8f : 1.4f);
        }

        const auto jointView = registry.view<TransformComponent, JointComponent>();
        for (const EntityID entity : jointView)
        {
            const auto& transform = jointView.get<TransformComponent>(entity);
            const auto& joint = jointView.get<JointComponent>(entity);
            if (!joint.active)
            {
                continue;
            }

            const Vec3 anchor = worldPositionOf(transform);
            const bool selected = isSelected(entity);
            const ImU32 color = selected ? IM_COL32(255, 180, 255, 255) : IM_COL32(205, 128, 255, 228);
            drawCross(anchor, 0.06f, color, selected ? 1.8f : 1.4f);
            const EntityID entityA = resolveEntityByUUID(joint.connectedBodyA);
            const EntityID entityB = resolveEntityByUUID(joint.connectedBodyB);
            const TransformComponent* transformA =
                (entityA != entt::null && registry.valid(entityA)) ? registry.try_get<TransformComponent>(entityA) : nullptr;
            const TransformComponent* transformB =
                (entityB != entt::null && registry.valid(entityB)) ? registry.try_get<TransformComponent>(entityB) : nullptr;
            if (transformA != nullptr)
            {
                drawSegment(anchor, worldPositionOf(*transformA), color, selected ? 1.7f : 1.3f);
            }
            if (transformB != nullptr)
            {
                drawSegment(anchor, worldPositionOf(*transformB), color, selected ? 1.7f : 1.3f);
            }
            if (transformA != nullptr && transformB != nullptr)
            {
                drawSegment(
                    worldPositionOf(*transformA),
                    worldPositionOf(*transformB),
                    IM_COL32(188, 128, 252, 180),
                    selected ? 1.4f : 1.2f);
            }
        }

        const auto fixedJointView = registry.view<TransformComponent, FixedJointComponent>();
        for (const EntityID entity : fixedJointView)
        {
            const JointComponent* joint = registry.try_get<JointComponent>(entity);
            if (joint != nullptr && !joint->active)
            {
                continue;
            }

            const bool selected = isSelected(entity);
            const Vec3 center = worldPositionOf(fixedJointView.get<TransformComponent>(entity));
            const ImU32 color = selected ? IM_COL32(255, 209, 138, 255) : IM_COL32(255, 186, 116, 228);
            drawCross(center, 0.12f, color, selected ? 1.9f : 1.5f);
        }

        const auto hingeView = registry.view<TransformComponent, HingeJointComponent>();
        for (const EntityID entity : hingeView)
        {
            const JointComponent* joint = registry.try_get<JointComponent>(entity);
            if (joint != nullptr && !joint->active)
            {
                continue;
            }

            const auto& transform = hingeView.get<TransformComponent>(entity);
            const auto& hinge = hingeView.get<HingeJointComponent>(entity);
            Vec3 axisX {};
            Vec3 axisY {};
            Vec3 axisZ {};
            computeAxes(transform, axisX, axisY, axisZ);
            const Vec3 center = worldPositionOf(transform);
            const Vec3 axis = Normalize(RotateByEulerDegrees(
                Vec3 { hinge.axis[0], hinge.axis[1], hinge.axis[2] },
                transform.worldRotation));
            const bool selected = isSelected(entity);
            const ImU32 color = selected ? IM_COL32(255, 166, 138, 255) : IM_COL32(232, 132, 102, 230);
            drawArrow(center, Dot(axis, axis) > 1.0e-6f ? axis : axisX, 0.80f, color, selected ? 1.9f : 1.5f);

            if (hinge.enableLimits)
            {
                const Vec3 hingeAxis = Dot(axis, axis) > 1.0e-6f ? axis : axisX;
                Vec3 basisA = Normalize(Cross(
                    hingeAxis,
                    std::abs(hingeAxis.y) > 0.95f ? Vec3 { 1.0f, 0.0f, 0.0f } : Vec3 { 0.0f, 1.0f, 0.0f }));
                if (Dot(basisA, basisA) <= 1.0e-6f)
                {
                    basisA = axisZ;
                }
                Vec3 basisB = Normalize(Cross(hingeAxis, basisA));
                if (Dot(basisB, basisB) <= 1.0e-6f)
                {
                    basisB = axisY;
                }

                const float startRadians = hinge.lowerLimitDegrees * (kPi / 180.0f);
                const float endRadians = hinge.upperLimitDegrees * (kPi / 180.0f);
                const int segments = 20;
                const float arcRadius = 0.42f;
                for (int i = 0; i < segments; ++i)
                {
                    const float t0 = static_cast<float>(i) / static_cast<float>(segments);
                    const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments);
                    const float a0 = startRadians + (endRadians - startRadians) * t0;
                    const float a1 = startRadians + (endRadians - startRadians) * t1;
                    const Vec3 p0 = center + basisA * (std::cos(a0) * arcRadius) + basisB * (std::sin(a0) * arcRadius);
                    const Vec3 p1 = center + basisA * (std::cos(a1) * arcRadius) + basisB * (std::sin(a1) * arcRadius);
                    drawSegment(p0, p1, color, selected ? 1.8f : 1.4f);
                }
            }
        }

        const auto sliderView = registry.view<TransformComponent, SliderJointComponent>();
        for (const EntityID entity : sliderView)
        {
            const JointComponent* joint = registry.try_get<JointComponent>(entity);
            if (joint != nullptr && !joint->active)
            {
                continue;
            }

            const auto& transform = sliderView.get<TransformComponent>(entity);
            const auto& slider = sliderView.get<SliderJointComponent>(entity);
            const Vec3 center = worldPositionOf(transform);
            Vec3 axis = Normalize(RotateByEulerDegrees(
                Vec3 { slider.axis[0], slider.axis[1], slider.axis[2] },
                transform.worldRotation));
            if (Dot(axis, axis) <= 1.0e-6f)
            {
                axis = { 1.0f, 0.0f, 0.0f };
            }
            const bool selected = isSelected(entity);
            const ImU32 color = selected ? IM_COL32(178, 255, 178, 255) : IM_COL32(134, 228, 134, 232);
            drawArrow(center, axis, 0.70f, color, selected ? 1.9f : 1.5f);
            if (slider.enableLimits)
            {
                const Vec3 minPoint = center + axis * slider.lowerLimit;
                const Vec3 maxPoint = center + axis * slider.upperLimit;
                drawSegment(minPoint, maxPoint, color, selected ? 1.8f : 1.4f);
                drawCross(minPoint, 0.05f, color, selected ? 1.7f : 1.3f);
                drawCross(maxPoint, 0.05f, color, selected ? 1.7f : 1.3f);
            }
        }

        const auto d6View = registry.view<TransformComponent, D6JointComponent>();
        for (const EntityID entity : d6View)
        {
            const JointComponent* joint = registry.try_get<JointComponent>(entity);
            if (joint != nullptr && !joint->active)
            {
                continue;
            }

            const auto& transform = d6View.get<TransformComponent>(entity);
            const auto& d6 = d6View.get<D6JointComponent>(entity);
            Vec3 axisX {};
            Vec3 axisY {};
            Vec3 axisZ {};
            computeAxes(transform, axisX, axisY, axisZ);
            const Vec3 center = worldPositionOf(transform);
            const float axisLength = std::clamp(std::max(d6.linearLimit, 0.2f) * 0.35f, 0.35f, 1.2f);
            const bool selected = isSelected(entity);
            drawSegment(center - axisX * axisLength, center + axisX * axisLength, colorForJointMotion(d6.linearMotion[0], selected), selected ? 1.9f : 1.5f);
            drawSegment(center - axisY * axisLength, center + axisY * axisLength, colorForJointMotion(d6.linearMotion[1], selected), selected ? 1.9f : 1.5f);
            drawSegment(center - axisZ * axisLength, center + axisZ * axisLength, colorForJointMotion(d6.linearMotion[2], selected), selected ? 1.9f : 1.5f);
        }

        const auto fieldView = registry.view<TransformComponent, ForceFieldComponent>();
        for (const EntityID entity : fieldView)
        {
            const auto& transform = fieldView.get<TransformComponent>(entity);
            const auto& field = fieldView.get<ForceFieldComponent>(entity);
            if (!field.active)
            {
                continue;
            }

            Vec3 axisX {};
            Vec3 axisY {};
            Vec3 axisZ {};
            computeAxes(transform, axisX, axisY, axisZ);
            const auto absScale = computeAbsScale(transform);
            const Vec3 center = worldPositionOf(transform) + RotateByEulerDegrees(
                Vec3 { field.center[0] * absScale[0], field.center[1] * absScale[1], field.center[2] * absScale[2] },
                transform.worldRotation);
            const bool selected = isSelected(entity);
            const ImU32 color = selected ? IM_COL32(112, 255, 232, 255) : IM_COL32(86, 218, 196, 230);

            if (field.shape == ForceFieldShape::Box)
            {
                const Vec3 extents {
                    std::max(0.001f, field.boxHalfExtents[0] * absScale[0]),
                    std::max(0.001f, field.boxHalfExtents[1] * absScale[1]),
                    std::max(0.001f, field.boxHalfExtents[2] * absScale[2])
                };
                constexpr std::array<std::array<int, 2>, 12> kEdges = {
                    std::array<int, 2> { 0, 1 }, { 0, 2 }, { 0, 4 }, { 1, 3 },
                    { 1, 5 }, { 2, 3 }, { 2, 6 }, { 3, 7 },
                    { 4, 5 }, { 4, 6 }, { 5, 7 }, { 6, 7 }
                };
                std::array<Vec3, 8> corners {};
                int cornerIndex = 0;
                for (int sx = -1; sx <= 1; sx += 2)
                {
                    for (int sy = -1; sy <= 1; sy += 2)
                    {
                        for (int sz = -1; sz <= 1; sz += 2)
                        {
                            corners[static_cast<std::size_t>(cornerIndex)] = center +
                                axisX * (extents.x * static_cast<float>(sx)) +
                                axisY * (extents.y * static_cast<float>(sy)) +
                                axisZ * (extents.z * static_cast<float>(sz));
                            ++cornerIndex;
                        }
                    }
                }
                for (const auto& edge : kEdges)
                {
                    drawSegment(corners[static_cast<std::size_t>(edge[0])], corners[static_cast<std::size_t>(edge[1])], color, selected ? 1.8f : 1.4f);
                }
            }
            else if (field.shape == ForceFieldShape::Sphere)
            {
                const float radius = std::max(0.001f, field.sphereRadius * std::max(absScale[0], std::max(absScale[1], absScale[2])));
                drawWireCircle(center, axisX, axisY, radius, color);
                drawWireCircle(center, axisX, axisZ, radius, color);
                drawWireCircle(center, axisY, axisZ, radius, color);
            }
            else
            {
                const float radius = std::max(0.001f, field.capsuleRadius * std::max(absScale[0], absScale[2]));
                const float halfHeight = std::max(0.001f, field.capsuleHalfHeight * absScale[1]);
                drawWireCapsule(center, axisX, axisY, axisZ, radius, halfHeight, color, selected ? 1.8f : 1.4f);
            }

            if (field.type == ForceFieldType::Radial)
            {
                drawArrow(center, axisX, 0.45f, color, selected ? 1.7f : 1.3f);
                drawArrow(center, axisX * -1.0f, 0.45f, color, selected ? 1.7f : 1.3f);
                drawArrow(center, axisY, 0.45f, color, selected ? 1.7f : 1.3f);
                drawArrow(center, axisY * -1.0f, 0.45f, color, selected ? 1.7f : 1.3f);
                drawArrow(center, axisZ, 0.45f, color, selected ? 1.7f : 1.3f);
                drawArrow(center, axisZ * -1.0f, 0.45f, color, selected ? 1.7f : 1.3f);
            }
            else
            {
                Vec3 direction = Normalize(RotateByEulerDegrees(
                    Vec3 { field.direction[0], field.direction[1], field.direction[2] },
                    transform.worldRotation));
                if (Dot(direction, direction) <= 1.0e-6f)
                {
                    direction = axisY;
                }
                const float arrowLength = std::clamp(std::abs(field.strength) * 0.08f, 0.45f, 3.0f);
                drawArrow(center, direction, arrowLength, color, selected ? 1.8f : 1.4f);
            }
        }

        const auto buoyancyView = registry.view<TransformComponent, BuoyancyComponent>();
        for (const EntityID entity : buoyancyView)
        {
            const auto& transform = buoyancyView.get<TransformComponent>(entity);
            const auto& buoyancy = buoyancyView.get<BuoyancyComponent>(entity);
            if (!buoyancy.active)
            {
                continue;
            }

            const bool selected = isSelected(entity);
            const ImU32 color = selected ? IM_COL32(140, 230, 255, 255) : IM_COL32(108, 192, 236, 230);
            const Vec3 origin = worldPositionOf(transform);
            const auto absScale = computeAbsScale(transform);

            float maxXZ = 0.75f;
            for (const auto& point : buoyancy.floatPoints)
            {
                maxXZ = std::max(maxXZ, std::abs(point[0]) * absScale[0]);
                maxXZ = std::max(maxXZ, std::abs(point[2]) * absScale[2]);
            }
            maxXZ += 0.35f;
            const float waterY = buoyancy.waterLevel;
            const Vec3 p0 { origin.x - maxXZ, waterY, origin.z - maxXZ };
            const Vec3 p1 { origin.x + maxXZ, waterY, origin.z - maxXZ };
            const Vec3 p2 { origin.x + maxXZ, waterY, origin.z + maxXZ };
            const Vec3 p3 { origin.x - maxXZ, waterY, origin.z + maxXZ };
            drawSegment(p0, p1, color, selected ? 1.8f : 1.4f);
            drawSegment(p1, p2, color, selected ? 1.8f : 1.4f);
            drawSegment(p2, p3, color, selected ? 1.8f : 1.4f);
            drawSegment(p3, p0, color, selected ? 1.8f : 1.4f);

            for (const auto& localPoint : buoyancy.floatPoints)
            {
                const Vec3 scaled {
                    localPoint[0] * absScale[0],
                    localPoint[1] * absScale[1],
                    localPoint[2] * absScale[2]
                };
                const Vec3 worldPoint = origin + RotateByEulerDegrees(scaled, transform.worldRotation);
                drawCross(worldPoint, 0.04f, color, selected ? 1.8f : 1.4f);
                drawSegment(
                    worldPoint,
                    Vec3 { worldPoint.x, waterY, worldPoint.z },
                    IM_COL32(128, 188, 220, selected ? 250 : 210),
                    selected ? 1.5f : 1.2f);
            }
        }

        const auto vehicleView = registry.view<TransformComponent, VehicleComponent>();
        for (const EntityID entity : vehicleView)
        {
            const auto& transform = vehicleView.get<TransformComponent>(entity);
            const auto& vehicle = vehicleView.get<VehicleComponent>(entity);
            if (!vehicle.active)
            {
                continue;
            }

            const bool selected = isSelected(entity);
            const ImU32 color = selected ? IM_COL32(255, 168, 244, 255) : IM_COL32(224, 126, 208, 228);
            const Vec3 center = worldPositionOf(transform);
            drawCross(center, 0.10f, color, selected ? 1.9f : 1.5f);

            const EntityID chassisEntity = resolveEntityByUUID(vehicle.chassisRigidBody);
            if (chassisEntity != entt::null && registry.valid(chassisEntity))
            {
                if (const TransformComponent* chassisTransform = registry.try_get<TransformComponent>(chassisEntity))
                {
                    drawSegment(center, worldPositionOf(*chassisTransform), color, selected ? 1.8f : 1.4f);
                }
            }

            for (const UUID wheelId : vehicle.wheelEntities)
            {
                const EntityID wheelEntity = resolveEntityByUUID(wheelId);
                if (wheelEntity == entt::null || !registry.valid(wheelEntity))
                {
                    continue;
                }
                if (const TransformComponent* wheelTransform = registry.try_get<TransformComponent>(wheelEntity))
                {
                    drawSegment(center, worldPositionOf(*wheelTransform), color, selected ? 1.7f : 1.3f);
                }
            }
        }

        const auto eventsView = registry.view<TransformComponent, PhysicsEventsComponent>();
        for (const EntityID entity : eventsView)
        {
            const auto& transform = eventsView.get<TransformComponent>(entity);
            const auto& events = eventsView.get<PhysicsEventsComponent>(entity);
            const bool anyEnabled =
                events.onCollisionEnter || events.onCollisionStay || events.onCollisionExit ||
                events.onTriggerEnter || events.onTriggerStay || events.onTriggerExit;
            if (!anyEnabled)
            {
                continue;
            }

            Vec3 axisX = { 1.0f, 0.0f, 0.0f };
            Vec3 axisY = { 0.0f, 1.0f, 0.0f };
            Vec3 axisZ = { 0.0f, 0.0f, 1.0f };
            computeAxes(transform, axisX, axisY, axisZ);

            const bool selected = isSelected(entity);
            const ImU32 color = selected ? IM_COL32(255, 245, 148, 255) : IM_COL32(235, 216, 102, 225);
            const Vec3 center = worldPositionOf(transform);
            drawCross(center, 0.08f, color, selected ? 1.8f : 1.4f);
            drawWireCircle(center, axisX, axisZ, 0.18f, color);
        }

        const auto ragdollView = registry.view<TransformComponent, RagdollComponent>();
        for (const EntityID entity : ragdollView)
        {
            const auto& transform = ragdollView.get<TransformComponent>(entity);
            const auto& ragdoll = ragdollView.get<RagdollComponent>(entity);
            if (!ragdoll.active)
            {
                continue;
            }

            Vec3 axisX {};
            Vec3 axisY {};
            Vec3 axisZ {};
            computeAxes(transform, axisX, axisY, axisZ);
            (void)axisZ;
            const bool selected = isSelected(entity);
            const ImU32 color = selected ? IM_COL32(255, 188, 242, 255) : IM_COL32(228, 142, 208, 230);
            const Vec3 pelvis = worldPositionOf(transform);
            const Vec3 chest = pelvis + axisY * 0.35f;
            const Vec3 head = chest + axisY * 0.22f;
            const Vec3 leftArm = chest - axisX * 0.22f;
            const Vec3 rightArm = chest + axisX * 0.22f;
            const Vec3 leftLeg = pelvis - axisY * 0.34f - axisX * 0.12f;
            const Vec3 rightLeg = pelvis - axisY * 0.34f + axisX * 0.12f;
            drawSegment(pelvis, chest, color, selected ? 1.9f : 1.5f);
            drawSegment(chest, head, color, selected ? 1.9f : 1.5f);
            drawSegment(chest, leftArm, color, selected ? 1.8f : 1.4f);
            drawSegment(chest, rightArm, color, selected ? 1.8f : 1.4f);
            drawSegment(pelvis, leftLeg, color, selected ? 1.8f : 1.4f);
            drawSegment(pelvis, rightLeg, color, selected ? 1.8f : 1.4f);
        }
    }
}
