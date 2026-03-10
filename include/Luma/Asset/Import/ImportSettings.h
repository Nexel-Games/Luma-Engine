#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace Luma::Assets
{
    enum class ImportSettingType
    {
        Bool = 0,
        Int,
        Float,
        String,
        Enum,
        Path
    };

    struct ImportSettingField
    {
        std::string key;
        std::string label;
        std::string group;
        ImportSettingType type = ImportSettingType::String;
        std::string defaultValue;
        std::string minValue;
        std::string maxValue;
        std::vector<std::string> enumValues;
        std::string tooltip;
    };

    struct ImportSettingsSchema
    {
        std::string importerID;
        std::vector<ImportSettingField> fields;
    };

    using ImportSettingsMap = std::unordered_map<std::string, std::string>;

    inline ImportSettingsMap BuildDefaultSettings(const ImportSettingsSchema& schema)
    {
        ImportSettingsMap settings;
        for (const ImportSettingField& field : schema.fields)
        {
            settings[field.key] = field.defaultValue;
        }
        return settings;
    }
}

