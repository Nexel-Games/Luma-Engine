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
