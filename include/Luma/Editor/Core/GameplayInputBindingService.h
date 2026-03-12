#pragma once

#include <cstddef>
#include <string_view>

namespace Luma::Editor
{
    struct InputActionSettingsEntry
    {
        const char* label = "";
        std::string_view context;
        std::string_view action;
        bool allowAxes = false;
    };

    class GameplayInputBindingService
    {
    public:
        void ConfigureEditorGameplayDefaults() const;
        static const char* GetGameplayInputContextName();
        static const InputActionSettingsEntry* GetDefaultInputActionSettingsEntries(std::size_t& outCount);
        static bool IsVehicleInputAction(std::string_view actionName);
        static void ResetVehicleInputBindings(std::string_view contextName);
    };
}
