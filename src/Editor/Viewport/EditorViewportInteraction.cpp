#include "Luma/Editor/Viewport/EditorViewportInteraction.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

#include <ImGuizmo.h>

#include "Luma/Scene/CameraComponent.h"
#include "Luma/Scene/RelationshipComponent.h"
#include "Luma/Scene/TransformComponent.h"

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

        Vec3 operator+(const Vec3& lhs, const Vec3& rhs)
        {
            return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
        }

        Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
        {
            return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
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

        ImGuizmo::OPERATION ToImGuizmoOperation(const std::uint8_t operation)
        {
            switch (static_cast<EditorViewportController::GizmoOperation>(operation))
            {
            case EditorViewportController::GizmoOperation::Rotate:
                return ImGuizmo::ROTATE;
            case EditorViewportController::GizmoOperation::Scale:
                return ImGuizmo::SCALE;
            case EditorViewportController::GizmoOperation::Translate:
            default:
                return ImGuizmo::TRANSLATE;
            }
        }
    }

    void EditorViewportInteraction::Handle(const EditorViewportInteractionContext& context)
    {
        if (context.scene == nullptr ||
            context.viewportController == nullptr ||
            context.selectionState == nullptr ||
            context.editorCamera == nullptr ||
            context.drawList == nullptr)
        {
            return;
        }

        auto& registry = context.scene->GetRegistry();
        auto& viewportController = *context.viewportController;
        auto& selectionState = *context.selectionState;
        auto notifySelectionChanged = [&]()
        {
            if (context.onSelectionChanged)
            {
                context.onSelectionChanged();
            }
        };

        if (!context.drawSceneTexture)
        {
            viewportController.MarqueeSelecting() = false;
            return;
        }

        float fovDegrees = 60.0f;
        if (context.lensSourceEntity != entt::null &&
            registry.valid(context.lensSourceEntity) &&
            registry.all_of<CameraComponent>(context.lensSourceEntity))
        {
            const auto& camera = registry.get<CameraComponent>(context.lensSourceEntity);
            fovDegrees = std::clamp(camera.fovDegrees, 10.0f, 170.0f);
        }

        constexpr float kPi = 3.14159265359f;
        const float yawRadians = context.editorCamera->yaw * (kPi / 180.0f);
        const float pitchRadians = context.editorCamera->pitch * (kPi / 180.0f);
        const Vec3 eye {
            context.editorCamera->position[0],
            context.editorCamera->position[1],
            context.editorCamera->position[2]
        };
        const Vec3 forward = Normalize({
            std::cos(yawRadians) * std::cos(pitchRadians),
            std::sin(pitchRadians),
            std::sin(yawRadians) * std::cos(pitchRadians)
        });
        const Mat4 view = BuildLookAt(eye, eye + forward, Vec3 { 0.0f, 1.0f, 0.0f });
        const float aspectRatio = std::max(context.renderAreaSize.x, 1.0f) / std::max(context.renderAreaSize.y, 1.0f);
        const float fovRadians = fovDegrees * (kPi / 180.0f);
        const Mat4 projection = BuildPerspective(fovRadians, aspectRatio, 0.1f, 2000.0f);
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

        auto pickNearestEntityAt = [&](const ImVec2& samplePosition, const float maxDistancePixels) -> EntityID
        {
            const auto entityView = registry.view<TransformComponent>();
            EntityID nearestEntity = entt::null;
            float nearestDistanceSq = std::numeric_limits<float>::max();
            for (const EntityID entity : entityView)
            {
                const auto& transform = entityView.get<TransformComponent>(entity);
                ImVec2 entityScreenPosition {};
                if (!projectWorldToScreen(
                        Vec3 {
                            transform.worldPosition[0],
                            transform.worldPosition[1],
                            transform.worldPosition[2]
                        },
                        entityScreenPosition))
                {
                    continue;
                }

                const float dx = entityScreenPosition.x - samplePosition.x;
                const float dy = entityScreenPosition.y - samplePosition.y;
                const float distanceSq = dx * dx + dy * dy;
                if (distanceSq < nearestDistanceSq)
                {
                    nearestDistanceSq = distanceSq;
                    nearestEntity = entity;
                }
            }

            if (nearestEntity != entt::null &&
                nearestDistanceSq <= (maxDistancePixels * maxDistancePixels))
            {
                return nearestEntity;
            }
            return entt::null;
        };

        const ImVec2 mousePosition = ImGui::GetMousePos();
        const bool mouseInViewport = mousePosition.x >= context.viewportMin.x && mousePosition.x <= context.viewportMax.x &&
            mousePosition.y >= context.viewportMin.y && mousePosition.y <= context.viewportMax.y;
        const bool additiveSelection = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift;

        const bool clickSelectInTransformMode =
            mouseInViewport &&
            !context.viewportInputBlockedByPopup &&
            !viewportController.SelectToolActive() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !ImGui::IsAnyItemActive() &&
            !viewportController.GizmoInteracting() &&
            !ImGuizmo::IsOver();
        if (clickSelectInTransformMode)
        {
            const EntityID nearestEntity = pickNearestEntityAt(mousePosition, 16.0f);
            if (nearestEntity != entt::null)
            {
                if (additiveSelection)
                {
                    selectionState.Toggle(nearestEntity);
                }
                else
                {
                    selectionState.SelectSingle(nearestEntity);
                }
                notifySelectionChanged();
            }
            else if (!additiveSelection)
            {
                selectionState.Clear();
                notifySelectionChanged();
            }
            selectionState.Prune(registry);
        }

        if (viewportController.SelectToolActive())
        {
            if (mouseInViewport &&
                !context.viewportInputBlockedByPopup &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGui::IsAnyItemActive() &&
                !ImGuizmo::IsOver())
            {
                viewportController.MarqueeSelecting() = true;
                viewportController.MarqueeStart() = { mousePosition.x, mousePosition.y };
                viewportController.MarqueeCurrent() = viewportController.MarqueeStart();
            }

            if (viewportController.MarqueeSelecting())
            {
                viewportController.MarqueeCurrent() = { mousePosition.x, mousePosition.y };
                const float minX = std::min(viewportController.MarqueeStart()[0], viewportController.MarqueeCurrent()[0]);
                const float minY = std::min(viewportController.MarqueeStart()[1], viewportController.MarqueeCurrent()[1]);
                const float maxX = std::max(viewportController.MarqueeStart()[0], viewportController.MarqueeCurrent()[0]);
                const float maxY = std::max(viewportController.MarqueeStart()[1], viewportController.MarqueeCurrent()[1]);

                context.drawList->AddRectFilled(
                    ImVec2(minX, minY),
                    ImVec2(maxX, maxY),
                    IM_COL32(76, 176, 255, 36),
                    2.0f);
                context.drawList->AddRect(
                    ImVec2(minX, minY),
                    ImVec2(maxX, maxY),
                    IM_COL32(76, 176, 255, 220),
                    2.0f,
                    0,
                    1.5f);
                viewportController.SetGizmoInteracting(true);

                if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
                {
                    const float selectionWidth = maxX - minX;
                    const float selectionHeight = maxY - minY;

                    if (!additiveSelection)
                    {
                        selectionState.Clear();
                        notifySelectionChanged();
                    }

                    if (selectionWidth <= 3.0f && selectionHeight <= 3.0f)
                    {
                        const EntityID nearestEntity = pickNearestEntityAt(mousePosition, 16.0f);
                        if (nearestEntity != entt::null)
                        {
                            if (additiveSelection)
                            {
                                selectionState.Toggle(nearestEntity);
                            }
                            else
                            {
                                selectionState.SelectSingle(nearestEntity);
                            }
                            notifySelectionChanged();
                        }
                    }
                    else
                    {
                        const auto entityView = registry.view<TransformComponent>();
                        bool appendedAny = false;
                        for (const EntityID entity : entityView)
                        {
                            const auto& transform = entityView.get<TransformComponent>(entity);
                            ImVec2 entityScreenPosition {};
                            if (!projectWorldToScreen(
                                    Vec3 {
                                        transform.worldPosition[0],
                                        transform.worldPosition[1],
                                        transform.worldPosition[2]
                                    },
                                    entityScreenPosition))
                            {
                                continue;
                            }

                            if (entityScreenPosition.x < minX || entityScreenPosition.x > maxX ||
                                entityScreenPosition.y < minY || entityScreenPosition.y > maxY)
                            {
                                continue;
                            }

                            selectionState.Append(entity);
                            appendedAny = true;
                        }

                        if (appendedAny)
                        {
                            notifySelectionChanged();
                        }
                    }

                    selectionState.Prune(registry);
                    viewportController.MarqueeSelecting() = false;
                }
            }
        }
        else
        {
            viewportController.MarqueeSelecting() = false;
        }

        const EntityID selectedEntity = selectionState.PrimaryRef();
        const bool canUseGizmo =
            selectedEntity != entt::null &&
            selectionState.IsSelected(selectedEntity) &&
            registry.valid(selectedEntity) &&
            registry.all_of<TransformComponent>(selectedEntity);
        if (!canUseGizmo)
        {
            return;
        }

        auto& transform = registry.get<TransformComponent>(selectedEntity);
        const RelationshipComponent* relationship = registry.try_get<RelationshipComponent>(selectedEntity);

        float translation[3] = {
            transform.worldPosition[0],
            transform.worldPosition[1],
            transform.worldPosition[2]
        };
        float rotation[3] = {
            transform.worldRotation[0],
            transform.worldRotation[1],
            transform.worldRotation[2]
        };
        float scale[3] = {
            std::max(std::abs(transform.worldScale[0]), 1.0e-4f),
            std::max(std::abs(transform.worldScale[1]), 1.0e-4f),
            std::max(std::abs(transform.worldScale[2]), 1.0e-4f)
        };

        float modelMatrix[16] = {};
        ImGuizmo::RecomposeMatrixFromComponents(translation, rotation, scale, modelMatrix);

        float viewMatrix[16] = {};
        float projectionMatrix[16] = {};
        std::memcpy(viewMatrix, view.elements.data(), sizeof(viewMatrix));
        std::memcpy(projectionMatrix, projection.elements.data(), sizeof(projectionMatrix));

        ImGuizmo::SetOrthographic(false);
        ImGuizmo::Enable(true);
        ImGuizmo::SetID(0);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(context.viewportMin.x, context.viewportMin.y, context.renderAreaSize.x, context.renderAreaSize.y);

        float snapValues[3] = {};
        float* snapPtr = nullptr;
        if (viewportController.GizmoSnapEnabled())
        {
            switch (viewportController.ActiveGizmoOperation())
            {
            case EditorViewportController::GizmoOperation::Translate:
                snapValues[0] = viewportController.GizmoTranslateSnap()[0];
                snapValues[1] = viewportController.GizmoTranslateSnap()[1];
                snapValues[2] = viewportController.GizmoTranslateSnap()[2];
                break;
            case EditorViewportController::GizmoOperation::Rotate:
                snapValues[0] = viewportController.GizmoRotateSnap();
                break;
            case EditorViewportController::GizmoOperation::Scale:
                snapValues[0] = viewportController.GizmoScaleSnap()[0];
                snapValues[1] = viewportController.GizmoScaleSnap()[1];
                snapValues[2] = viewportController.GizmoScaleSnap()[2];
                break;
            default:
                break;
            }
            snapPtr = snapValues;
        }

        ImGuizmo::Manipulate(
            viewMatrix,
            projectionMatrix,
            ToImGuizmoOperation(static_cast<std::uint8_t>(viewportController.ActiveGizmoOperation())),
            viewportController.GizmoLocalSpace() ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
            modelMatrix,
            nullptr,
            snapPtr);

        viewportController.SetGizmoInteracting(
            ImGuizmo::IsUsing() || (ImGuizmo::IsOver() && ImGui::IsMouseDown(ImGuiMouseButton_Left)));
        if (!ImGuizmo::IsUsing())
        {
            return;
        }

        float resultTranslation[3] = {};
        float resultRotation[3] = {};
        float resultScale[3] = {};
        ImGuizmo::DecomposeMatrixToComponents(modelMatrix, resultTranslation, resultRotation, resultScale);

        const Vec3 worldPosition { resultTranslation[0], resultTranslation[1], resultTranslation[2] };
        const Vec3 worldRotation { resultRotation[0], resultRotation[1], resultRotation[2] };
        const Vec3 worldScale {
            std::max(resultScale[0], 0.001f),
            std::max(resultScale[1], 0.001f),
            std::max(resultScale[2], 0.001f)
        };

        if (relationship != nullptr &&
            relationship->parent != entt::null &&
            registry.valid(relationship->parent) &&
            registry.all_of<TransformComponent>(relationship->parent))
        {
            const auto& parentTransform = registry.get<TransformComponent>(relationship->parent);
            transform.position = {
                worldPosition.x - parentTransform.worldPosition[0],
                worldPosition.y - parentTransform.worldPosition[1],
                worldPosition.z - parentTransform.worldPosition[2]
            };
            transform.rotation = {
                worldRotation.x - parentTransform.worldRotation[0],
                worldRotation.y - parentTransform.worldRotation[1],
                worldRotation.z - parentTransform.worldRotation[2]
            };

            auto safeDivide = [](const float numerator, const float denominator)
            {
                if (std::abs(denominator) <= 1.0e-4f)
                {
                    return numerator;
                }
                return numerator / denominator;
            };
            transform.scale = {
                std::max(0.001f, safeDivide(worldScale.x, parentTransform.worldScale[0])),
                std::max(0.001f, safeDivide(worldScale.y, parentTransform.worldScale[1])),
                std::max(0.001f, safeDivide(worldScale.z, parentTransform.worldScale[2]))
            };
        }
        else
        {
            transform.position = { worldPosition.x, worldPosition.y, worldPosition.z };
            transform.rotation = { worldRotation.x, worldRotation.y, worldRotation.z };
            transform.scale = { worldScale.x, worldScale.y, worldScale.z };
        }

        transform.dirty = true;
        if (context.onTransformChanged)
        {
            context.onTransformChanged();
        }
    }
}
