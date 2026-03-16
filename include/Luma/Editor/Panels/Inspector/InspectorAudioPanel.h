#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "Luma/Scene/Scene.h"

namespace Luma::Editor
{
    struct InspectorAudioPanelContext
    {
        Scene* scene = nullptr;
        EntityID selectedEntity = entt::null;
        const std::filesystem::path* selectedContentEntry = nullptr;
        bool playModeActive = false;
        std::function<void(std::string)> setContentStatus;
    };

    class InspectorAudioPanel
    {
    public:
        void Draw(const InspectorAudioPanelContext& context);

    private:
        void StopPreview();

        std::uint64_t m_PreviewHandle = 0;
        EntityID m_PreviewEntity = entt::null;
        std::string m_PreviewClipAsset;
    };
}
