#include "Luma/Editor/Panels/Viewport/ViewportPanel.h"

#include <algorithm>
#include <cmath>

#include "Luma/RHI/IRenderBackend.h"

namespace Luma::Editor
{
    void ViewportPanel::Draw(const ViewportPanelContext& context)
    {
        if (context.open == nullptr || context.viewportController == nullptr)
        {
            return;
        }

        auto& viewportController = *context.viewportController;
        auto& editorCamera = viewportController.Camera();
        ImGui::SetNextWindowBgAlpha(0.0f);
        if (!ImGui::Begin(
                "Viewport",
                context.open,
                ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::End();
            return;
        }

        if (context.panelIconTexture != nullptr)
        {
            ImGui::Image(
                reinterpret_cast<ImTextureID>(context.panelIconTexture),
                ImVec2(16.0f, 16.0f),
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f));
        }

        const bool viewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const bool viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const ImVec2 viewportSize(
            std::max(available.x, 1.0f),
            std::max(available.y, 1.0f));

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::BeginChild(
            "ViewportRenderArea",
            viewportSize,
            false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();

        const ImVec2 renderAreaSizeRaw = ImGui::GetContentRegionAvail();
        const ImVec2 renderAreaSize(
            std::max(renderAreaSizeRaw.x, 1.0f),
            std::max(renderAreaSizeRaw.y, 1.0f));
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 p1(p0.x + renderAreaSize.x, p0.y + renderAreaSize.y);
        const bool viewportInputBlockedByPopup =
            context.isInputBlockedByPopup ? context.isInputBlockedByPopup(p0, p1) : false;
        const bool effectiveViewportHovered = viewportHovered && !viewportInputBlockedByPopup;
        viewportController.SetViewportWindowState(viewportFocused, effectiveViewportHovered, viewportSize.x, viewportSize.y);
        void* sceneTexture = context.renderer != nullptr ? context.renderer->GetSceneOutputImGuiTexture() : nullptr;

        ImVec2 sceneUv0(0.0f, 0.0f);
        ImVec2 sceneUv1(1.0f, 1.0f);
        ImVec2 skyUv0(0.0f, 0.0f);
        ImVec2 skyUv1(1.0f, 1.0f);
        {
            constexpr float kPi = 3.14159265359f;
            const float fovDegrees = context.resolveLensFovDegrees ? context.resolveLensFovDegrees() : 60.0f;
            const float clampedFovRadians = std::clamp(fovDegrees, 20.0f, 140.0f) * (kPi / 180.0f);
            const float aspect = std::max(renderAreaSize.x, 1.0f) / std::max(renderAreaSize.y, 1.0f);
            const float horizontalFov = 2.0f * std::atan(std::tan(clampedFovRadians * 0.5f) * aspect);
            const float uSpan = std::clamp(horizontalFov / (2.0f * kPi), 0.05f, 1.0f);
            const float vSpan = std::clamp(clampedFovRadians / kPi, 0.05f, 1.0f);

            float uCenter = std::remainder(editorCamera.yaw, 360.0f) / 360.0f + 0.5f;
            uCenter = uCenter - std::floor(uCenter);
            const float pitch = std::clamp(editorCamera.pitch, -89.0f, 89.0f);
            const float vCenter = 0.5f - (pitch / 180.0f);

            float uMin = uCenter - uSpan * 0.5f;
            float uMax = uCenter + uSpan * 0.5f;
            const float vMin = std::clamp(vCenter - vSpan * 0.5f, 0.0f, 1.0f);
            const float vMax = std::clamp(vCenter + vSpan * 0.5f, 0.0f, 1.0f);

            if (uMin < 0.0f || uMax > 1.0f)
            {
                // Near the equirect seam, keep a stable fallback instead of wrapping through two draw calls.
                uMin = 0.0f;
                uMax = 1.0f;
            }

            skyUv0 = ImVec2(uMin, vMin);
            skyUv1 = ImVec2(uMax, std::max(vMax, vMin + 1.0e-4f));
        }
        if (context.renderer != nullptr && context.renderer->GetAPI() == RendererAPI::OpenGL)
        {
            sceneUv0 = ImVec2(0.0f, 1.0f);
            sceneUv1 = ImVec2(1.0f, 0.0f);
            skyUv0 = ImVec2(skyUv0.x, 1.0f - skyUv0.y);
            skyUv1 = ImVec2(skyUv1.x, 1.0f - skyUv1.y);
        }

        ImGui::SetCursorScreenPos(p0);
        const bool hasSkyboxPreview = context.skyboxPreviewTexture != nullptr;
        const bool drawSceneTexture = sceneTexture != nullptr;
        if (drawSceneTexture)
        {
            ImGui::Image(sceneTexture, renderAreaSize, sceneUv0, sceneUv1);
        }
        else if (hasSkyboxPreview)
        {
            ImGui::Image(context.skyboxPreviewTexture, renderAreaSize, skyUv0, skyUv1);
        }
        else
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(p0, p1, IM_COL32(19, 23, 29, 255), 4.0f);
            ImGui::Dummy(renderAreaSize);
        }

        if (context.handleAssetDrop)
        {
            context.handleAssetDrop();
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        viewportController.SetGizmoInteracting(false);
        const ImGuiIO& gizmoHotkeyIo = ImGui::GetIO();
        const bool allowGizmoHotkeys =
            viewportController.ViewportFocused() &&
            viewportController.ViewportHovered() &&
            !viewportController.RightMousePressed() &&
            !gizmoHotkeyIo.WantTextInput;
        viewportController.ApplyToolHotkeys(allowGizmoHotkeys);

        if (context.drawToolbar)
        {
            context.drawToolbar(drawList, p0);
        }

        if (drawSceneTexture)
        {
            const EntityID lensSourceEntity = context.gamePreviewActive
                ? viewportController.LensSourceEntity()
                : entt::null;

            if (!context.gamePreviewActive && context.showColliderDebug && context.drawDebugOverlay)
            {
                context.drawDebugOverlay(drawList, p0, renderAreaSize, lensSourceEntity);
            }

            if (!context.gamePreviewActive && context.handleInteraction)
            {
                context.handleInteraction(
                    drawList,
                    p0,
                    p1,
                    renderAreaSize,
                    viewportInputBlockedByPopup,
                    lensSourceEntity);
            }
        }
        else
        {
            viewportController.MarqueeSelecting() = false;
        }

        if (!context.gamePreviewActive)
        {
            viewportController.HandleCameraInput(context.deltaTimeSeconds);
        }
        ImGui::EndChild();
        ImGui::End();
    }
}
