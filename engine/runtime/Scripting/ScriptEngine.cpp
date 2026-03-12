#include "Luma/Scripting/ScriptEngine.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>
#include <utility>

#include <lua.hpp>

#include "Luma/Asset/Core/AssetMetaIO.h"
#include "Luma/Core/App/Project.h"
#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Scripting/Lua/LuaHelpers.h"
#include "Luma/Scripting/Lua/LuaState.h"

namespace Luma
{
    namespace
    {
        std::string ToLowerString(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }

        std::string NormalizePathKey(const std::filesystem::path& path)
        {
            std::string key = path.lexically_normal().generic_string();
#if defined(_WIN32)
            std::transform(
                key.begin(),
                key.end(),
                key.begin(),
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
#endif
            return key;
        }

        bool TryGetFileWriteTime(
            const std::filesystem::path& path,
            std::filesystem::file_time_type& outWriteTime)
        {
            std::error_code errorCode;
            outWriteTime = std::filesystem::last_write_time(path, errorCode);
            return !errorCode;
        }

        bool TryReadArrayTable(lua_State* state, const int index, const int expectedCount, float* outValues)
        {
            const int absoluteIndex = lua_absindex(state, index);
            for (int elementIndex = 0; elementIndex < expectedCount; ++elementIndex)
            {
                lua_rawgeti(state, absoluteIndex, elementIndex + 1);
                if (!lua_isnumber(state, -1))
                {
                    lua_pop(state, 1);
                    return false;
                }

                outValues[elementIndex] = static_cast<float>(lua_tonumber(state, -1));
                lua_pop(state, 1);
            }

            lua_rawgeti(state, absoluteIndex, expectedCount + 1);
            const bool hasExtraElement = !lua_isnil(state, -1);
            lua_pop(state, 1);
            return !hasExtraElement;
        }

        bool TryReadStringField(lua_State* state, const int index, const char* fieldName, std::string& outValue)
        {
            const int absoluteIndex = lua_absindex(state, index);
            if (!lua_istable(state, absoluteIndex))
            {
                return false;
            }
            lua_getfield(state, absoluteIndex, fieldName);
            const bool valid = lua_isstring(state, -1) != 0;
            if (valid)
            {
                outValue = lua_tostring(state, -1);
            }
            lua_pop(state, 1);
            return valid;
        }

        bool TryReadNumberField(lua_State* state, const int index, const char* fieldName, float& outValue)
        {
            const int absoluteIndex = lua_absindex(state, index);
            if (!lua_istable(state, absoluteIndex))
            {
                return false;
            }
            lua_getfield(state, absoluteIndex, fieldName);
            const bool valid = lua_isnumber(state, -1) != 0;
            if (valid)
            {
                outValue = static_cast<float>(lua_tonumber(state, -1));
            }
            lua_pop(state, 1);
            return valid;
        }

        bool TryReadStringArrayField(
            lua_State* state,
            const int index,
            const char* fieldName,
            std::vector<std::string>& outValues)
        {
            const int absoluteIndex = lua_absindex(state, index);
            if (!lua_istable(state, absoluteIndex))
            {
                return false;
            }

            lua_getfield(state, absoluteIndex, fieldName);
            if (!lua_istable(state, -1))
            {
                lua_pop(state, 1);
                return false;
            }

            outValues.clear();
            const int arrayIndex = lua_absindex(state, -1);
            lua_pushnil(state);
            while (lua_next(state, arrayIndex) != 0)
            {
                if (lua_isstring(state, -1))
                {
                    outValues.emplace_back(lua_tostring(state, -1));
                }
                lua_pop(state, 1);
            }

            lua_pop(state, 1);
            return !outValues.empty();
        }

        bool HasField(lua_State* state, const int index, const char* fieldName)
        {
            const int absoluteIndex = lua_absindex(state, index);
            if (!lua_istable(state, absoluteIndex))
            {
                return false;
            }
            lua_getfield(state, absoluteIndex, fieldName);
            const bool exists = !lua_isnil(state, -1);
            lua_pop(state, 1);
            return exists;
        }

        bool ParseScriptValueTypeHint(const std::string_view value, ScriptValueType& outType, ScriptPropertyEditorHint& outHint)
        {
            const std::string lowered = ToLowerString(std::string(value));
            outHint = ScriptPropertyEditorHint::Default;

            if (lowered == "bool" || lowered == "boolean")
            {
                outType = ScriptValueType::Bool;
                return true;
            }
            if (lowered == "int" || lowered == "integer")
            {
                outType = ScriptValueType::Int;
                return true;
            }
            if (lowered == "float" || lowered == "number")
            {
                outType = ScriptValueType::Float;
                return true;
            }
            if (lowered == "string")
            {
                outType = ScriptValueType::String;
                return true;
            }
            if (lowered == "enum")
            {
                outType = ScriptValueType::String;
                outHint = ScriptPropertyEditorHint::Enum;
                return true;
            }
            if (lowered == "asset" || lowered == "assetref" || lowered == "assetreference")
            {
                outType = ScriptValueType::String;
                outHint = ScriptPropertyEditorHint::AssetReference;
                return true;
            }
            if (lowered == "entity" || lowered == "entityref" || lowered == "entityreference")
            {
                outType = ScriptValueType::Entity;
                outHint = ScriptPropertyEditorHint::EntityReference;
                return true;
            }
            if (lowered == "vec2")
            {
                outType = ScriptValueType::Vec2;
                return true;
            }
            if (lowered == "vec3")
            {
                outType = ScriptValueType::Vec3;
                return true;
            }
            if (lowered == "vec4")
            {
                outType = ScriptValueType::Vec4;
                return true;
            }
            if (lowered == "color3")
            {
                outType = ScriptValueType::Vec3;
                outHint = ScriptPropertyEditorHint::Color3;
                return true;
            }
            if (lowered == "color4")
            {
                outType = ScriptValueType::Vec4;
                outHint = ScriptPropertyEditorHint::Color4;
                return true;
            }

            return false;
        }

        bool NormalizeValueForTypeHint(
            const ScriptValueType expectedType,
            const ScriptPropertyEditorHint editorHint,
            ScriptValue& value)
        {
            if (editorHint == ScriptPropertyEditorHint::Color3 && value.type == ScriptValueType::Vec3)
            {
                return true;
            }
            if (editorHint == ScriptPropertyEditorHint::Color4 && value.type == ScriptValueType::Vec4)
            {
                return true;
            }
            if (editorHint == ScriptPropertyEditorHint::Enum && value.type == ScriptValueType::String)
            {
                return true;
            }
            if (editorHint == ScriptPropertyEditorHint::AssetReference && value.type == ScriptValueType::String)
            {
                return true;
            }
            if (editorHint == ScriptPropertyEditorHint::EntityReference)
            {
                if (value.type == ScriptValueType::Entity)
                {
                    return true;
                }
                if (value.type == ScriptValueType::Int)
                {
                    value.type = ScriptValueType::Entity;
                    value.entityValue = static_cast<UUID>(std::max(0, value.intValue));
                    value.intValue = 0;
                    return true;
                }
            }
            if (value.type == expectedType)
            {
                return true;
            }

            if (expectedType == ScriptValueType::Float && value.type == ScriptValueType::Int)
            {
                value.type = ScriptValueType::Float;
                value.floatValue = static_cast<float>(value.intValue);
                value.intValue = 0;
                return true;
            }

            if (expectedType == ScriptValueType::Int && value.type == ScriptValueType::Float)
            {
                value.type = ScriptValueType::Int;
                value.intValue = static_cast<int>(value.floatValue);
                value.floatValue = 0.0f;
                return true;
            }

            return false;
        }

        bool LooksLikeDescriptorTable(lua_State* state, const int index)
        {
            return HasField(state, index, "default") ||
                HasField(state, index, "type") ||
                HasField(state, index, "tooltip") ||
                HasField(state, index, "min") ||
                HasField(state, index, "max") ||
                HasField(state, index, "options") ||
                HasField(state, index, "assetType");
        }

        bool TryReadScriptValue(lua_State* state, const int index, ScriptValue& outValue)
        {
            switch (lua_type(state, index))
            {
            case LUA_TBOOLEAN:
                outValue = {};
                outValue.type = ScriptValueType::Bool;
                outValue.boolValue = lua_toboolean(state, index) != 0;
                return true;

            case LUA_TNUMBER:
                outValue = {};
                if (lua_isinteger(state, index))
                {
                    outValue.type = ScriptValueType::Int;
                    outValue.intValue = static_cast<int>(lua_tointeger(state, index));
                }
                else
                {
                    outValue.type = ScriptValueType::Float;
                    outValue.floatValue = static_cast<float>(lua_tonumber(state, index));
                }
                return true;

            case LUA_TSTRING:
                outValue = {};
                outValue.type = ScriptValueType::String;
                outValue.stringValue = lua_tostring(state, index);
                return true;

            case LUA_TTABLE:
            {
                ScriptValue parsedValue {};
                if (TryReadArrayTable(state, index, 2, parsedValue.vec2Value.data()))
                {
                    parsedValue.type = ScriptValueType::Vec2;
                    outValue = std::move(parsedValue);
                    return true;
                }

                if (TryReadArrayTable(state, index, 3, parsedValue.vec3Value.data()))
                {
                    parsedValue.type = ScriptValueType::Vec3;
                    outValue = std::move(parsedValue);
                    return true;
                }

                if (TryReadArrayTable(state, index, 4, parsedValue.vec4Value.data()))
                {
                    parsedValue.type = ScriptValueType::Vec4;
                    outValue = std::move(parsedValue);
                    return true;
                }

                return false;
            }

            default:
                return false;
            }
        }

        bool TryReadPropertyDescriptor(lua_State* state, const int index, ScriptPropertyInfo& outProperty)
        {
            if (!lua_istable(state, index))
            {
                return false;
            }

            if (!LooksLikeDescriptorTable(state, index))
            {
                return false;
            }

            const int absoluteIndex = lua_absindex(state, index);

            ScriptPropertyEditorHint editorHint = ScriptPropertyEditorHint::Default;
            ScriptValueType hintedType = ScriptValueType::None;
            std::string typeName;
            if (TryReadStringField(state, absoluteIndex, "type", typeName))
            {
                if (!ParseScriptValueTypeHint(typeName, hintedType, editorHint))
                {
                    return false;
                }
            }

            if (!HasField(state, absoluteIndex, "default"))
            {
                return false;
            }

            lua_getfield(state, absoluteIndex, "default");
            ScriptValue defaultValue {};
            const bool readDefault = TryReadScriptValue(state, -1, defaultValue);
            lua_pop(state, 1);
            if (!readDefault)
            {
                return false;
            }

            if (hintedType != ScriptValueType::None &&
                !NormalizeValueForTypeHint(hintedType, editorHint, defaultValue))
            {
                return false;
            }

            outProperty.defaultValue = std::move(defaultValue);
            outProperty.editorHint = editorHint;
            TryReadStringField(state, absoluteIndex, "tooltip", outProperty.tooltip);
            outProperty.hasMinValue = TryReadNumberField(state, absoluteIndex, "min", outProperty.minValue);
            outProperty.hasMaxValue = TryReadNumberField(state, absoluteIndex, "max", outProperty.maxValue);
            TryReadStringArrayField(state, absoluteIndex, "options", outProperty.options);
            TryReadStringField(state, absoluteIndex, "assetType", outProperty.assetTypeFilter);

            if (outProperty.editorHint == ScriptPropertyEditorHint::Enum && outProperty.options.empty())
            {
                return false;
            }
            return true;
        }

        bool ReadPropertiesTable(
            lua_State* state,
            const int propertiesIndex,
            LuaScriptAssetMetadata& outMetadata,
            const std::string_view pathPrefix = {})
        {
            const int absoluteIndex = lua_absindex(state, propertiesIndex);
            lua_pushnil(state);
            while (lua_next(state, absoluteIndex) != 0)
            {
                if (lua_type(state, -2) == LUA_TSTRING)
                {
                    ScriptPropertyInfo propertyInfo {};
                    const std::string propertyName = lua_tostring(state, -2);
                    propertyInfo.name = pathPrefix.empty()
                        ? propertyName
                        : std::string(pathPrefix) + "." + propertyName;

                    if (TryReadPropertyDescriptor(state, -1, propertyInfo))
                    {
                        outMetadata.properties.push_back(std::move(propertyInfo));
                    }
                    else
                    {
                        ScriptValue defaultValue {};
                        if (TryReadScriptValue(state, -1, defaultValue))
                        {
                            propertyInfo.defaultValue = std::move(defaultValue);
                            outMetadata.properties.push_back(std::move(propertyInfo));
                        }
                        else if (lua_istable(state, -1))
                        {
                            ReadPropertiesTable(state, -1, outMetadata, propertyInfo.name);
                        }
                    }
                }

                lua_pop(state, 1);
            }

            return true;
        }

        bool LoadScriptMetadata(lua_State* state, const std::filesystem::path& sourcePath, LuaScriptAssetMetadata& outMetadata, std::string& outError)
        {
            outMetadata = {};
            outError.clear();

            const int stackBase = lua_gettop(state);
            const int tracebackIndex = LuaHelpers::PushTraceback(state);
            const std::string sourcePathString = sourcePath.string();

            if (luaL_loadfile(state, sourcePathString.c_str()) != LUA_OK)
            {
                outError = "Failed to load Lua script '" + sourcePathString + "': " + LuaHelpers::PopError(state);
                lua_settop(state, stackBase);
                return false;
            }

            if (lua_pcall(state, 0, 1, tracebackIndex) != LUA_OK)
            {
                outError = "Failed to execute Lua script '" + sourcePathString + "': " + LuaHelpers::PopError(state);
                lua_settop(state, stackBase);
                return false;
            }

            if (!lua_istable(state, -1))
            {
                outError = "Lua script must return a table: " + sourcePathString;
                lua_settop(state, stackBase);
                return false;
            }

            lua_getfield(state, -1, "Properties");
            if (lua_istable(state, -1))
            {
                ReadPropertiesTable(state, -1, outMetadata);
            }
            lua_pop(state, 1);

            lua_settop(state, stackBase);
            return true;
        }
    }

    std::unique_ptr<LuaState> ScriptEngine::s_State;
    std::unordered_map<std::string, ScriptEngine::CachedScriptMetadata> ScriptEngine::s_MetadataCache;

    bool ScriptEngine::Initialize()
    {
        if (s_State != nullptr)
        {
            return true;
        }

        s_State = std::make_unique<LuaState>();
        if (!s_State->Initialize())
        {
            s_State.reset();
            return false;
        }

        return true;
    }

    void ScriptEngine::Shutdown()
    {
        if (s_State == nullptr)
        {
            return;
        }

        ClearScriptMetadata();
        s_State->Shutdown();
        s_State.reset();
    }

    bool ScriptEngine::IsInitialized()
    {
        return s_State != nullptr && s_State->IsInitialized();
    }

    void ScriptEngine::RefreshPackagePaths()
    {
        if (s_State == nullptr)
        {
            return;
        }

        s_State->ConfigurePackagePaths();
    }

    bool ScriptEngine::ExecuteFile(const std::filesystem::path& path, std::string* outError)
    {
        if (s_State == nullptr)
        {
            const std::string error = "ScriptEngine is not initialized.";
            if (outError != nullptr)
            {
                *outError = error;
            }
            LUMA_LOG_ERROR("Script", error);
            return false;
        }

        return s_State->ExecuteFile(path, outError);
    }

    bool ScriptEngine::ExecuteString(const std::string_view code, const std::string_view chunkName, std::string* outError)
    {
        if (s_State == nullptr)
        {
            const std::string error = "ScriptEngine is not initialized.";
            if (outError != nullptr)
            {
                *outError = error;
            }
            LUMA_LOG_ERROR("Script", error);
            return false;
        }

        return s_State->ExecuteString(code, chunkName, outError);
    }

    bool ScriptEngine::ResolveScriptSourcePath(
        const std::filesystem::path& assetPath,
        std::filesystem::path& outSourcePath,
        std::string* outError)
    {
        outSourcePath.clear();
        if (outError != nullptr)
        {
            outError->clear();
        }

        if (assetPath.empty())
        {
            if (outError != nullptr)
            {
                *outError = "Lua script asset path is empty.";
            }
            return false;
        }

        std::filesystem::path resolvedPath = assetPath;
        if (!resolvedPath.is_absolute())
        {
            resolvedPath = Project::IsLoaded()
                ? (Project::GetProjectRoot() / resolvedPath)
                : (std::filesystem::current_path() / resolvedPath);
        }
        resolvedPath = resolvedPath.lexically_normal();

        const std::string extension = ToLowerString(resolvedPath.extension().string());
        if (extension == ".lua")
        {
            if (!std::filesystem::exists(resolvedPath))
            {
                if (outError != nullptr)
                {
                    *outError = "Lua script file does not exist: " + resolvedPath.string();
                }
                return false;
            }

            outSourcePath = resolvedPath;
            return true;
        }

        if (extension == ".lumascript")
        {
            Assets::AssetMeta meta {};
            std::string metaError;
            if (!Assets::ReadMetaFile(resolvedPath.string() + ".meta", meta, metaError))
            {
                if (outError != nullptr)
                {
                    *outError = "Failed to read Lua script asset metadata: " + metaError;
                }
                return false;
            }

            if (meta.sourcePaths.empty())
            {
                if (outError != nullptr)
                {
                    *outError = "Lua script asset has no source path: " + resolvedPath.string();
                }
                return false;
            }

            std::filesystem::path sourcePath = meta.sourcePaths.front();
            if (!sourcePath.is_absolute())
            {
                sourcePath = Project::IsLoaded()
                    ? (Project::GetProjectRoot() / sourcePath)
                    : (std::filesystem::current_path() / sourcePath);
            }
            sourcePath = sourcePath.lexically_normal();
            if (!std::filesystem::exists(sourcePath))
            {
                if (outError != nullptr)
                {
                    *outError = "Lua script source file does not exist: " + sourcePath.string();
                }
                return false;
            }

            outSourcePath = sourcePath;
            return true;
        }

        if (outError != nullptr)
        {
            *outError = "Unsupported Lua script reference: " + assetPath.string();
        }
        return false;
    }

    const LuaScriptAssetMetadata* ScriptEngine::GetScriptMetadata(
        const std::filesystem::path& assetPath,
        std::string* outError)
    {
        if (outError != nullptr)
        {
            outError->clear();
        }

        lua_State* state = GetLuaState();
        if (state == nullptr)
        {
            if (outError != nullptr)
            {
                *outError = "ScriptEngine is not initialized.";
            }
            return nullptr;
        }

        const std::string cacheKey = NormalizePathKey(assetPath);
        if (const auto cached = s_MetadataCache.find(cacheKey); cached != s_MetadataCache.end())
        {
            bool cacheStale = false;
            if (!cached->second.sourcePath.empty() && cached->second.hasWriteTime)
            {
                std::filesystem::file_time_type currentWriteTime {};
                if (TryGetFileWriteTime(cached->second.sourcePath, currentWriteTime))
                {
                    cacheStale = currentWriteTime != cached->second.lastWriteTime;
                }
                else
                {
                    cacheStale = true;
                }
            }

            if (!cacheStale)
            {
                if (!cached->second.loaded)
                {
                    if (outError != nullptr)
                    {
                        *outError = cached->second.error;
                    }
                    return nullptr;
                }

                return &cached->second.metadata;
            }

            s_MetadataCache.erase(cached);
        }

        CachedScriptMetadata cachedMetadata {};
        std::filesystem::path sourcePath;
        if (!ResolveScriptSourcePath(assetPath, sourcePath, &cachedMetadata.error))
        {
            if (outError != nullptr)
            {
                *outError = cachedMetadata.error;
            }
            s_MetadataCache.emplace(cacheKey, cachedMetadata);
            return nullptr;
        }

        cachedMetadata.sourcePath = sourcePath;
        cachedMetadata.hasWriteTime = TryGetFileWriteTime(sourcePath, cachedMetadata.lastWriteTime);

        cachedMetadata.loaded = LoadScriptMetadata(state, sourcePath, cachedMetadata.metadata, cachedMetadata.error);
        if (!cachedMetadata.loaded)
        {
            LUMA_LOG_ERROR("Script", cachedMetadata.error);
            if (outError != nullptr)
            {
                *outError = cachedMetadata.error;
            }
        }

        auto [it, inserted] = s_MetadataCache.emplace(cacheKey, std::move(cachedMetadata));
        (void)inserted;
        if (!it->second.loaded)
        {
            return nullptr;
        }

        return &it->second.metadata;
    }

    void ScriptEngine::InvalidateScriptMetadata(const std::filesystem::path& assetPath)
    {
        s_MetadataCache.erase(NormalizePathKey(assetPath));
    }

    void ScriptEngine::ClearScriptMetadata()
    {
        s_MetadataCache.clear();
    }

    lua_State* ScriptEngine::GetLuaState()
    {
        if (s_State == nullptr)
        {
            return nullptr;
        }

        return s_State->GetRawState();
    }
}
