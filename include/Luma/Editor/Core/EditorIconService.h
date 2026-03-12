#pragma once

#include <cstdint>

namespace Luma
{
    class IRenderBackend;

    namespace Editor
    {
        struct EditorIconSet
        {
            void* gizmoSelect = nullptr;
            void* gizmoTranslate = nullptr;
            void* gizmoRotate = nullptr;
            void* gizmoScale = nullptr;
            void* gizmoSnap = nullptr;
            void* gizmoGrid = nullptr;
            void* hierarchyPanel = nullptr;
            void* hierarchyCreate = nullptr;
            void* hierarchyCamera = nullptr;
            void* hierarchyCube = nullptr;
            void* hierarchyPlane = nullptr;
            void* hierarchySphere = nullptr;
            void* hierarchyCylinder = nullptr;
            void* hierarchyRigidBody = nullptr;
            void* hierarchyHinge = nullptr;
            void* viewportPanel = nullptr;
            void* physicsKinematicBody = nullptr;
            void* physicsColliderBox = nullptr;
            void* physicsColliderSphere = nullptr;
            void* physicsColliderCapsule = nullptr;
            void* physicsColliderConvex = nullptr;
            void* toolbarSelectionDetails = nullptr;
            void* toolbarPause = nullptr;
            void* toolbarStop = nullptr;
        };

        class EditorIconService
        {
        public:
            bool EnsureLoaded(IRenderBackend* renderer);
            void Release(IRenderBackend* renderer);

            const EditorIconSet& Icons() const
            {
                return m_Icons;
            }

        private:
            bool HasAnyIcon() const;

        private:
            bool m_LoadAttempted = false;
            EditorIconSet m_Icons {};
        };
    }
}
