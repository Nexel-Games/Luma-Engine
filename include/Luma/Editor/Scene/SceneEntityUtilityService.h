#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "Luma/Editor/Core/EditorSelectionState.h"
#include "Luma/Scene/PostProcessComponent.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SkyLightComponent.h"

namespace Luma::Editor
{
    struct SceneEntitySelectionContext
    {
        Scene* scene = nullptr;
        EditorSelectionState* selectionState = nullptr;
        std::filesystem::path* selectedContentEntry = nullptr;
    };

    class SceneEntityUtilityService
    {
    public:
        std::string GenerateUniqueEntityName(const Scene& scene, std::string_view baseName) const;
        void SelectSingleEntity(SceneEntitySelectionContext& context, EntityID entity) const;
        void ToggleEntitySelection(SceneEntitySelectionContext& context, EntityID entity) const;
        bool IsEntitySelected(const EditorSelectionState& selectionState, EntityID entity) const;
        void ClearEntitySelection(EditorSelectionState& selectionState) const;
        void PruneEntitySelection(Scene& scene, EditorSelectionState& selectionState) const;
        void InitializeSkyLightDefaults(SkyLightComponent& skyLight) const;
        void InitializePostProcessDefaults(PostProcessComponent& postProcess) const;
    };
}
