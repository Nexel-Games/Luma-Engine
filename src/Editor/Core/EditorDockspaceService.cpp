#include "Luma/Editor/Core/EditorDockspaceService.h"

#include <imgui.h>
#include <imgui_internal.h>

namespace Luma::Editor
{
    namespace
    {
        void BuildDefaultDockLayout(const ImGuiID dockspaceId)
        {
            if (dockspaceId == 0)
            {
                return;
            }

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            if (viewport == nullptr)
            {
                return;
            }

            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

            ImGuiID mainNode = dockspaceId;
            const ImGuiID leftNode = ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Left, 0.20f, nullptr, &mainNode);
            const ImGuiID rightNode = ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Right, 0.25f, nullptr, &mainNode);
            const ImGuiID bottomNode = ImGui::DockBuilderSplitNode(mainNode, ImGuiDir_Down, 0.30f, nullptr, &mainNode);

            ImGui::DockBuilderDockWindow("Hierarchy", leftNode);
            ImGui::DockBuilderDockWindow("Inspector", rightNode);
            ImGui::DockBuilderDockWindow("Content Browser", bottomNode);
            ImGui::DockBuilderDockWindow("Console", bottomNode);
            ImGui::DockBuilderDockWindow("Viewport", mainNode);
            ImGui::DockBuilderFinish(dockspaceId);
        }
    }

    void EditorDockspaceService::Update(bool& dockLayoutInitialized) const
    {
        const ImGuiIO& io = ImGui::GetIO();
        const bool dockingEnabled = (io.ConfigFlags & ImGuiConfigFlags_DockingEnable) != 0;
        if (!dockingEnabled)
        {
            return;
        }

        const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
        if (!dockLayoutInitialized)
        {
            BuildDefaultDockLayout(dockspaceId);
            dockLayoutInitialized = true;
        }
    }
}
