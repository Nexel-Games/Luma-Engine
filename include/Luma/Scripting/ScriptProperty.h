#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <vector>

#include "Luma/Core/Foundation/UUID.h"

namespace Luma
{
    enum class ScriptValueType
    {
        None = 0,
        Bool,
        Int,
        Float,
        String,
        Entity,
        Vec2,
        Vec3,
        Vec4
    };

    enum class ScriptPropertyEditorHint
    {
        Default = 0,
        Color3,
        Color4,
        Enum,
        AssetReference,
        EntityReference
    };

    struct ScriptValue
    {
        ScriptValueType type = ScriptValueType::None;

        bool boolValue = false;
        int intValue = 0;
        float floatValue = 0.0f;
        std::string stringValue;
        UUID entityValue = 0;
        std::array<float, 2> vec2Value { 0.0f, 0.0f };
        std::array<float, 3> vec3Value { 0.0f, 0.0f, 0.0f };
        std::array<float, 4> vec4Value { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    struct ScriptPropertyInfo
    {
        std::string name;
        ScriptValue defaultValue;
        ScriptPropertyEditorHint editorHint = ScriptPropertyEditorHint::Default;
        std::string tooltip;
        bool hasMinValue = false;
        bool hasMaxValue = false;
        float minValue = 0.0f;
        float maxValue = 0.0f;
        std::vector<std::string> options;
        std::string assetTypeFilter;
    };

    struct LuaScriptAssetMetadata
    {
        std::vector<ScriptPropertyInfo> properties;
    };

    enum class ScriptPropertyOverrideIssueType
    {
        MissingProperty = 0,
        TypeMismatch
    };

    struct ScriptPropertyOverrideIssue
    {
        std::string propertyName;
        ScriptPropertyOverrideIssueType type = ScriptPropertyOverrideIssueType::MissingProperty;
        ScriptValueType expectedType = ScriptValueType::None;
        ScriptValueType actualType = ScriptValueType::None;
    };

    inline bool ScriptValueTypeMatches(const ScriptValue& lhs, const ScriptValue& rhs)
    {
        return lhs.type == rhs.type;
    }

    inline bool ScriptValuesEqual(const ScriptValue& lhs, const ScriptValue& rhs)
    {
        if (lhs.type != rhs.type)
        {
            return false;
        }

        switch (lhs.type)
        {
        case ScriptValueType::Bool:
            return lhs.boolValue == rhs.boolValue;
        case ScriptValueType::Int:
            return lhs.intValue == rhs.intValue;
        case ScriptValueType::Float:
            return lhs.floatValue == rhs.floatValue;
        case ScriptValueType::String:
            return lhs.stringValue == rhs.stringValue;
        case ScriptValueType::Entity:
            return lhs.entityValue == rhs.entityValue;
        case ScriptValueType::Vec2:
            return lhs.vec2Value == rhs.vec2Value;
        case ScriptValueType::Vec3:
            return lhs.vec3Value == rhs.vec3Value;
        case ScriptValueType::Vec4:
            return lhs.vec4Value == rhs.vec4Value;
        case ScriptValueType::None:
        default:
            return true;
        }
    }

    inline const ScriptPropertyInfo* FindScriptPropertyInfo(
        const LuaScriptAssetMetadata& metadata,
        const std::string_view propertyName)
    {
        for (const ScriptPropertyInfo& property : metadata.properties)
        {
            if (property.name == propertyName)
            {
                return &property;
            }
        }

        return nullptr;
    }

    inline void CollectScriptPropertyOverrideIssues(
        const LuaScriptAssetMetadata& metadata,
        const std::unordered_map<std::string, ScriptValue>& overrides,
        std::vector<ScriptPropertyOverrideIssue>& outIssues)
    {
        outIssues.clear();
        outIssues.reserve(overrides.size());
        for (const auto& [propertyName, value] : overrides)
        {
            const ScriptPropertyInfo* property = FindScriptPropertyInfo(metadata, propertyName);
            if (property == nullptr)
            {
                outIssues.push_back({ propertyName, ScriptPropertyOverrideIssueType::MissingProperty, ScriptValueType::None, value.type });
                continue;
            }

            if (!ScriptValueTypeMatches(value, property->defaultValue))
            {
                outIssues.push_back({
                    propertyName,
                    ScriptPropertyOverrideIssueType::TypeMismatch,
                    property->defaultValue.type,
                    value.type
                });
            }
        }
    }

    inline bool IsScriptPropertyOverrideValid(
        const LuaScriptAssetMetadata& metadata,
        const std::string_view propertyName,
        const ScriptValue& value)
    {
        const ScriptPropertyInfo* property = FindScriptPropertyInfo(metadata, propertyName);
        return property != nullptr && ScriptValueTypeMatches(value, property->defaultValue);
    }
}
