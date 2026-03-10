#include "Luma/Editor/Viewport/ViewportAssetDropService.h"

#include <imgui.h>

namespace Luma::Editor
{
    void ViewportAssetDropService::Handle(const ViewportAssetDropContext& context)
    {
        if (!ImGui::BeginDragDropTarget())
        {
            return;
        }

        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ASSET_PATH"))
        {
            if (payload->Data != nullptr && payload->DataSize > 1)
            {
                const std::filesystem::path assetPath(static_cast<const char*>(payload->Data));
                const bool isCandidate = context.isMeshAssetPathCandidate
                    ? context.isMeshAssetPathCandidate(assetPath)
                    : false;
                if (isCandidate && context.createEntityFromMeshAsset)
                {
                    const std::array<float, 3> spawnPosition = context.computeDropSpawnPosition
                        ? context.computeDropSpawnPosition()
                        : std::array<float, 3> { 0.0f, 0.0f, 0.0f };
                    if (context.createEntityFromMeshAsset(assetPath, entt::null, &spawnPosition) != entt::null &&
                        context.setContentStatus)
                    {
                        context.setContentStatus("Created mesh entity from " + assetPath.filename().string() + ".");
                    }
                }
            }
        }

        ImGui::EndDragDropTarget();
    }
}
