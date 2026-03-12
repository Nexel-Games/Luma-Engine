#include "Luma/Editor/Core/EditorIconService.h"

#include <array>
#include <filesystem>
#include <initializer_list>
#include <system_error>
#include <vector>

#include <stb_image.h>

#include "Luma/RHI/IRenderBackend.h"

namespace
{
    std::vector<std::filesystem::path> BuildIconCandidates(
        std::initializer_list<std::filesystem::path> absoluteCandidates,
        const std::filesystem::path& relativePath)
    {
        std::vector<std::filesystem::path> candidates(absoluteCandidates);
        const auto current = std::filesystem::current_path();
        const auto parent = current.parent_path();
        const auto parent2 = parent.parent_path();
        const std::array<std::filesystem::path, 3> roots { current, parent, parent2 };

        for (const auto& root : roots)
        {
            candidates.push_back(root / "thirdparty" / "editor-icons" / "imgs" / relativePath);
        }
        for (const auto& root : roots)
        {
            candidates.push_back(root / "LumaEngine" / "thirdparty" / "editor-icons" / "imgs" / relativePath);
        }

        return candidates;
    }

    void* LoadIconFromCandidates(
        Luma::IRenderBackend& renderer,
        const std::vector<std::filesystem::path>& candidates)
    {
        std::error_code ec;
        for (const auto& candidate : candidates)
        {
            if (!std::filesystem::exists(candidate, ec) || !std::filesystem::is_regular_file(candidate, ec))
            {
                continue;
            }

            int width = 0;
            int height = 0;
            int channels = 0;
            stbi_set_flip_vertically_on_load(0);
            unsigned char* pixels = stbi_load(candidate.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
            if (pixels == nullptr || width <= 0 || height <= 0)
            {
                continue;
            }

            void* texture = renderer.CreateImGuiTextureRGBA8(
                static_cast<std::uint32_t>(width),
                static_cast<std::uint32_t>(height),
                reinterpret_cast<const std::uint8_t*>(pixels));
            stbi_image_free(pixels);
            if (texture != nullptr)
            {
                return texture;
            }
        }

        return nullptr;
    }

    void ReleaseIcon(Luma::IRenderBackend* renderer, void*& texture)
    {
        if (texture != nullptr && renderer != nullptr)
        {
            renderer->DestroyImGuiTexture(texture);
        }
        texture = nullptr;
    }
}

namespace Luma::Editor
{
    bool EditorIconService::EnsureLoaded(IRenderBackend* renderer)
    {
        if (HasAnyIcon())
        {
            return true;
        }
        if (m_LoadAttempted || renderer == nullptr)
        {
            return false;
        }

        m_LoadAttempted = true;

        m_Icons.gizmoSelect = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Common/Cursor.png"),
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Common/Cursor.png")
        }, "Common/Cursor.png"));
        m_Icons.gizmoTranslate = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/icon_translate_40x.png")
        }, "Icons/icon_translate_40x.png"));
        m_Icons.gizmoRotate = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/icon_rotate_40x.png")
        }, "Icons/icon_rotate_40x.png"));
        m_Icons.gizmoScale = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/icon_scale_40x.png")
        }, "Icons/icon_scale_40x.png"));
        m_Icons.gizmoSnap = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/PhysicsAssetEditor/icon_PhAT_Snap_40x.png")
        }, "PhysicsAssetEditor/icon_PhAT_Snap_40x.png"));
        m_Icons.gizmoGrid = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/icon_MatEd_Grid_40x.png")
        }, "Icons/icon_MatEd_Grid_40x.png"));
        m_Icons.hierarchyPanel = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/icon_tab_SceneOutliner_16x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Icons/icon_tab_SceneOutliner_16x.png")
        }, "Icons/icon_tab_SceneOutliner_16x.png"));
        m_Icons.hierarchyCreate = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/PlusSymbol_12x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Icons/PlusSymbol_12x.png")
        }, "Icons/PlusSymbol_12x.png"));
        m_Icons.hierarchyCamera = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/AssetIcons/CameraActor_16x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Icons/AssetIcons/CameraActor_16x.png")
        }, "Icons/AssetIcons/CameraActor_16x.png"));
        m_Icons.hierarchyCube = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/icon_MatEd_Cube_40x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Icons/icon_MatEd_Cube_40x.png")
        }, "Icons/icon_MatEd_Cube_40x.png"));
        m_Icons.hierarchyPlane = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/icon_MatEd_Plane_40x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Icons/icon_MatEd_Plane_40x.png")
        }, "Icons/icon_MatEd_Plane_40x.png"));
        m_Icons.hierarchySphere = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/icon_MatEd_Sphere_40x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Icons/icon_MatEd_Sphere_40x.png")
        }, "Icons/icon_MatEd_Sphere_40x.png"));
        m_Icons.hierarchyCylinder = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/icon_MatEd_Cylinder_40x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Icons/icon_MatEd_Cylinder_40x.png")
        }, "Icons/icon_MatEd_Cylinder_40x.png"));
        m_Icons.hierarchyRigidBody = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/PhysicsAssetEditor/icon_PHatMode_Body_40x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/PhysicsAssetEditor/icon_PHatMode_Body_40x.png")
        }, "PhysicsAssetEditor/icon_PHatMode_Body_40x.png"));
        m_Icons.hierarchyHinge = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/PhysicsAssetEditor/icon_PhAT_Hinge_40x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/PhysicsAssetEditor/icon_PhAT_Hinge_40x.png")
        }, "PhysicsAssetEditor/icon_PhAT_Hinge_40x.png"));
        m_Icons.viewportPanel = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/icon_tab_Viewports_16x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Icons/icon_tab_Viewports_16x.png")
        }, "Icons/icon_tab_Viewports_16x.png"));
        m_Icons.contentAudio = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Sequencer/Dropdown_Icons/Icon_Audio_Track_16x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Sequencer/Dropdown_Icons/Icon_Audio_Track_16x.png")
        }, "Sequencer/Dropdown_Icons/Icon_Audio_Track_16x.png"));
        m_Icons.physicsKinematicBody = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/PhysicsAssetEditor/KinematicBody_16x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/PhysicsAssetEditor/KinematicBody_16x.png")
        }, "PhysicsAssetEditor/KinematicBody_16x.png"));
        m_Icons.physicsColliderBox = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/PhysicsAssetEditor/icon_PhAT_Box_40x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/PhysicsAssetEditor/icon_PhAT_Box_40x.png")
        }, "PhysicsAssetEditor/icon_PhAT_Box_40x.png"));
        m_Icons.physicsColliderSphere = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/PhysicsAssetEditor/icon_PhAT_Sphere_40x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/PhysicsAssetEditor/icon_PhAT_Sphere_40x.png")
        }, "PhysicsAssetEditor/icon_PhAT_Sphere_40x.png"));
        m_Icons.physicsColliderCapsule = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/PhysicsAssetEditor/icon_PhAT_TaperedCapsule_40x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/PhysicsAssetEditor/icon_PhAT_TaperedCapsule_40x.png")
        }, "PhysicsAssetEditor/icon_PhAT_TaperedCapsule_40x.png"));
        m_Icons.physicsColliderConvex = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/PhysicsAssetEditor/Convex_16x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/PhysicsAssetEditor/Convex_16x.png")
        }, "PhysicsAssetEditor/Convex_16x.png"));
        m_Icons.toolbarSelectionDetails = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/generic_play_16x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Icons/generic_play_16x.png")
        }, "Icons/generic_play_16x.png"));
        m_Icons.toolbarPause = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/generic_pause_16x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Icons/generic_pause_16x.png"),
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/icon_pause_40x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Icons/icon_pause_40x.png")
        }, "Icons/generic_pause_16x.png"));
        if (m_Icons.toolbarPause == nullptr)
        {
            m_Icons.toolbarPause = LoadIconFromCandidates(*renderer, BuildIconCandidates({
                std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/icon_pause_40x.png"),
                std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Icons/icon_pause_40x.png")
            }, "Icons/icon_pause_40x.png"));
        }
        m_Icons.toolbarStop = LoadIconFromCandidates(*renderer, BuildIconCandidates({
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/generic_stop_16x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Icons/generic_stop_16x.png"),
            std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/icon_stop_40x.png"),
            std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Icons/icon_stop_40x.png")
        }, "Icons/generic_stop_16x.png"));
        if (m_Icons.toolbarStop == nullptr)
        {
            m_Icons.toolbarStop = LoadIconFromCandidates(*renderer, BuildIconCandidates({
                std::filesystem::path("C:/Luma/thirdparty/editor-icons/imgs/Icons/icon_stop_40x.png"),
                std::filesystem::path("C:/Luma/LumaEngine/thirdparty/editor-icons/imgs/Icons/icon_stop_40x.png")
            }, "Icons/icon_stop_40x.png"));
        }

        return HasAnyIcon();
    }

    void EditorIconService::Release(IRenderBackend* renderer)
    {
        ReleaseIcon(renderer, m_Icons.gizmoSelect);
        ReleaseIcon(renderer, m_Icons.gizmoTranslate);
        ReleaseIcon(renderer, m_Icons.gizmoRotate);
        ReleaseIcon(renderer, m_Icons.gizmoScale);
        ReleaseIcon(renderer, m_Icons.gizmoSnap);
        ReleaseIcon(renderer, m_Icons.gizmoGrid);
        ReleaseIcon(renderer, m_Icons.hierarchyPanel);
        ReleaseIcon(renderer, m_Icons.hierarchyCreate);
        ReleaseIcon(renderer, m_Icons.hierarchyCamera);
        ReleaseIcon(renderer, m_Icons.hierarchyCube);
        ReleaseIcon(renderer, m_Icons.hierarchyPlane);
        ReleaseIcon(renderer, m_Icons.hierarchySphere);
        ReleaseIcon(renderer, m_Icons.hierarchyCylinder);
        ReleaseIcon(renderer, m_Icons.hierarchyRigidBody);
        ReleaseIcon(renderer, m_Icons.hierarchyHinge);
        ReleaseIcon(renderer, m_Icons.viewportPanel);
        ReleaseIcon(renderer, m_Icons.contentAudio);
        ReleaseIcon(renderer, m_Icons.physicsKinematicBody);
        ReleaseIcon(renderer, m_Icons.physicsColliderBox);
        ReleaseIcon(renderer, m_Icons.physicsColliderSphere);
        ReleaseIcon(renderer, m_Icons.physicsColliderCapsule);
        ReleaseIcon(renderer, m_Icons.physicsColliderConvex);
        ReleaseIcon(renderer, m_Icons.toolbarSelectionDetails);
        ReleaseIcon(renderer, m_Icons.toolbarPause);
        ReleaseIcon(renderer, m_Icons.toolbarStop);
        m_LoadAttempted = false;
    }

    bool EditorIconService::HasAnyIcon() const
    {
        return m_Icons.gizmoSelect != nullptr ||
            m_Icons.gizmoTranslate != nullptr ||
            m_Icons.gizmoRotate != nullptr ||
            m_Icons.gizmoScale != nullptr ||
            m_Icons.gizmoSnap != nullptr ||
            m_Icons.gizmoGrid != nullptr ||
            m_Icons.hierarchyPanel != nullptr ||
            m_Icons.hierarchyCreate != nullptr ||
            m_Icons.hierarchyCamera != nullptr ||
            m_Icons.hierarchyCube != nullptr ||
            m_Icons.hierarchyPlane != nullptr ||
            m_Icons.hierarchySphere != nullptr ||
            m_Icons.hierarchyCylinder != nullptr ||
            m_Icons.hierarchyRigidBody != nullptr ||
            m_Icons.hierarchyHinge != nullptr ||
            m_Icons.viewportPanel != nullptr ||
            m_Icons.contentAudio != nullptr ||
            m_Icons.physicsKinematicBody != nullptr ||
            m_Icons.physicsColliderBox != nullptr ||
            m_Icons.physicsColliderSphere != nullptr ||
            m_Icons.physicsColliderCapsule != nullptr ||
            m_Icons.physicsColliderConvex != nullptr ||
            m_Icons.toolbarSelectionDetails != nullptr ||
            m_Icons.toolbarPause != nullptr ||
            m_Icons.toolbarStop != nullptr;
    }
}
