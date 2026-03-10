#pragma once

namespace Luma::Editor
{
    class EditorViewportController;

    class PreferencesPanel
    {
    public:
        void Draw(bool* open, EditorViewportController& viewportController);
    };
}
