#include "Luma/Scripting/LuaScriptRuntime.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <lua.hpp>

#include "Luma/Audio/Core/AudioSystem.h"
#include "Luma/Core/App/Project.h"
#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Core/Foundation/Time.h"
#include "Luma/Input/Input.h"
#include "Luma/Scene/AudioListenerComponent.h"
#include "Luma/Scene/AudioSourceComponent.h"
#include "Luma/Scene/CameraComponent.h"
#include "Luma/Scene/ColliderComponent.h"
#include "Luma/Scene/DirectionalLightComponent.h"
#include "Luma/Scene/LuaScriptComponent.h"
#include "Luma/Scene/MaterialComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/PhysicsEventsComponent.h"
#include "Luma/Scene/PointLightComponent.h"
#include "Luma/Scene/RelationshipComponent.h"
#include "Luma/Scene/RigidBodyComponent.h"
#include "Luma/Scene/SpotLightComponent.h"
#include "Luma/Scene/TagComponent.h"
#include "Luma/Scene/TransformComponent.h"
#include "Luma/Scripting/ScriptEngine.h"
#include "Luma/Scripting/Lua/LuaHelpers.h"

namespace Luma
{
    namespace
    {
        Scene* g_ActiveScriptScene = nullptr;

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

        bool TryGetFileWriteTime(
            const std::filesystem::path& path,
            std::filesystem::file_time_type& outWriteTime)
        {
            std::error_code errorCode;
            outWriteTime = std::filesystem::last_write_time(path, errorCode);
            return !errorCode;
        }

        std::string NormalizeSlashes(std::string value)
        {
            std::replace(value.begin(), value.end(), '\\', '/');
            return value;
        }

        std::string TrimWhitespace(std::string value)
        {
            auto notWhitespace = [](const unsigned char character)
            {
                return !std::isspace(character);
            };

            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notWhitespace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notWhitespace).base(), value.end());
            return value;
        }

        std::string MakeDisplayScriptPath(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return {};
            }

            std::error_code errorCode;
            const std::filesystem::path normalizedPath = path.lexically_normal();
            if (Project::IsLoaded())
            {
                const std::filesystem::path& projectRoot = Project::GetProjectRoot();
                if (!projectRoot.empty())
                {
                    const std::filesystem::path relative = std::filesystem::relative(normalizedPath, projectRoot, errorCode);
                    if (!errorCode)
                    {
                        return relative.generic_string();
                    }
                }
            }

            return normalizedPath.generic_string();
        }

        std::string ExtractLuaErrorHeadline(
            const std::string_view rawError,
            const std::filesystem::path& sourcePath)
        {
            std::string headline(rawError.substr(0, rawError.find('\n')));
            headline = TrimWhitespace(std::move(headline));
            if (headline.empty())
            {
                return "Unknown Lua error.";
            }

            headline = NormalizeSlashes(std::move(headline));
            const std::string sourcePathText = NormalizeSlashes(sourcePath.lexically_normal().generic_string());
            const std::string displayPath = NormalizeSlashes(MakeDisplayScriptPath(sourcePath));

            if (!sourcePathText.empty())
            {
                std::size_t searchIndex = 0;
                while ((searchIndex = headline.find(sourcePathText, searchIndex)) != std::string::npos)
                {
                    headline.replace(searchIndex, sourcePathText.size(), displayPath);
                    searchIndex += displayPath.size();
                }
            }

            return headline;
        }

        std::string GetEntityDebugLabel(Scene& scene, const EntityID entity)
        {
            auto& registry = scene.GetRegistry();
            if (registry.valid(entity) && registry.all_of<TagComponent>(entity))
            {
                const std::string& name = registry.get<TagComponent>(entity).name;
                if (!name.empty())
                {
                    return name;
                }
            }

            return "Entity";
        }

        std::string FormatRuntimeScriptError(
            Scene& scene,
            const EntityID entity,
            const std::filesystem::path& sourcePath,
            const std::string_view callbackName,
            const std::string_view rawError)
        {
            std::string message = "Lua script fault | Entity: ";
            message += GetEntityDebugLabel(scene, entity);
            message += " | Callback: ";
            message += callbackName;
            message += " | Script: ";
            message += MakeDisplayScriptPath(sourcePath);
            message += " | ";
            message += ExtractLuaErrorHeadline(rawError, sourcePath);
            return message;
        }

        std::string FormatScriptLoadError(
            const std::filesystem::path& sourcePath,
            const std::string_view stage,
            const std::string_view rawError)
        {
            std::string message = "Lua script ";
            message += stage;
            message += " failed | Script: ";
            message += MakeDisplayScriptPath(sourcePath);
            message += " | ";
            message += ExtractLuaErrorHeadline(rawError, sourcePath);
            return message;
        }

        bool TryReadVec3Argument(lua_State* state, const int startIndex, std::array<float, 3>& outValue)
        {
            if (lua_istable(state, startIndex))
            {
                lua_getfield(state, startIndex, "x");
                lua_getfield(state, startIndex, "y");
                lua_getfield(state, startIndex, "z");
                if (!lua_isnumber(state, -3) || !lua_isnumber(state, -2) || !lua_isnumber(state, -1))
                {
                    lua_pop(state, 3);
                    return false;
                }

                outValue[0] = static_cast<float>(lua_tonumber(state, -3));
                outValue[1] = static_cast<float>(lua_tonumber(state, -2));
                outValue[2] = static_cast<float>(lua_tonumber(state, -1));
                lua_pop(state, 3);
                return true;
            }

            if (!lua_isnumber(state, startIndex) ||
                !lua_isnumber(state, startIndex + 1) ||
                !lua_isnumber(state, startIndex + 2))
            {
                return false;
            }

            outValue[0] = static_cast<float>(lua_tonumber(state, startIndex));
            outValue[1] = static_cast<float>(lua_tonumber(state, startIndex + 1));
            outValue[2] = static_cast<float>(lua_tonumber(state, startIndex + 2));
            return true;
        }

        bool TryReadColorArgument(lua_State* state, const int startIndex, std::array<float, 4>& outValue)
        {
            if (lua_istable(state, startIndex))
            {
                auto tryFieldSet = [&](const char* x, const char* y, const char* z, const char* w) -> bool
                {
                    lua_getfield(state, startIndex, x);
                    lua_getfield(state, startIndex, y);
                    lua_getfield(state, startIndex, z);
                    lua_getfield(state, startIndex, w);
                    const bool valid =
                        lua_isnumber(state, -4) && lua_isnumber(state, -3) && lua_isnumber(state, -2) &&
                        (lua_isnumber(state, -1) || lua_isnil(state, -1));
                    if (valid)
                    {
                        outValue[0] = static_cast<float>(lua_tonumber(state, -4));
                        outValue[1] = static_cast<float>(lua_tonumber(state, -3));
                        outValue[2] = static_cast<float>(lua_tonumber(state, -2));
                        outValue[3] = lua_isnumber(state, -1) ? static_cast<float>(lua_tonumber(state, -1)) : 1.0f;
                    }
                    lua_pop(state, 4);
                    return valid;
                };

                if (tryFieldSet("r", "g", "b", "a") || tryFieldSet("x", "y", "z", "w"))
                {
                    return true;
                }

                lua_rawgeti(state, startIndex, 1);
                lua_rawgeti(state, startIndex, 2);
                lua_rawgeti(state, startIndex, 3);
                lua_rawgeti(state, startIndex, 4);
                const bool valid =
                    lua_isnumber(state, -4) && lua_isnumber(state, -3) && lua_isnumber(state, -2) &&
                    (lua_isnumber(state, -1) || lua_isnil(state, -1));
                if (valid)
                {
                    outValue[0] = static_cast<float>(lua_tonumber(state, -4));
                    outValue[1] = static_cast<float>(lua_tonumber(state, -3));
                    outValue[2] = static_cast<float>(lua_tonumber(state, -2));
                    outValue[3] = lua_isnumber(state, -1) ? static_cast<float>(lua_tonumber(state, -1)) : 1.0f;
                }
                lua_pop(state, 4);
                return valid;
            }

            if (!lua_isnumber(state, startIndex) ||
                !lua_isnumber(state, startIndex + 1) ||
                !lua_isnumber(state, startIndex + 2))
            {
                return false;
            }

            outValue[0] = static_cast<float>(lua_tonumber(state, startIndex));
            outValue[1] = static_cast<float>(lua_tonumber(state, startIndex + 1));
            outValue[2] = static_cast<float>(lua_tonumber(state, startIndex + 2));
            outValue[3] = lua_isnumber(state, startIndex + 3) ? static_cast<float>(lua_tonumber(state, startIndex + 3)) : 1.0f;
            return true;
        }

        bool TryReadAudioPlaySettings(lua_State* state, const int index, Audio::PlaySettings& outSettings)
        {
            if (!lua_istable(state, index))
            {
                return false;
            }

            const int absoluteIndex = lua_absindex(state, index);
            auto tryReadNumber = [&](const char* fieldName, float& target)
            {
                lua_getfield(state, absoluteIndex, fieldName);
                if (lua_isnumber(state, -1))
                {
                    target = static_cast<float>(lua_tonumber(state, -1));
                }
                lua_pop(state, 1);
            };
            auto tryReadBool = [&](const char* fieldName, bool& target)
            {
                lua_getfield(state, absoluteIndex, fieldName);
                if (lua_isboolean(state, -1))
                {
                    target = lua_toboolean(state, -1) != 0;
                }
                lua_pop(state, 1);
            };

            tryReadNumber("volume", outSettings.volume);
            tryReadNumber("pitch", outSettings.pitch);
            tryReadBool("looping", outSettings.looping);
            tryReadBool("spatialized", outSettings.spatialized);
            tryReadNumber("minDistance", outSettings.minDistance);
            tryReadNumber("maxDistance", outSettings.maxDistance);
            return true;
        }

        void PushVec3(lua_State* state, const std::array<float, 3>& value)
        {
            lua_createtable(state, 3, 6);
            lua_pushnumber(state, value[0]);
            lua_rawseti(state, -2, 1);
            lua_pushnumber(state, value[1]);
            lua_rawseti(state, -2, 2);
            lua_pushnumber(state, value[2]);
            lua_rawseti(state, -2, 3);
            lua_pushnumber(state, value[0]);
            lua_setfield(state, -2, "x");
            lua_pushnumber(state, value[1]);
            lua_setfield(state, -2, "y");
            lua_pushnumber(state, value[2]);
            lua_setfield(state, -2, "z");
            lua_pushnumber(state, value[0]);
            lua_setfield(state, -2, "r");
            lua_pushnumber(state, value[1]);
            lua_setfield(state, -2, "g");
            lua_pushnumber(state, value[2]);
            lua_setfield(state, -2, "b");
        }

        void PushVec4(lua_State* state, const std::array<float, 4>& value)
        {
            lua_createtable(state, 4, 8);
            lua_pushnumber(state, value[0]);
            lua_rawseti(state, -2, 1);
            lua_pushnumber(state, value[1]);
            lua_rawseti(state, -2, 2);
            lua_pushnumber(state, value[2]);
            lua_rawseti(state, -2, 3);
            lua_pushnumber(state, value[3]);
            lua_rawseti(state, -2, 4);
            lua_pushnumber(state, value[0]);
            lua_setfield(state, -2, "x");
            lua_pushnumber(state, value[1]);
            lua_setfield(state, -2, "y");
            lua_pushnumber(state, value[2]);
            lua_setfield(state, -2, "z");
            lua_pushnumber(state, value[3]);
            lua_setfield(state, -2, "w");
            lua_pushnumber(state, value[0]);
            lua_setfield(state, -2, "r");
            lua_pushnumber(state, value[1]);
            lua_setfield(state, -2, "g");
            lua_pushnumber(state, value[2]);
            lua_setfield(state, -2, "b");
            lua_pushnumber(state, value[3]);
            lua_setfield(state, -2, "a");
        }

        void PushScriptValue(lua_State* state, const ScriptValue& value)
        {
            switch (value.type)
            {
            case ScriptValueType::Bool:
                lua_pushboolean(state, value.boolValue ? 1 : 0);
                break;

            case ScriptValueType::Int:
                lua_pushinteger(state, static_cast<lua_Integer>(value.intValue));
                break;

            case ScriptValueType::Float:
                lua_pushnumber(state, value.floatValue);
                break;

            case ScriptValueType::String:
                lua_pushlstring(state, value.stringValue.c_str(), value.stringValue.size());
                break;

            case ScriptValueType::Entity:
                if (g_ActiveScriptScene != nullptr)
                {
                    const EntityID resolvedEntity = g_ActiveScriptScene->FindByUUID(value.entityValue);
                    if (resolvedEntity != entt::null)
                    {
                        lua_pushinteger(state, static_cast<lua_Integer>(static_cast<entt::id_type>(resolvedEntity)));
                    }
                    else
                    {
                        lua_pushnil(state);
                    }
                }
                else
                {
                    lua_pushnil(state);
                }
                break;

            case ScriptValueType::Vec2:
                lua_createtable(state, 2, 0);
                lua_pushnumber(state, value.vec2Value[0]);
                lua_rawseti(state, -2, 1);
                lua_pushnumber(state, value.vec2Value[1]);
                lua_rawseti(state, -2, 2);
                break;

            case ScriptValueType::Vec3:
                lua_createtable(state, 3, 0);
                lua_pushnumber(state, value.vec3Value[0]);
                lua_rawseti(state, -2, 1);
                lua_pushnumber(state, value.vec3Value[1]);
                lua_rawseti(state, -2, 2);
                lua_pushnumber(state, value.vec3Value[2]);
                lua_rawseti(state, -2, 3);
                break;

            case ScriptValueType::Vec4:
                lua_createtable(state, 4, 0);
                lua_pushnumber(state, value.vec4Value[0]);
                lua_rawseti(state, -2, 1);
                lua_pushnumber(state, value.vec4Value[1]);
                lua_rawseti(state, -2, 2);
                lua_pushnumber(state, value.vec4Value[2]);
                lua_rawseti(state, -2, 3);
                lua_pushnumber(state, value.vec4Value[3]);
                lua_rawseti(state, -2, 4);
                break;

            case ScriptValueType::None:
            default:
                lua_pushnil(state);
                break;
            }
        }

        ScriptValue ResolvePropertyValue(const ScriptPropertyInfo& property, const LuaScriptComponent& component)
        {
            if (const auto overrideIt = component.propertyOverrides.find(property.name);
                overrideIt != component.propertyOverrides.end() && ScriptValueTypeMatches(overrideIt->second, property.defaultValue))
            {
                return overrideIt->second;
            }

            return property.defaultValue;
        }

        void SetNestedPropertyField(
            lua_State* state,
            const int tableIndex,
            const std::string_view propertyPath,
            const ScriptValue& value)
        {
            const int absoluteTableIndex = lua_absindex(state, tableIndex);
            std::size_t segmentStart = 0;
            int currentTableIndex = absoluteTableIndex;

            while (segmentStart < propertyPath.size())
            {
                const std::size_t dotIndex = propertyPath.find('.', segmentStart);
                const bool lastSegment = dotIndex == std::string::npos;
                const std::string segment = std::string(propertyPath.substr(
                    segmentStart,
                    lastSegment ? std::string_view::npos : dotIndex - segmentStart));
                if (segment.empty())
                {
                    break;
                }

                if (lastSegment)
                {
                    PushScriptValue(state, value);
                    lua_setfield(state, currentTableIndex, segment.c_str());
                    break;
                }

                lua_getfield(state, currentTableIndex, segment.c_str());
                if (!lua_istable(state, -1))
                {
                    lua_pop(state, 1);
                    lua_newtable(state);
                    lua_pushvalue(state, -1);
                    lua_setfield(state, currentTableIndex, segment.c_str());
                }

                currentTableIndex = lua_absindex(state, -1);
                segmentStart = dotIndex + 1;
            }

            while (lua_gettop(state) > absoluteTableIndex)
            {
                lua_pop(state, 1);
            }
        }

        void ApplyResolvedPropertiesToInstanceTable(
            lua_State* state,
            const int instanceIndex,
            const LuaScriptAssetMetadata& metadata,
            const LuaScriptComponent& component)
        {
            const int absoluteInstanceIndex = lua_absindex(state, instanceIndex);

            lua_getfield(state, absoluteInstanceIndex, "Properties");
            if (!lua_istable(state, -1))
            {
                lua_pop(state, 1);
                lua_newtable(state);
            }

            const int propertiesIndex = lua_gettop(state);
            for (const ScriptPropertyInfo& property : metadata.properties)
            {
                SetNestedPropertyField(state, propertiesIndex, property.name, ResolvePropertyValue(property, component));
            }

            lua_setfield(state, absoluteInstanceIndex, "Properties");
        }

        bool TryGetEntityFromLua(lua_State* state, const int index, EntityID& outEntity)
        {
            if (!lua_isinteger(state, index))
            {
                return false;
            }

            outEntity = static_cast<EntityID>(static_cast<entt::id_type>(lua_tointeger(state, index)));
            return true;
        }

        bool IsSceneEntityValid(const EntityID entity)
        {
            return g_ActiveScriptScene != nullptr && g_ActiveScriptScene->GetRegistry().valid(entity);
        }

        void PushEntityArray(lua_State* state, const std::vector<EntityID>& entities)
        {
            lua_createtable(state, static_cast<int>(entities.size()), 0);
            int arrayIndex = 1;
            for (const EntityID entity : entities)
            {
                lua_pushinteger(state, static_cast<lua_Integer>(static_cast<entt::id_type>(entity)));
                lua_rawseti(state, -2, arrayIndex++);
            }
        }

        std::optional<PrimitiveType> ParsePrimitiveType(const std::string_view value)
        {
            const std::string lowered = ToLowerString(std::string(value));
            if (lowered == "cube" || lowered == "box")
            {
                return PrimitiveType::Cube;
            }
            if (lowered == "sphere")
            {
                return PrimitiveType::Sphere;
            }
            if (lowered == "plane")
            {
                return PrimitiveType::Plane;
            }
            if (lowered == "cylinder")
            {
                return PrimitiveType::Cylinder;
            }
            if (lowered == "capsule")
            {
                return PrimitiveType::Capsule;
            }
            if (lowered == "cone")
            {
                return PrimitiveType::Cone;
            }
            if (lowered == "torus")
            {
                return PrimitiveType::Torus;
            }
            return std::nullopt;
        }

        const char* PrimitiveTypeName(const PrimitiveType primitive)
        {
            switch (primitive)
            {
            case PrimitiveType::Cube:
                return "Cube";
            case PrimitiveType::Sphere:
                return "Sphere";
            case PrimitiveType::Plane:
                return "Plane";
            case PrimitiveType::Cylinder:
                return "Cylinder";
            case PrimitiveType::Capsule:
                return "Capsule";
            case PrimitiveType::Cone:
                return "Cone";
            case PrimitiveType::Torus:
                return "Torus";
            default:
                return "Unknown";
            }
        }

        std::optional<RigidBodyType> ParseRigidBodyType(const std::string_view value)
        {
            const std::string lowered = ToLowerString(std::string(value));
            if (lowered == "static")
            {
                return RigidBodyType::Static;
            }
            if (lowered == "dynamic")
            {
                return RigidBodyType::Dynamic;
            }
            if (lowered == "kinematic")
            {
                return RigidBodyType::Kinematic;
            }
            return std::nullopt;
        }

        const char* RigidBodyTypeName(const RigidBodyType type)
        {
            switch (type)
            {
            case RigidBodyType::Static:
                return "Static";
            case RigidBodyType::Dynamic:
                return "Dynamic";
            case RigidBodyType::Kinematic:
                return "Kinematic";
            default:
                return "Unknown";
            }
        }

        std::optional<ColliderShapeType> ParseColliderShapeType(const std::string_view value)
        {
            const std::string lowered = ToLowerString(std::string(value));
            if (lowered == "box")
            {
                return ColliderShapeType::Box;
            }
            if (lowered == "sphere")
            {
                return ColliderShapeType::Sphere;
            }
            if (lowered == "capsule")
            {
                return ColliderShapeType::Capsule;
            }
            if (lowered == "mesh")
            {
                return ColliderShapeType::Mesh;
            }
            if (lowered == "cylinder")
            {
                return ColliderShapeType::Cylinder;
            }
            return std::nullopt;
        }

        const char* ColliderShapeTypeName(const ColliderShapeType shape)
        {
            switch (shape)
            {
            case ColliderShapeType::Box:
                return "Box";
            case ColliderShapeType::Sphere:
                return "Sphere";
            case ColliderShapeType::Capsule:
                return "Capsule";
            case ColliderShapeType::Mesh:
                return "Mesh";
            case ColliderShapeType::Cylinder:
                return "Cylinder";
            default:
                return "Unknown";
            }
        }

        std::optional<KeyCode> ParseKeyCode(const std::string_view value)
        {
            const std::string lowered = ToLowerString(std::string(value));
            if (lowered == "w")
            {
                return KeyCode::W;
            }
            if (lowered == "a")
            {
                return KeyCode::A;
            }
            if (lowered == "s")
            {
                return KeyCode::S;
            }
            if (lowered == "d")
            {
                return KeyCode::D;
            }
            if (lowered == "q")
            {
                return KeyCode::Q;
            }
            if (lowered == "e")
            {
                return KeyCode::E;
            }
            if (lowered == "space")
            {
                return KeyCode::Space;
            }
            if (lowered == "enter")
            {
                return KeyCode::Enter;
            }
            if (lowered == "escape")
            {
                return KeyCode::Escape;
            }
            if (lowered == "leftshift" || lowered == "shift")
            {
                return KeyCode::LeftShift;
            }
            if (lowered == "rightshift")
            {
                return KeyCode::RightShift;
            }
            if (lowered == "c")
            {
                return KeyCode::C;
            }
            if (lowered == "r")
            {
                return KeyCode::R;
            }
            return std::nullopt;
        }

        int LuaTimeGetDeltaSeconds(lua_State* state)
        {
            lua_pushnumber(state, Time::GetDeltaSeconds());
            return 1;
        }

        int LuaTimeGetElapsedSeconds(lua_State* state)
        {
            lua_pushnumber(state, Time::GetElapsedSeconds());
            return 1;
        }

        int LuaInputIsKeyDown(lua_State* state)
        {
            const std::string key = LuaHelpers::ToString(state, 1);
            if (const auto parsedKey = ParseKeyCode(key))
            {
                lua_pushboolean(state, Input::IsKeyDown(*parsedKey));
                return 1;
            }
            lua_pushboolean(state, 0);
            return 1;
        }

        int LuaInputWasKeyPressed(lua_State* state)
        {
            const std::string key = LuaHelpers::ToString(state, 1);
            if (const auto parsedKey = ParseKeyCode(key))
            {
                lua_pushboolean(state, Input::WasKeyPressed(*parsedKey));
                return 1;
            }
            lua_pushboolean(state, 0);
            return 1;
        }

        int LuaEntityIsValid(lua_State* state)
        {
            EntityID entity = entt::null;
            lua_pushboolean(state, TryGetEntityFromLua(state, 1, entity) && IsSceneEntityValid(entity));
            return 1;
        }

        int LuaEntityGetName(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushliteral(state, "");
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<TagComponent>(entity))
            {
                lua_pushliteral(state, "");
                return 1;
            }

            const std::string& name = registry.get<TagComponent>(entity).name;
            lua_pushlstring(state, name.c_str(), name.size());
            return 1;
        }

        int LuaEntitySetName(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<TagComponent>(entity))
            {
                return 0;
            }

            registry.get<TagComponent>(entity).name = LuaHelpers::ToString(state, 2);
            return 0;
        }

        int LuaEntityGetTag(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushliteral(state, "");
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<TagComponent>(entity))
            {
                lua_pushliteral(state, "");
                return 1;
            }

            const std::string& tag = registry.get<TagComponent>(entity).tag;
            lua_pushlstring(state, tag.c_str(), tag.size());
            return 1;
        }

        int LuaEntityFindByName(lua_State* state)
        {
            const std::string targetName = LuaHelpers::ToString(state, 1);
            if (g_ActiveScriptScene == nullptr || targetName.empty())
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            const auto view = registry.view<TagComponent>();
            for (const EntityID entity : view)
            {
                if (view.get<TagComponent>(entity).name == targetName)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(static_cast<entt::id_type>(entity)));
                    return 1;
                }
            }

            lua_pushnil(state);
            return 1;
        }

        int LuaEntityFindFirstByTag(lua_State* state)
        {
            const std::string targetTag = LuaHelpers::ToString(state, 1);
            if (g_ActiveScriptScene == nullptr || targetTag.empty())
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            const auto view = registry.view<TagComponent>();
            for (const EntityID entity : view)
            {
                if (view.get<TagComponent>(entity).tag == targetTag)
                {
                    lua_pushinteger(state, static_cast<lua_Integer>(static_cast<entt::id_type>(entity)));
                    return 1;
                }
            }

            lua_pushnil(state);
            return 1;
        }

        int LuaEntityHasComponent(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            const std::string componentName = ToLowerString(LuaHelpers::ToString(state, 2));
            auto& registry = g_ActiveScriptScene->GetRegistry();

            bool hasComponent = false;
            if (componentName == "transform")
            {
                hasComponent = registry.all_of<TransformComponent>(entity);
            }
            else if (componentName == "meshrenderer" || componentName == "renderer")
            {
                hasComponent = registry.all_of<MeshRendererComponent>(entity);
            }
            else if (componentName == "material")
            {
                hasComponent = registry.all_of<MaterialComponent>(entity);
            }
            else if (componentName == "camera")
            {
                hasComponent = registry.all_of<CameraComponent>(entity);
            }
            else if (componentName == "luascript" || componentName == "script")
            {
                hasComponent = registry.all_of<LuaScriptComponent>(entity);
            }
            else if (componentName == "rigidbody")
            {
                hasComponent = registry.all_of<RigidBodyComponent>(entity);
            }
            else if (componentName == "collider")
            {
                hasComponent = registry.all_of<ColliderComponent>(entity);
            }
            else if (componentName == "audiosource")
            {
                hasComponent = registry.all_of<AudioSourceComponent>(entity);
            }
            else if (componentName == "audiolistener")
            {
                hasComponent = registry.all_of<AudioListenerComponent>(entity);
            }

            lua_pushboolean(state, hasComponent ? 1 : 0);
            return 1;
        }

        int LuaEntitySetTag(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<TagComponent>(entity))
            {
                return 0;
            }

            registry.get<TagComponent>(entity).tag = LuaHelpers::ToString(state, 2);
            return 0;
        }

        int LuaEntityGetParent(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<RelationshipComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            const EntityID parent = registry.get<RelationshipComponent>(entity).parent;
            if (parent == entt::null || !registry.valid(parent))
            {
                lua_pushnil(state);
                return 1;
            }

            lua_pushinteger(state, static_cast<lua_Integer>(static_cast<entt::id_type>(parent)));
            return 1;
        }

        int LuaEntityGetChildren(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_newtable(state);
                return 1;
            }

            PushEntityArray(state, g_ActiveScriptScene->GetChildren(entity));
            return 1;
        }

        int LuaEntityAddComponent(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            const std::string componentName = ToLowerString(LuaHelpers::ToString(state, 2));
            bool supported = true;

            if (componentName == "meshrenderer" || componentName == "renderer")
            {
                if (!registry.all_of<MeshRendererComponent>(entity))
                {
                    registry.emplace<MeshRendererComponent>(entity);
                }
            }
            else if (componentName == "material")
            {
                if (!registry.all_of<MaterialComponent>(entity))
                {
                    registry.emplace<MaterialComponent>(entity);
                }
            }
            else if (componentName == "camera")
            {
                if (!registry.all_of<CameraComponent>(entity))
                {
                    auto& camera = registry.emplace<CameraComponent>(entity);
                    camera.primary = false;
                }
            }
            else if (componentName == "rigidbody")
            {
                if (!registry.all_of<RigidBodyComponent>(entity))
                {
                    registry.emplace<RigidBodyComponent>(entity);
                }
            }
            else if (componentName == "collider")
            {
                if (!registry.all_of<ColliderComponent>(entity))
                {
                    registry.emplace<ColliderComponent>(entity);
                }
            }
            else if (componentName == "directionallight")
            {
                if (!registry.all_of<DirectionalLightComponent>(entity))
                {
                    registry.emplace<DirectionalLightComponent>(entity);
                }
            }
            else if (componentName == "pointlight")
            {
                if (!registry.all_of<PointLightComponent>(entity))
                {
                    registry.emplace<PointLightComponent>(entity);
                }
            }
            else if (componentName == "spotlight")
            {
                if (!registry.all_of<SpotLightComponent>(entity))
                {
                    registry.emplace<SpotLightComponent>(entity);
                }
            }
            else if (componentName == "audiosource")
            {
                if (!registry.all_of<AudioSourceComponent>(entity))
                {
                    registry.emplace<AudioSourceComponent>(entity);
                }
            }
            else if (componentName == "audiolistener")
            {
                if (!registry.all_of<AudioListenerComponent>(entity))
                {
                    registry.emplace<AudioListenerComponent>(entity);
                }
            }
            else
            {
                supported = false;
            }

            lua_pushboolean(state, supported ? 1 : 0);
            return 1;
        }

        int LuaEntityRemoveComponent(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            const std::string componentName = ToLowerString(LuaHelpers::ToString(state, 2));
            bool removed = false;

            if (componentName == "meshrenderer" || componentName == "renderer")
            {
                removed = registry.remove<MeshRendererComponent>(entity) > 0;
            }
            else if (componentName == "material")
            {
                removed = registry.remove<MaterialComponent>(entity) > 0;
            }
            else if (componentName == "camera")
            {
                removed = registry.remove<CameraComponent>(entity) > 0;
            }
            else if (componentName == "rigidbody")
            {
                removed = registry.remove<RigidBodyComponent>(entity) > 0;
            }
            else if (componentName == "collider")
            {
                removed = registry.remove<ColliderComponent>(entity) > 0;
            }
            else if (componentName == "directionallight")
            {
                removed = registry.remove<DirectionalLightComponent>(entity) > 0;
            }
            else if (componentName == "pointlight")
            {
                removed = registry.remove<PointLightComponent>(entity) > 0;
            }
            else if (componentName == "spotlight")
            {
                removed = registry.remove<SpotLightComponent>(entity) > 0;
            }
            else if (componentName == "audiosource")
            {
                removed = registry.remove<AudioSourceComponent>(entity) > 0;
            }
            else if (componentName == "audiolistener")
            {
                removed = registry.remove<AudioListenerComponent>(entity) > 0;
            }

            lua_pushboolean(state, removed ? 1 : 0);
            return 1;
        }

        int LuaSceneCreateEntity(lua_State* state)
        {
            if (g_ActiveScriptScene == nullptr)
            {
                lua_pushnil(state);
                return 1;
            }

            const std::string name = lua_gettop(state) >= 1 ? LuaHelpers::ToString(state, 1) : std::string("Entity");
            Entity entity = g_ActiveScriptScene->CreateEntity(name);

            if (lua_gettop(state) >= 2)
            {
                EntityID parent = entt::null;
                if (TryGetEntityFromLua(state, 2, parent) && IsSceneEntityValid(parent))
                {
                    g_ActiveScriptScene->SetParent(entity.GetHandle(), parent);
                }
            }

            lua_pushinteger(state, static_cast<lua_Integer>(static_cast<entt::id_type>(entity.GetHandle())));
            return 1;
        }

        int LuaSceneDestroyEntity(lua_State* state)
        {
            EntityID entity = entt::null;
            if (g_ActiveScriptScene == nullptr || !TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            g_ActiveScriptScene->DestroyEntity(entity);
            return 0;
        }

        int LuaSceneSetParent(lua_State* state)
        {
            EntityID child = entt::null;
            EntityID parent = entt::null;
            if (g_ActiveScriptScene == nullptr ||
                !TryGetEntityFromLua(state, 1, child) ||
                !TryGetEntityFromLua(state, 2, parent) ||
                !IsSceneEntityValid(child) ||
                !IsSceneEntityValid(parent))
            {
                return 0;
            }

            g_ActiveScriptScene->SetParent(child, parent);
            return 0;
        }

        int LuaSceneUnparent(lua_State* state)
        {
            EntityID entity = entt::null;
            if (g_ActiveScriptScene == nullptr || !TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            g_ActiveScriptScene->Unparent(entity);
            return 0;
        }

        int LuaSceneGetRootEntities(lua_State* state)
        {
            if (g_ActiveScriptScene == nullptr)
            {
                lua_newtable(state);
                return 1;
            }

            PushEntityArray(state, g_ActiveScriptScene->GetRootEntities());
            return 1;
        }

        int LuaSceneCreatePrimitive(lua_State* state)
        {
            if (g_ActiveScriptScene == nullptr)
            {
                lua_pushnil(state);
                return 1;
            }

            const std::string name = lua_gettop(state) >= 1 ? LuaHelpers::ToString(state, 1) : std::string("Primitive");
            const std::string primitiveName = lua_gettop(state) >= 2 ? LuaHelpers::ToString(state, 2) : std::string("Cube");
            const auto primitive = ParsePrimitiveType(primitiveName);
            if (!primitive.has_value())
            {
                lua_pushnil(state);
                return 1;
            }

            Entity entity = g_ActiveScriptScene->CreateEntity(name);
            auto& registry = g_ActiveScriptScene->GetRegistry();
            auto& renderer = registry.emplace<MeshRendererComponent>(entity.GetHandle());
            renderer.usePrimitive = true;
            renderer.primitive = *primitive;
            registry.emplace<MaterialComponent>(entity.GetHandle());

            if (lua_gettop(state) >= 3)
            {
                EntityID parent = entt::null;
                if (TryGetEntityFromLua(state, 3, parent) && IsSceneEntityValid(parent))
                {
                    g_ActiveScriptScene->SetParent(entity.GetHandle(), parent);
                }
            }

            lua_pushinteger(state, static_cast<lua_Integer>(static_cast<entt::id_type>(entity.GetHandle())));
            return 1;
        }

        int LuaAudioIsInitialized(lua_State* state)
        {
            lua_pushboolean(state, Audio::AudioSystem::IsInitialized() ? 1 : 0);
            return 1;
        }

        int LuaAudioHasPlaybackDevice(lua_State* state)
        {
            lua_pushboolean(state, Audio::AudioSystem::HasPlaybackDevice() ? 1 : 0);
            return 1;
        }

        int LuaAudioPreload(lua_State* state)
        {
            const std::string path = LuaHelpers::ToString(state, 1);
            const auto clip = Audio::AudioSystem::LoadClip(path);
            lua_pushboolean(state, clip && clip->IsValid() ? 1 : 0);
            return 1;
        }

        int LuaAudioPlay2D(lua_State* state)
        {
            const std::string path = LuaHelpers::ToString(state, 1);
            auto clip = Audio::AudioSystem::LoadClip(path);
            if (!clip || !clip->IsValid())
            {
                lua_pushinteger(state, 0);
                return 1;
            }

            Audio::PlaySettings settings {};
            if (lua_gettop(state) >= 2)
            {
                TryReadAudioPlaySettings(state, 2, settings);
            }

            const Audio::AudioHandle handle = Audio::AudioSystem::Play2D(clip, settings);
            lua_pushinteger(state, static_cast<lua_Integer>(handle));
            return 1;
        }

        int LuaAudioPlayOneShot(lua_State* state)
        {
            const std::string path = LuaHelpers::ToString(state, 1);
            auto clip = Audio::AudioSystem::LoadClip(path);
            if (!clip || !clip->IsValid())
            {
                lua_pushinteger(state, 0);
                return 1;
            }

            Audio::PlaySettings settings {};
            settings.looping = false;
            if (lua_gettop(state) >= 2)
            {
                TryReadAudioPlaySettings(state, 2, settings);
                settings.looping = false;
            }

            const Audio::AudioHandle handle = Audio::AudioSystem::Play2D(clip, settings);
            lua_pushinteger(state, static_cast<lua_Integer>(handle));
            return 1;
        }

        int LuaAudioPlay3D(lua_State* state)
        {
            const std::string path = LuaHelpers::ToString(state, 1);
            auto clip = Audio::AudioSystem::LoadClip(path);
            if (!clip || !clip->IsValid())
            {
                lua_pushinteger(state, 0);
                return 1;
            }

            std::array<float, 3> position {};
            if (!TryReadVec3Argument(state, 2, position))
            {
                lua_pushinteger(state, 0);
                return 1;
            }

            Audio::PlaySettings settings {};
            const int settingsIndex = lua_istable(state, 2) ? 3 : 5;
            if (lua_gettop(state) >= settingsIndex)
            {
                TryReadAudioPlaySettings(state, settingsIndex, settings);
            }

            const Audio::AudioHandle handle = Audio::AudioSystem::Play3D(clip, position, settings);
            lua_pushinteger(state, static_cast<lua_Integer>(handle));
            return 1;
        }

        int LuaAudioStop(lua_State* state)
        {
            const Audio::AudioHandle handle = static_cast<Audio::AudioHandle>(luaL_checkinteger(state, 1));
            Audio::AudioSystem::Stop(handle);
            return 0;
        }

        int LuaAudioPause(lua_State* state)
        {
            const Audio::AudioHandle handle = static_cast<Audio::AudioHandle>(luaL_checkinteger(state, 1));
            Audio::AudioSystem::Pause(handle);
            return 0;
        }

        int LuaAudioResume(lua_State* state)
        {
            const Audio::AudioHandle handle = static_cast<Audio::AudioHandle>(luaL_checkinteger(state, 1));
            Audio::AudioSystem::Resume(handle);
            return 0;
        }

        int LuaAudioIsPlaying(lua_State* state)
        {
            const Audio::AudioHandle handle = static_cast<Audio::AudioHandle>(luaL_checkinteger(state, 1));
            lua_pushboolean(state, Audio::AudioSystem::IsPlaying(handle) ? 1 : 0);
            return 1;
        }

        int LuaAudioSetVolume(lua_State* state)
        {
            const Audio::AudioHandle handle = static_cast<Audio::AudioHandle>(luaL_checkinteger(state, 1));
            Audio::AudioSystem::SetVolume(handle, static_cast<float>(luaL_checknumber(state, 2)));
            return 0;
        }

        int LuaAudioSetPitch(lua_State* state)
        {
            const Audio::AudioHandle handle = static_cast<Audio::AudioHandle>(luaL_checkinteger(state, 1));
            Audio::AudioSystem::SetPitch(handle, static_cast<float>(luaL_checknumber(state, 2)));
            return 0;
        }

        int LuaAudioSetPosition(lua_State* state)
        {
            const Audio::AudioHandle handle = static_cast<Audio::AudioHandle>(luaL_checkinteger(state, 1));
            std::array<float, 3> position {};
            if (TryReadVec3Argument(state, 2, position))
            {
                Audio::AudioSystem::SetPosition(handle, position);
            }
            return 0;
        }

        int LuaAudioSetMasterVolume(lua_State* state)
        {
            Audio::AudioSystem::SetMasterVolume(static_cast<float>(luaL_checknumber(state, 1)));
            return 0;
        }

        int LuaAudioGetMasterVolume(lua_State* state)
        {
            lua_pushnumber(state, Audio::AudioSystem::GetMasterVolume());
            return 1;
        }

        int LuaAudioSetListenerTransform(lua_State* state)
        {
            std::array<float, 3> position {};
            std::array<float, 3> forward { 0.0f, 0.0f, -1.0f };
            std::array<float, 3> up { 0.0f, 1.0f, 0.0f };
            if (!TryReadVec3Argument(state, 1, position))
            {
                return 0;
            }

            if (lua_gettop(state) >= 2)
            {
                (void)TryReadVec3Argument(state, 2, forward);
            }
            if (lua_gettop(state) >= 3)
            {
                (void)TryReadVec3Argument(state, 3, up);
            }

            Audio::AudioSystem::SetListenerTransform(position, forward, up);
            return 0;
        }

        bool TryGetAudioSourceComponent(EntityID entity, AudioSourceComponent*& outComponent)
        {
            if (!IsSceneEntityValid(entity))
            {
                return false;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<AudioSourceComponent>(entity))
            {
                return false;
            }

            outComponent = &registry.get<AudioSourceComponent>(entity);
            return true;
        }

        bool TryGetEntityFromProxy(lua_State* state, const int index, EntityID& outEntity)
        {
            if (!lua_istable(state, index))
            {
                return false;
            }

            const int absoluteIndex = lua_absindex(state, index);
            lua_getfield(state, absoluteIndex, "__entity");
            const bool isValid = TryGetEntityFromLua(state, -1, outEntity);
            lua_pop(state, 1);
            return isValid;
        }

        bool TryGetAudioListenerComponent(EntityID entity, AudioListenerComponent*& outComponent)
        {
            if (!IsSceneEntityValid(entity))
            {
                return false;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<AudioListenerComponent>(entity))
            {
                return false;
            }

            outComponent = &registry.get<AudioListenerComponent>(entity);
            return true;
        }

        bool TryGetAudioSourceComponentFromProxy(
            lua_State* state,
            const int index,
            EntityID& outEntity,
            AudioSourceComponent*& outComponent)
        {
            return TryGetEntityFromProxy(state, index, outEntity) && TryGetAudioSourceComponent(outEntity, outComponent);
        }

        bool TryGetAudioListenerComponentFromProxy(
            lua_State* state,
            const int index,
            EntityID& outEntity,
            AudioListenerComponent*& outComponent)
        {
            return TryGetEntityFromProxy(state, index, outEntity) && TryGetAudioListenerComponent(outEntity, outComponent);
        }

        Audio::AudioHandle StartAudioSourcePlayback(EntityID entity, AudioSourceComponent& source)
        {
            if (source.clipAsset.empty() || source.mute)
            {
                return 0;
            }

            auto clip = Audio::AudioSystem::LoadClip(source.clipAsset);
            if (!clip || !clip->IsValid())
            {
                return 0;
            }

            Audio::PlaySettings settings {};
            settings.volume = source.volume;
            settings.pitch = source.pitch;
            settings.looping = source.looping;
            settings.spatialized = source.spatialized;
            settings.minDistance = source.minDistance;
            settings.maxDistance = source.maxDistance;

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (source.runtimeHandle != 0)
            {
                Audio::AudioSystem::Stop(source.runtimeHandle);
                source.runtimeHandle = 0;
            }

            if (source.spatialized && registry.all_of<TransformComponent>(entity))
            {
                source.runtimeHandle = Audio::AudioSystem::Play3D(
                    clip,
                    registry.get<TransformComponent>(entity).worldPosition,
                    settings);
            }
            else
            {
                source.runtimeHandle = Audio::AudioSystem::Play2D(clip, settings);
            }

            return source.runtimeHandle;
        }

        Audio::AudioHandle PlayAudioSourceOneShot(EntityID entity, const AudioSourceComponent& source)
        {
            if (source.clipAsset.empty() || source.mute)
            {
                return 0;
            }

            auto clip = Audio::AudioSystem::LoadClip(source.clipAsset);
            if (!clip || !clip->IsValid())
            {
                return 0;
            }

            Audio::PlaySettings settings {};
            settings.volume = source.volume;
            settings.pitch = source.pitch;
            settings.looping = false;
            settings.spatialized = source.spatialized;
            settings.minDistance = source.minDistance;
            settings.maxDistance = source.maxDistance;

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (source.spatialized && registry.all_of<TransformComponent>(entity))
            {
                return Audio::AudioSystem::Play3D(
                    clip,
                    registry.get<TransformComponent>(entity).worldPosition,
                    settings);
            }

            return Audio::AudioSystem::Play2D(clip, settings);
        }

        int LuaAudioSourcePlay(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                lua_pushinteger(state, 0);
                return 1;
            }

            const Audio::AudioHandle handle = StartAudioSourcePlayback(entity, *source);
            lua_pushinteger(state, static_cast<lua_Integer>(handle));
            return 1;
        }

        int LuaAudioSourceProxyPlay(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                lua_pushinteger(state, 0);
                return 1;
            }

            const Audio::AudioHandle handle = StartAudioSourcePlayback(entity, *source);
            lua_pushinteger(state, static_cast<lua_Integer>(handle));
            return 1;
        }

        int LuaAudioSourcePlayOneShot(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                lua_pushinteger(state, 0);
                return 1;
            }

            const Audio::AudioHandle handle = PlayAudioSourceOneShot(entity, *source);
            lua_pushinteger(state, static_cast<lua_Integer>(handle));
            return 1;
        }

        int LuaAudioSourceProxyPlayOneShot(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                lua_pushinteger(state, 0);
                return 1;
            }

            const Audio::AudioHandle handle = PlayAudioSourceOneShot(entity, *source);
            lua_pushinteger(state, static_cast<lua_Integer>(handle));
            return 1;
        }

        int LuaAudioSourceStop(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                return 0;
            }

            if (source->runtimeHandle != 0)
            {
                Audio::AudioSystem::Stop(source->runtimeHandle);
                source->runtimeHandle = 0;
            }
            return 0;
        }

        int LuaAudioSourceProxyStop(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                return 0;
            }

            if (source->runtimeHandle != 0)
            {
                Audio::AudioSystem::Stop(source->runtimeHandle);
                source->runtimeHandle = 0;
            }
            return 0;
        }

        int LuaAudioSourcePause(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                return 0;
            }

            if (source->runtimeHandle != 0)
            {
                Audio::AudioSystem::Pause(source->runtimeHandle);
            }
            return 0;
        }

        int LuaAudioSourceProxyPause(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                return 0;
            }

            if (source->runtimeHandle != 0)
            {
                Audio::AudioSystem::Pause(source->runtimeHandle);
            }
            return 0;
        }

        int LuaAudioSourceResume(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                return 0;
            }

            if (source->runtimeHandle != 0)
            {
                Audio::AudioSystem::Resume(source->runtimeHandle);
            }
            else
            {
                StartAudioSourcePlayback(entity, *source);
            }
            return 0;
        }

        int LuaAudioSourceProxyResume(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                return 0;
            }

            if (source->runtimeHandle != 0)
            {
                Audio::AudioSystem::Resume(source->runtimeHandle);
            }
            else
            {
                StartAudioSourcePlayback(entity, *source);
            }
            return 0;
        }

        int LuaAudioSourceIsPlaying(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            const bool isPlaying = source->runtimeHandle != 0 && Audio::AudioSystem::IsPlaying(source->runtimeHandle);
            lua_pushboolean(state, isPlaying ? 1 : 0);
            return 1;
        }

        int LuaAudioSourceProxyIsPlaying(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            const bool isPlaying = source->runtimeHandle != 0 && Audio::AudioSystem::IsPlaying(source->runtimeHandle);
            lua_pushboolean(state, isPlaying ? 1 : 0);
            return 1;
        }

        int LuaAudioSourceGetClip(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                lua_pushliteral(state, "");
                return 1;
            }

            lua_pushlstring(state, source->clipAsset.c_str(), source->clipAsset.size());
            return 1;
        }

        int LuaAudioSourceProxyGetClip(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                lua_pushliteral(state, "");
                return 1;
            }

            lua_pushlstring(state, source->clipAsset.c_str(), source->clipAsset.size());
            return 1;
        }

        int LuaAudioSourceSetClip(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                return 0;
            }

            source->clipAsset = LuaHelpers::ToString(state, 2);
            if (source->runtimeHandle != 0)
            {
                Audio::AudioSystem::Stop(source->runtimeHandle);
                source->runtimeHandle = 0;
            }
            return 0;
        }

        int LuaAudioSourceProxySetClip(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                return 0;
            }

            source->clipAsset = LuaHelpers::ToString(state, 2);
            if (source->runtimeHandle != 0)
            {
                Audio::AudioSystem::Stop(source->runtimeHandle);
                source->runtimeHandle = 0;
            }
            return 0;
        }

        int LuaAudioSourceGetVolume(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                lua_pushnumber(state, 0.0);
                return 1;
            }

            lua_pushnumber(state, source->volume);
            return 1;
        }

        int LuaAudioSourceProxyGetVolume(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                lua_pushnumber(state, 0.0);
                return 1;
            }

            lua_pushnumber(state, source->volume);
            return 1;
        }

        int LuaAudioSourceSetVolume(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                return 0;
            }

            source->volume = std::clamp(static_cast<float>(luaL_checknumber(state, 2)), 0.0f, 4.0f);
            if (source->runtimeHandle != 0)
            {
                Audio::AudioSystem::SetVolume(source->runtimeHandle, source->volume);
            }
            return 0;
        }

        int LuaAudioSourceProxySetVolume(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                return 0;
            }

            source->volume = std::clamp(static_cast<float>(luaL_checknumber(state, 2)), 0.0f, 4.0f);
            if (source->runtimeHandle != 0)
            {
                Audio::AudioSystem::SetVolume(source->runtimeHandle, source->volume);
            }
            return 0;
        }

        int LuaAudioSourceGetPitch(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                lua_pushnumber(state, 0.0);
                return 1;
            }

            lua_pushnumber(state, source->pitch);
            return 1;
        }

        int LuaAudioSourceProxyGetPitch(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                lua_pushnumber(state, 0.0);
                return 1;
            }

            lua_pushnumber(state, source->pitch);
            return 1;
        }

        int LuaAudioSourceSetPitch(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                return 0;
            }

            source->pitch = std::clamp(static_cast<float>(luaL_checknumber(state, 2)), 0.01f, 4.0f);
            if (source->runtimeHandle != 0)
            {
                Audio::AudioSystem::SetPitch(source->runtimeHandle, source->pitch);
            }
            return 0;
        }

        int LuaAudioSourceProxySetPitch(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                return 0;
            }

            source->pitch = std::clamp(static_cast<float>(luaL_checknumber(state, 2)), 0.01f, 4.0f);
            if (source->runtimeHandle != 0)
            {
                Audio::AudioSystem::SetPitch(source->runtimeHandle, source->pitch);
            }
            return 0;
        }

        int LuaAudioSourceGetLooping(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, source->looping ? 1 : 0);
            return 1;
        }

        int LuaAudioSourceProxyGetLooping(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, source->looping ? 1 : 0);
            return 1;
        }

        int LuaAudioSourceSetLooping(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                return 0;
            }

            const bool wasPlaying = source->runtimeHandle != 0 && Audio::AudioSystem::IsPlaying(source->runtimeHandle);
            source->looping = lua_toboolean(state, 2) != 0;
            if (wasPlaying)
            {
                StartAudioSourcePlayback(entity, *source);
            }
            return 0;
        }

        int LuaAudioSourceProxySetLooping(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                return 0;
            }

            const bool wasPlaying = source->runtimeHandle != 0 && Audio::AudioSystem::IsPlaying(source->runtimeHandle);
            source->looping = lua_toboolean(state, 2) != 0;
            if (wasPlaying)
            {
                StartAudioSourcePlayback(entity, *source);
            }
            return 0;
        }

        int LuaAudioSourceGetSpatialized(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, source->spatialized ? 1 : 0);
            return 1;
        }

        int LuaAudioSourceProxyGetSpatialized(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, source->spatialized ? 1 : 0);
            return 1;
        }

        int LuaAudioSourceSetSpatialized(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                return 0;
            }

            const bool wasPlaying = source->runtimeHandle != 0 && Audio::AudioSystem::IsPlaying(source->runtimeHandle);
            source->spatialized = lua_toboolean(state, 2) != 0;
            if (wasPlaying)
            {
                StartAudioSourcePlayback(entity, *source);
            }
            return 0;
        }

        int LuaAudioSourceProxySetSpatialized(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                return 0;
            }

            const bool wasPlaying = source->runtimeHandle != 0 && Audio::AudioSystem::IsPlaying(source->runtimeHandle);
            source->spatialized = lua_toboolean(state, 2) != 0;
            if (wasPlaying)
            {
                StartAudioSourcePlayback(entity, *source);
            }
            return 0;
        }

        int LuaAudioSourceGetMute(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, source->mute ? 1 : 0);
            return 1;
        }

        int LuaAudioSourceProxyGetMute(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, source->mute ? 1 : 0);
            return 1;
        }

        int LuaAudioSourceSetMute(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                return 0;
            }

            source->mute = lua_toboolean(state, 2) != 0;
            if (source->mute && source->runtimeHandle != 0)
            {
                Audio::AudioSystem::Stop(source->runtimeHandle);
                source->runtimeHandle = 0;
            }
            return 0;
        }

        int LuaAudioSourceProxySetMute(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetAudioSourceComponentFromProxy(state, 1, entity, source))
            {
                return 0;
            }

            source->mute = lua_toboolean(state, 2) != 0;
            if (source->mute && source->runtimeHandle != 0)
            {
                Audio::AudioSystem::Stop(source->runtimeHandle);
                source->runtimeHandle = 0;
            }
            return 0;
        }

        int LuaAudioListenerGetEnabled(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioListenerComponent* listener = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioListenerComponent(entity, listener))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, listener->enabled ? 1 : 0);
            return 1;
        }

        int LuaAudioListenerProxyGetEnabled(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioListenerComponent* listener = nullptr;
            if (!TryGetAudioListenerComponentFromProxy(state, 1, entity, listener))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, listener->enabled ? 1 : 0);
            return 1;
        }

        int LuaAudioListenerSetEnabled(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioListenerComponent* listener = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioListenerComponent(entity, listener))
            {
                return 0;
            }

            listener->enabled = lua_toboolean(state, 2) != 0;
            return 0;
        }

        int LuaAudioListenerProxySetEnabled(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioListenerComponent* listener = nullptr;
            if (!TryGetAudioListenerComponentFromProxy(state, 1, entity, listener))
            {
                return 0;
            }

            listener->enabled = lua_toboolean(state, 2) != 0;
            return 0;
        }

        int LuaAudioListenerGetVolume(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioListenerComponent* listener = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioListenerComponent(entity, listener))
            {
                lua_pushnumber(state, 0.0);
                return 1;
            }

            lua_pushnumber(state, listener->volume);
            return 1;
        }

        int LuaAudioListenerProxyGetVolume(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioListenerComponent* listener = nullptr;
            if (!TryGetAudioListenerComponentFromProxy(state, 1, entity, listener))
            {
                lua_pushnumber(state, 0.0);
                return 1;
            }

            lua_pushnumber(state, listener->volume);
            return 1;
        }

        int LuaAudioListenerSetVolume(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioListenerComponent* listener = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioListenerComponent(entity, listener))
            {
                return 0;
            }

            listener->volume = std::clamp(static_cast<float>(luaL_checknumber(state, 2)), 0.0f, 2.0f);
            return 0;
        }

        int LuaAudioListenerProxySetVolume(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioListenerComponent* listener = nullptr;
            if (!TryGetAudioListenerComponentFromProxy(state, 1, entity, listener))
            {
                return 0;
            }

            listener->volume = std::clamp(static_cast<float>(luaL_checknumber(state, 2)), 0.0f, 2.0f);
            return 0;
        }

        void PushAudioSourceProxy(lua_State* state, const EntityID entity)
        {
            lua_newtable(state);
            lua_pushinteger(state, static_cast<lua_Integer>(static_cast<entt::id_type>(entity)));
            lua_setfield(state, -2, "__entity");
            lua_pushcfunction(state, LuaAudioSourceProxyPlay);
            lua_setfield(state, -2, "Play");
            lua_pushcfunction(state, LuaAudioSourceProxyPlayOneShot);
            lua_setfield(state, -2, "PlayOneShot");
            lua_pushcfunction(state, LuaAudioSourceProxyStop);
            lua_setfield(state, -2, "Stop");
            lua_pushcfunction(state, LuaAudioSourceProxyPause);
            lua_setfield(state, -2, "Pause");
            lua_pushcfunction(state, LuaAudioSourceProxyResume);
            lua_setfield(state, -2, "Resume");
            lua_pushcfunction(state, LuaAudioSourceProxyIsPlaying);
            lua_setfield(state, -2, "IsPlaying");
            lua_pushcfunction(state, LuaAudioSourceProxyGetClip);
            lua_setfield(state, -2, "GetClip");
            lua_pushcfunction(state, LuaAudioSourceProxySetClip);
            lua_setfield(state, -2, "SetClip");
            lua_pushcfunction(state, LuaAudioSourceProxyGetVolume);
            lua_setfield(state, -2, "GetVolume");
            lua_pushcfunction(state, LuaAudioSourceProxySetVolume);
            lua_setfield(state, -2, "SetVolume");
            lua_pushcfunction(state, LuaAudioSourceProxyGetPitch);
            lua_setfield(state, -2, "GetPitch");
            lua_pushcfunction(state, LuaAudioSourceProxySetPitch);
            lua_setfield(state, -2, "SetPitch");
            lua_pushcfunction(state, LuaAudioSourceProxyGetLooping);
            lua_setfield(state, -2, "GetLooping");
            lua_pushcfunction(state, LuaAudioSourceProxySetLooping);
            lua_setfield(state, -2, "SetLooping");
            lua_pushcfunction(state, LuaAudioSourceProxyGetSpatialized);
            lua_setfield(state, -2, "GetSpatialized");
            lua_pushcfunction(state, LuaAudioSourceProxySetSpatialized);
            lua_setfield(state, -2, "SetSpatialized");
            lua_pushcfunction(state, LuaAudioSourceProxyGetMute);
            lua_setfield(state, -2, "GetMute");
            lua_pushcfunction(state, LuaAudioSourceProxySetMute);
            lua_setfield(state, -2, "SetMute");
        }

        void PushAudioListenerProxy(lua_State* state, const EntityID entity)
        {
            lua_newtable(state);
            lua_pushinteger(state, static_cast<lua_Integer>(static_cast<entt::id_type>(entity)));
            lua_setfield(state, -2, "__entity");
            lua_pushcfunction(state, LuaAudioListenerProxyGetEnabled);
            lua_setfield(state, -2, "GetEnabled");
            lua_pushcfunction(state, LuaAudioListenerProxySetEnabled);
            lua_setfield(state, -2, "SetEnabled");
            lua_pushcfunction(state, LuaAudioListenerProxyGetVolume);
            lua_setfield(state, -2, "GetVolume");
            lua_pushcfunction(state, LuaAudioListenerProxySetVolume);
            lua_setfield(state, -2, "SetVolume");
        }

        int LuaEntityGetAudioSource(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioSourceComponent* source = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioSourceComponent(entity, source))
            {
                lua_pushnil(state);
                return 1;
            }

            PushAudioSourceProxy(state, entity);
            return 1;
        }

        int LuaEntityGetAudioListener(lua_State* state)
        {
            EntityID entity = entt::null;
            AudioListenerComponent* listener = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetAudioListenerComponent(entity, listener))
            {
                lua_pushnil(state);
                return 1;
            }

            PushAudioListenerProxy(state, entity);
            return 1;
        }

        bool TryGetMaterialComponent(EntityID entity, MaterialComponent*& outComponent)
        {
            if (!IsSceneEntityValid(entity))
            {
                return false;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<MaterialComponent>(entity))
            {
                return false;
            }

            outComponent = &registry.get<MaterialComponent>(entity);
            return true;
        }

        bool TryGetMaterialComponentFromProxy(
            lua_State* state,
            const int index,
            EntityID& outEntity,
            MaterialComponent*& outComponent)
        {
            return TryGetEntityFromProxy(state, index, outEntity) && TryGetMaterialComponent(outEntity, outComponent);
        }

        bool TryGetRigidBodyComponent(EntityID entity, RigidBodyComponent*& outComponent)
        {
            if (!IsSceneEntityValid(entity))
            {
                return false;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<RigidBodyComponent>(entity))
            {
                return false;
            }

            outComponent = &registry.get<RigidBodyComponent>(entity);
            return true;
        }

        bool TryGetRigidBodyComponentFromProxy(
            lua_State* state,
            const int index,
            EntityID& outEntity,
            RigidBodyComponent*& outComponent)
        {
            return TryGetEntityFromProxy(state, index, outEntity) && TryGetRigidBodyComponent(outEntity, outComponent);
        }

        bool TryGetLightEntity(EntityID entity)
        {
            if (!IsSceneEntityValid(entity))
            {
                return false;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            return registry.all_of<DirectionalLightComponent>(entity) ||
                registry.all_of<PointLightComponent>(entity) ||
                registry.all_of<SpotLightComponent>(entity);
        }

        bool TryGetLightEntityFromProxy(lua_State* state, const int index, EntityID& outEntity)
        {
            return TryGetEntityFromProxy(state, index, outEntity) && TryGetLightEntity(outEntity);
        }

        int LuaTransformGetPosition(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<TransformComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            PushVec3(state, registry.get<TransformComponent>(entity).position);
            return 1;
        }

        int LuaTransformSetPosition(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            std::array<float, 3> value {};
            if (!TryReadVec3Argument(state, 2, value))
            {
                return 0;
            }

            auto& transform = g_ActiveScriptScene->GetRegistry().get<TransformComponent>(entity);
            transform.position = value;
            transform.dirty = true;
            return 0;
        }

        int LuaTransformTranslate(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            std::array<float, 3> delta {};
            if (!TryReadVec3Argument(state, 2, delta))
            {
                return 0;
            }

            auto& transform = g_ActiveScriptScene->GetRegistry().get<TransformComponent>(entity);
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                transform.position[axis] += delta[axis];
            }
            transform.dirty = true;
            return 0;
        }

        int LuaTransformGetRotation(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<TransformComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            PushVec3(state, registry.get<TransformComponent>(entity).rotation);
            return 1;
        }

        int LuaTransformSetRotation(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            std::array<float, 3> value {};
            if (!TryReadVec3Argument(state, 2, value))
            {
                return 0;
            }

            auto& transform = g_ActiveScriptScene->GetRegistry().get<TransformComponent>(entity);
            transform.rotation = value;
            transform.dirty = true;
            return 0;
        }

        int LuaTransformGetScale(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<TransformComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            PushVec3(state, registry.get<TransformComponent>(entity).scale);
            return 1;
        }

        int LuaTransformSetScale(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            std::array<float, 3> value {};
            if (!TryReadVec3Argument(state, 2, value))
            {
                return 0;
            }

            auto& transform = g_ActiveScriptScene->GetRegistry().get<TransformComponent>(entity);
            transform.scale = value;
            transform.dirty = true;
            return 0;
        }

        int LuaRendererGetColor(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<MaterialComponent>(entity))
            {
                PushVec4(state, registry.get<MaterialComponent>(entity).albedoColor);
                return 1;
            }

            if (registry.all_of<MeshRendererComponent>(entity))
            {
                PushVec4(state, registry.get<MeshRendererComponent>(entity).color);
                return 1;
            }

            lua_pushnil(state);
            return 1;
        }

        int LuaRendererSetColor(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            std::array<float, 4> color { 1.0f, 1.0f, 1.0f, 1.0f };
            if (!TryReadColorArgument(state, 2, color))
            {
                return 0;
            }

            for (float& channel : color)
            {
                channel = std::clamp(channel, 0.0f, 1.0f);
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<MeshRendererComponent>(entity))
            {
                registry.get<MeshRendererComponent>(entity).color = color;
            }
            if (registry.all_of<MaterialComponent>(entity))
            {
                registry.get<MaterialComponent>(entity).albedoColor = color;
            }

            return 0;
        }

        int LuaRendererSetVisible(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            if (!lua_isboolean(state, 2))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<MeshRendererComponent>(entity))
            {
                registry.get<MeshRendererComponent>(entity).visible = lua_toboolean(state, 2) != 0;
            }

            return 0;
        }

        int LuaRendererGetVisible(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<MeshRendererComponent>(entity))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, registry.get<MeshRendererComponent>(entity).visible ? 1 : 0);
            return 1;
        }

        int LuaRendererGetPrimitive(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<MeshRendererComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            const auto& renderer = registry.get<MeshRendererComponent>(entity);
            if (!renderer.usePrimitive)
            {
                lua_pushnil(state);
                return 1;
            }

            lua_pushstring(state, PrimitiveTypeName(renderer.primitive));
            return 1;
        }

        int LuaRendererSetPrimitive(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            const auto primitive = ParsePrimitiveType(LuaHelpers::ToString(state, 2));
            if (!primitive.has_value())
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<MeshRendererComponent>(entity))
            {
                return 0;
            }

            auto& renderer = registry.get<MeshRendererComponent>(entity);
            renderer.usePrimitive = true;
            renderer.primitive = *primitive;
            renderer.meshSource.clear();
            renderer.importedSceneSource.clear();
            return 0;
        }

        int LuaMaterialGetColor(lua_State* state)
        {
            return LuaRendererGetColor(state);
        }

        int LuaMaterialSetColor(lua_State* state)
        {
            return LuaRendererSetColor(state);
        }

        int LuaMaterialGetMetallic(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<MaterialComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            lua_pushnumber(state, registry.get<MaterialComponent>(entity).metallic);
            return 1;
        }

        int LuaMaterialSetMetallic(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity) || !lua_isnumber(state, 2))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<MaterialComponent>(entity))
            {
                registry.get<MaterialComponent>(entity).metallic =
                    std::clamp(static_cast<float>(lua_tonumber(state, 2)), 0.0f, 1.0f);
            }

            return 0;
        }

        int LuaMaterialGetSmoothness(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<MaterialComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            lua_pushnumber(state, registry.get<MaterialComponent>(entity).smoothness);
            return 1;
        }

        int LuaMaterialSetSmoothness(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity) || !lua_isnumber(state, 2))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<MaterialComponent>(entity))
            {
                registry.get<MaterialComponent>(entity).smoothness =
                    std::clamp(static_cast<float>(lua_tonumber(state, 2)), 0.0f, 1.0f);
            }

            return 0;
        }

        int LuaMaterialProxyGetColor(lua_State* state)
        {
            EntityID entity = entt::null;
            MaterialComponent* material = nullptr;
            if (!TryGetMaterialComponentFromProxy(state, 1, entity, material))
            {
                lua_pushnil(state);
                return 1;
            }

            PushVec4(state, material->albedoColor);
            return 1;
        }

        int LuaMaterialProxySetColor(lua_State* state)
        {
            EntityID entity = entt::null;
            MaterialComponent* material = nullptr;
            if (!TryGetMaterialComponentFromProxy(state, 1, entity, material))
            {
                return 0;
            }

            std::array<float, 4> color {};
            if (!TryReadColorArgument(state, 2, color))
            {
                return 0;
            }

            for (float& channel : color)
            {
                channel = std::clamp(channel, 0.0f, 1.0f);
            }

            material->albedoColor = color;
            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<MeshRendererComponent>(entity))
            {
                registry.get<MeshRendererComponent>(entity).color = color;
            }

            return 0;
        }

        int LuaMaterialProxyGetMetallic(lua_State* state)
        {
            EntityID entity = entt::null;
            MaterialComponent* material = nullptr;
            if (!TryGetMaterialComponentFromProxy(state, 1, entity, material))
            {
                lua_pushnil(state);
                return 1;
            }

            lua_pushnumber(state, material->metallic);
            return 1;
        }

        int LuaMaterialProxySetMetallic(lua_State* state)
        {
            EntityID entity = entt::null;
            MaterialComponent* material = nullptr;
            if (!TryGetMaterialComponentFromProxy(state, 1, entity, material) || !lua_isnumber(state, 2))
            {
                return 0;
            }

            material->metallic = std::clamp(static_cast<float>(lua_tonumber(state, 2)), 0.0f, 1.0f);
            return 0;
        }

        int LuaMaterialProxyGetSmoothness(lua_State* state)
        {
            EntityID entity = entt::null;
            MaterialComponent* material = nullptr;
            if (!TryGetMaterialComponentFromProxy(state, 1, entity, material))
            {
                lua_pushnil(state);
                return 1;
            }

            lua_pushnumber(state, material->smoothness);
            return 1;
        }

        int LuaMaterialProxySetSmoothness(lua_State* state)
        {
            EntityID entity = entt::null;
            MaterialComponent* material = nullptr;
            if (!TryGetMaterialComponentFromProxy(state, 1, entity, material) || !lua_isnumber(state, 2))
            {
                return 0;
            }

            material->smoothness = std::clamp(static_cast<float>(lua_tonumber(state, 2)), 0.0f, 1.0f);
            return 0;
        }

        int LuaRigidBodyGetVelocity(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<RigidBodyComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            PushVec3(state, registry.get<RigidBodyComponent>(entity).linearVelocity);
            return 1;
        }

        int LuaRigidBodySetVelocity(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            std::array<float, 3> value {};
            if (!TryReadVec3Argument(state, 2, value))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<RigidBodyComponent>(entity))
            {
                auto& body = registry.get<RigidBodyComponent>(entity);
                body.linearVelocity = value;
                body.sleeping = false;
            }

            return 0;
        }

        int LuaRigidBodyGetAngularVelocity(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<RigidBodyComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            PushVec3(state, registry.get<RigidBodyComponent>(entity).angularVelocity);
            return 1;
        }

        int LuaRigidBodySetAngularVelocity(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            std::array<float, 3> value {};
            if (!TryReadVec3Argument(state, 2, value))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<RigidBodyComponent>(entity))
            {
                auto& body = registry.get<RigidBodyComponent>(entity);
                body.angularVelocity = value;
                body.sleeping = false;
            }

            return 0;
        }

        int LuaRigidBodyAddImpulse(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            std::array<float, 3> impulse {};
            if (!TryReadVec3Argument(state, 2, impulse))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<RigidBodyComponent>(entity))
            {
                return 0;
            }

            auto& body = registry.get<RigidBodyComponent>(entity);
            const float safeMass = std::max(body.mass, 0.001f);
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                body.linearVelocity[axis] += impulse[axis] / safeMass;
            }
            body.sleeping = false;
            return 0;
        }

        int LuaRigidBodyWake(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<RigidBodyComponent>(entity))
            {
                registry.get<RigidBodyComponent>(entity).sleeping = false;
            }

            return 0;
        }

        int LuaRigidBodyGetMass(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<RigidBodyComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            lua_pushnumber(state, registry.get<RigidBodyComponent>(entity).mass);
            return 1;
        }

        int LuaRigidBodySetMass(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity) || !lua_isnumber(state, 2))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<RigidBodyComponent>(entity))
            {
                registry.get<RigidBodyComponent>(entity).mass =
                    std::max(0.001f, static_cast<float>(lua_tonumber(state, 2)));
            }

            return 0;
        }

        int LuaRigidBodyGetUseGravity(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<RigidBodyComponent>(entity))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, registry.get<RigidBodyComponent>(entity).enableGravity ? 1 : 0);
            return 1;
        }

        int LuaRigidBodySetUseGravity(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity) || !lua_isboolean(state, 2))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<RigidBodyComponent>(entity))
            {
                registry.get<RigidBodyComponent>(entity).enableGravity = lua_toboolean(state, 2) != 0;
            }

            return 0;
        }

        int LuaRigidBodyGetBodyType(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<RigidBodyComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            lua_pushstring(state, RigidBodyTypeName(registry.get<RigidBodyComponent>(entity).bodyType));
            return 1;
        }

        int LuaRigidBodySetBodyType(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            const auto bodyType = ParseRigidBodyType(LuaHelpers::ToString(state, 2));
            if (!bodyType.has_value())
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<RigidBodyComponent>(entity))
            {
                registry.get<RigidBodyComponent>(entity).bodyType = *bodyType;
            }

            return 0;
        }

        int LuaRigidBodyIsSleeping(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<RigidBodyComponent>(entity))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, registry.get<RigidBodyComponent>(entity).sleeping ? 1 : 0);
            return 1;
        }

        int LuaRigidBodyProxyGetVelocity(lua_State* state)
        {
            EntityID entity = entt::null;
            RigidBodyComponent* body = nullptr;
            if (!TryGetRigidBodyComponentFromProxy(state, 1, entity, body))
            {
                lua_pushnil(state);
                return 1;
            }

            PushVec3(state, body->linearVelocity);
            return 1;
        }

        int LuaRigidBodyProxySetVelocity(lua_State* state)
        {
            EntityID entity = entt::null;
            RigidBodyComponent* body = nullptr;
            if (!TryGetRigidBodyComponentFromProxy(state, 1, entity, body))
            {
                return 0;
            }

            std::array<float, 3> value {};
            if (!TryReadVec3Argument(state, 2, value))
            {
                return 0;
            }

            body->linearVelocity = value;
            body->sleeping = false;
            return 0;
        }

        int LuaRigidBodyProxyGetAngularVelocity(lua_State* state)
        {
            EntityID entity = entt::null;
            RigidBodyComponent* body = nullptr;
            if (!TryGetRigidBodyComponentFromProxy(state, 1, entity, body))
            {
                lua_pushnil(state);
                return 1;
            }

            PushVec3(state, body->angularVelocity);
            return 1;
        }

        int LuaRigidBodyProxySetAngularVelocity(lua_State* state)
        {
            EntityID entity = entt::null;
            RigidBodyComponent* body = nullptr;
            if (!TryGetRigidBodyComponentFromProxy(state, 1, entity, body))
            {
                return 0;
            }

            std::array<float, 3> value {};
            if (!TryReadVec3Argument(state, 2, value))
            {
                return 0;
            }

            body->angularVelocity = value;
            body->sleeping = false;
            return 0;
        }

        int LuaRigidBodyProxyAddImpulse(lua_State* state)
        {
            EntityID entity = entt::null;
            RigidBodyComponent* body = nullptr;
            if (!TryGetRigidBodyComponentFromProxy(state, 1, entity, body))
            {
                return 0;
            }

            std::array<float, 3> impulse {};
            if (!TryReadVec3Argument(state, 2, impulse))
            {
                return 0;
            }

            const float safeMass = std::max(body->mass, 0.001f);
            for (std::size_t axis = 0; axis < 3; ++axis)
            {
                body->linearVelocity[axis] += impulse[axis] / safeMass;
            }
            body->sleeping = false;
            return 0;
        }

        int LuaRigidBodyProxyWake(lua_State* state)
        {
            EntityID entity = entt::null;
            RigidBodyComponent* body = nullptr;
            if (!TryGetRigidBodyComponentFromProxy(state, 1, entity, body))
            {
                return 0;
            }

            body->sleeping = false;
            return 0;
        }

        int LuaRigidBodyProxyGetMass(lua_State* state)
        {
            EntityID entity = entt::null;
            RigidBodyComponent* body = nullptr;
            if (!TryGetRigidBodyComponentFromProxy(state, 1, entity, body))
            {
                lua_pushnil(state);
                return 1;
            }

            lua_pushnumber(state, body->mass);
            return 1;
        }

        int LuaRigidBodyProxySetMass(lua_State* state)
        {
            EntityID entity = entt::null;
            RigidBodyComponent* body = nullptr;
            if (!TryGetRigidBodyComponentFromProxy(state, 1, entity, body) || !lua_isnumber(state, 2))
            {
                return 0;
            }

            body->mass = std::max(0.001f, static_cast<float>(lua_tonumber(state, 2)));
            return 0;
        }

        int LuaRigidBodyProxyGetUseGravity(lua_State* state)
        {
            EntityID entity = entt::null;
            RigidBodyComponent* body = nullptr;
            if (!TryGetRigidBodyComponentFromProxy(state, 1, entity, body))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, body->enableGravity ? 1 : 0);
            return 1;
        }

        int LuaRigidBodyProxySetUseGravity(lua_State* state)
        {
            EntityID entity = entt::null;
            RigidBodyComponent* body = nullptr;
            if (!TryGetRigidBodyComponentFromProxy(state, 1, entity, body) || !lua_isboolean(state, 2))
            {
                return 0;
            }

            body->enableGravity = lua_toboolean(state, 2) != 0;
            return 0;
        }

        int LuaRigidBodyProxyGetBodyType(lua_State* state)
        {
            EntityID entity = entt::null;
            RigidBodyComponent* body = nullptr;
            if (!TryGetRigidBodyComponentFromProxy(state, 1, entity, body))
            {
                lua_pushnil(state);
                return 1;
            }

            lua_pushstring(state, RigidBodyTypeName(body->bodyType));
            return 1;
        }

        int LuaRigidBodyProxySetBodyType(lua_State* state)
        {
            EntityID entity = entt::null;
            RigidBodyComponent* body = nullptr;
            if (!TryGetRigidBodyComponentFromProxy(state, 1, entity, body))
            {
                return 0;
            }

            const auto bodyType = ParseRigidBodyType(LuaHelpers::ToString(state, 2));
            if (!bodyType.has_value())
            {
                return 0;
            }

            body->bodyType = *bodyType;
            return 0;
        }

        int LuaRigidBodyProxyIsSleeping(lua_State* state)
        {
            EntityID entity = entt::null;
            RigidBodyComponent* body = nullptr;
            if (!TryGetRigidBodyComponentFromProxy(state, 1, entity, body))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, body->sleeping ? 1 : 0);
            return 1;
        }

        int LuaLightGetIntensity(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<DirectionalLightComponent>(entity))
            {
                lua_pushnumber(state, registry.get<DirectionalLightComponent>(entity).intensity);
                return 1;
            }
            if (registry.all_of<PointLightComponent>(entity))
            {
                lua_pushnumber(state, registry.get<PointLightComponent>(entity).intensity);
                return 1;
            }

            lua_pushnil(state);
            return 1;
        }

        int LuaLightSetIntensity(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity) || !lua_isnumber(state, 2))
            {
                return 0;
            }

            const float intensity = std::max(0.0f, static_cast<float>(lua_tonumber(state, 2)));
            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<DirectionalLightComponent>(entity))
            {
                registry.get<DirectionalLightComponent>(entity).intensity = intensity;
            }
            if (registry.all_of<PointLightComponent>(entity))
            {
                registry.get<PointLightComponent>(entity).intensity = intensity;
            }

            return 0;
        }

        int LuaLightGetColor(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<DirectionalLightComponent>(entity))
            {
                PushVec3(state, registry.get<DirectionalLightComponent>(entity).color);
                return 1;
            }
            if (registry.all_of<PointLightComponent>(entity))
            {
                PushVec3(state, registry.get<PointLightComponent>(entity).color);
                return 1;
            }
            if (registry.all_of<SpotLightComponent>(entity))
            {
                PushVec3(state, registry.get<SpotLightComponent>(entity).color);
                return 1;
            }

            lua_pushnil(state);
            return 1;
        }

        int LuaLightSetColor(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            std::array<float, 3> color {};
            if (!TryReadVec3Argument(state, 2, color))
            {
                return 0;
            }

            for (float& channel : color)
            {
                channel = std::clamp(channel, 0.0f, 1.0f);
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<DirectionalLightComponent>(entity))
            {
                registry.get<DirectionalLightComponent>(entity).color = color;
            }
            if (registry.all_of<PointLightComponent>(entity))
            {
                registry.get<PointLightComponent>(entity).color = color;
            }
            if (registry.all_of<SpotLightComponent>(entity))
            {
                registry.get<SpotLightComponent>(entity).color = color;
            }

            return 0;
        }

        int LuaLightGetRange(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<PointLightComponent>(entity))
            {
                lua_pushnumber(state, registry.get<PointLightComponent>(entity).range);
                return 1;
            }
            if (registry.all_of<SpotLightComponent>(entity))
            {
                lua_pushnumber(state, registry.get<SpotLightComponent>(entity).range);
                return 1;
            }

            lua_pushnil(state);
            return 1;
        }

        int LuaLightSetRange(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity) || !lua_isnumber(state, 2))
            {
                return 0;
            }

            const float range = std::max(0.0f, static_cast<float>(lua_tonumber(state, 2)));
            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<PointLightComponent>(entity))
            {
                registry.get<PointLightComponent>(entity).range = range;
            }
            if (registry.all_of<SpotLightComponent>(entity))
            {
                registry.get<SpotLightComponent>(entity).range = range;
            }

            return 0;
        }

        int LuaLightProxyGetIntensity(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetLightEntityFromProxy(state, 1, entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<DirectionalLightComponent>(entity))
            {
                lua_pushnumber(state, registry.get<DirectionalLightComponent>(entity).intensity);
                return 1;
            }
            if (registry.all_of<PointLightComponent>(entity))
            {
                lua_pushnumber(state, registry.get<PointLightComponent>(entity).intensity);
                return 1;
            }

            lua_pushnil(state);
            return 1;
        }

        int LuaLightProxySetIntensity(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetLightEntityFromProxy(state, 1, entity) || !lua_isnumber(state, 2))
            {
                return 0;
            }

            const float intensity = std::max(0.0f, static_cast<float>(lua_tonumber(state, 2)));
            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<DirectionalLightComponent>(entity))
            {
                registry.get<DirectionalLightComponent>(entity).intensity = intensity;
            }
            if (registry.all_of<PointLightComponent>(entity))
            {
                registry.get<PointLightComponent>(entity).intensity = intensity;
            }
            return 0;
        }

        int LuaLightProxyGetColor(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetLightEntityFromProxy(state, 1, entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<DirectionalLightComponent>(entity))
            {
                PushVec3(state, registry.get<DirectionalLightComponent>(entity).color);
                return 1;
            }
            if (registry.all_of<PointLightComponent>(entity))
            {
                PushVec3(state, registry.get<PointLightComponent>(entity).color);
                return 1;
            }
            if (registry.all_of<SpotLightComponent>(entity))
            {
                PushVec3(state, registry.get<SpotLightComponent>(entity).color);
                return 1;
            }

            lua_pushnil(state);
            return 1;
        }

        int LuaLightProxySetColor(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetLightEntityFromProxy(state, 1, entity))
            {
                return 0;
            }

            std::array<float, 3> color {};
            if (!TryReadVec3Argument(state, 2, color))
            {
                return 0;
            }

            for (float& channel : color)
            {
                channel = std::clamp(channel, 0.0f, 1.0f);
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<DirectionalLightComponent>(entity))
            {
                registry.get<DirectionalLightComponent>(entity).color = color;
            }
            if (registry.all_of<PointLightComponent>(entity))
            {
                registry.get<PointLightComponent>(entity).color = color;
            }
            if (registry.all_of<SpotLightComponent>(entity))
            {
                registry.get<SpotLightComponent>(entity).color = color;
            }
            return 0;
        }

        int LuaLightProxyGetRange(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetLightEntityFromProxy(state, 1, entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<PointLightComponent>(entity))
            {
                lua_pushnumber(state, registry.get<PointLightComponent>(entity).range);
                return 1;
            }
            if (registry.all_of<SpotLightComponent>(entity))
            {
                lua_pushnumber(state, registry.get<SpotLightComponent>(entity).range);
                return 1;
            }

            lua_pushnil(state);
            return 1;
        }

        int LuaLightProxySetRange(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetLightEntityFromProxy(state, 1, entity) || !lua_isnumber(state, 2))
            {
                return 0;
            }

            const float range = std::max(0.0f, static_cast<float>(lua_tonumber(state, 2)));
            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<PointLightComponent>(entity))
            {
                registry.get<PointLightComponent>(entity).range = range;
            }
            if (registry.all_of<SpotLightComponent>(entity))
            {
                registry.get<SpotLightComponent>(entity).range = range;
            }
            return 0;
        }

        void PushMaterialProxy(lua_State* state, const EntityID entity)
        {
            lua_newtable(state);
            lua_pushinteger(state, static_cast<lua_Integer>(static_cast<entt::id_type>(entity)));
            lua_setfield(state, -2, "__entity");
            lua_pushcfunction(state, LuaMaterialProxyGetColor);
            lua_setfield(state, -2, "GetColor");
            lua_pushcfunction(state, LuaMaterialProxySetColor);
            lua_setfield(state, -2, "SetColor");
            lua_pushcfunction(state, LuaMaterialProxyGetMetallic);
            lua_setfield(state, -2, "GetMetallic");
            lua_pushcfunction(state, LuaMaterialProxySetMetallic);
            lua_setfield(state, -2, "SetMetallic");
            lua_pushcfunction(state, LuaMaterialProxyGetSmoothness);
            lua_setfield(state, -2, "GetSmoothness");
            lua_pushcfunction(state, LuaMaterialProxySetSmoothness);
            lua_setfield(state, -2, "SetSmoothness");
        }

        void PushRigidBodyProxy(lua_State* state, const EntityID entity)
        {
            lua_newtable(state);
            lua_pushinteger(state, static_cast<lua_Integer>(static_cast<entt::id_type>(entity)));
            lua_setfield(state, -2, "__entity");
            lua_pushcfunction(state, LuaRigidBodyProxyGetVelocity);
            lua_setfield(state, -2, "GetVelocity");
            lua_pushcfunction(state, LuaRigidBodyProxySetVelocity);
            lua_setfield(state, -2, "SetVelocity");
            lua_pushcfunction(state, LuaRigidBodyProxyGetAngularVelocity);
            lua_setfield(state, -2, "GetAngularVelocity");
            lua_pushcfunction(state, LuaRigidBodyProxySetAngularVelocity);
            lua_setfield(state, -2, "SetAngularVelocity");
            lua_pushcfunction(state, LuaRigidBodyProxyAddImpulse);
            lua_setfield(state, -2, "AddImpulse");
            lua_pushcfunction(state, LuaRigidBodyProxyWake);
            lua_setfield(state, -2, "Wake");
            lua_pushcfunction(state, LuaRigidBodyProxyGetMass);
            lua_setfield(state, -2, "GetMass");
            lua_pushcfunction(state, LuaRigidBodyProxySetMass);
            lua_setfield(state, -2, "SetMass");
            lua_pushcfunction(state, LuaRigidBodyProxyGetUseGravity);
            lua_setfield(state, -2, "GetUseGravity");
            lua_pushcfunction(state, LuaRigidBodyProxySetUseGravity);
            lua_setfield(state, -2, "SetUseGravity");
            lua_pushcfunction(state, LuaRigidBodyProxyGetBodyType);
            lua_setfield(state, -2, "GetBodyType");
            lua_pushcfunction(state, LuaRigidBodyProxySetBodyType);
            lua_setfield(state, -2, "SetBodyType");
            lua_pushcfunction(state, LuaRigidBodyProxyIsSleeping);
            lua_setfield(state, -2, "IsSleeping");
        }

        void PushLightProxy(lua_State* state, const EntityID entity)
        {
            lua_newtable(state);
            lua_pushinteger(state, static_cast<lua_Integer>(static_cast<entt::id_type>(entity)));
            lua_setfield(state, -2, "__entity");
            lua_pushcfunction(state, LuaLightProxyGetIntensity);
            lua_setfield(state, -2, "GetIntensity");
            lua_pushcfunction(state, LuaLightProxySetIntensity);
            lua_setfield(state, -2, "SetIntensity");
            lua_pushcfunction(state, LuaLightProxyGetColor);
            lua_setfield(state, -2, "GetColor");
            lua_pushcfunction(state, LuaLightProxySetColor);
            lua_setfield(state, -2, "SetColor");
            lua_pushcfunction(state, LuaLightProxyGetRange);
            lua_setfield(state, -2, "GetRange");
            lua_pushcfunction(state, LuaLightProxySetRange);
            lua_setfield(state, -2, "SetRange");
        }

        int LuaEntityGetMaterial(lua_State* state)
        {
            EntityID entity = entt::null;
            MaterialComponent* material = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetMaterialComponent(entity, material))
            {
                lua_pushnil(state);
                return 1;
            }

            PushMaterialProxy(state, entity);
            return 1;
        }

        int LuaEntityGetRigidBody(lua_State* state)
        {
            EntityID entity = entt::null;
            RigidBodyComponent* body = nullptr;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetRigidBodyComponent(entity, body))
            {
                lua_pushnil(state);
                return 1;
            }

            PushRigidBodyProxy(state, entity);
            return 1;
        }

        int LuaEntityGetLight(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !TryGetLightEntity(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            PushLightProxy(state, entity);
            return 1;
        }

        int LuaCameraGetFOV(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<CameraComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            lua_pushnumber(state, registry.get<CameraComponent>(entity).fovDegrees);
            return 1;
        }

        int LuaCameraSetFOV(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity) || !lua_isnumber(state, 2))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<CameraComponent>(entity))
            {
                registry.get<CameraComponent>(entity).fovDegrees =
                    std::clamp(static_cast<float>(lua_tonumber(state, 2)), 1.0f, 179.0f);
            }

            return 0;
        }

        int LuaCameraGetNearClip(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<CameraComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            lua_pushnumber(state, registry.get<CameraComponent>(entity).nearClip);
            return 1;
        }

        int LuaCameraSetNearClip(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity) || !lua_isnumber(state, 2))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<CameraComponent>(entity))
            {
                registry.get<CameraComponent>(entity).nearClip =
                    std::max(0.001f, static_cast<float>(lua_tonumber(state, 2)));
            }

            return 0;
        }

        int LuaCameraGetFarClip(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<CameraComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            lua_pushnumber(state, registry.get<CameraComponent>(entity).farClip);
            return 1;
        }

        int LuaCameraSetFarClip(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity) || !lua_isnumber(state, 2))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<CameraComponent>(entity))
            {
                auto& camera = registry.get<CameraComponent>(entity);
                camera.farClip = std::max(camera.nearClip + 0.001f, static_cast<float>(lua_tonumber(state, 2)));
            }

            return 0;
        }

        int LuaCameraGetPrimary(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<CameraComponent>(entity))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, registry.get<CameraComponent>(entity).primary ? 1 : 0);
            return 1;
        }

        int LuaCameraSetPrimary(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity) || !lua_isboolean(state, 2))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<CameraComponent>(entity))
            {
                registry.get<CameraComponent>(entity).primary = lua_toboolean(state, 2) != 0;
            }

            return 0;
        }

        int LuaCameraGetActive(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<CameraComponent>(entity))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, registry.get<CameraComponent>(entity).active ? 1 : 0);
            return 1;
        }

        int LuaCameraSetActive(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity) || !lua_isboolean(state, 2))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<CameraComponent>(entity))
            {
                registry.get<CameraComponent>(entity).active = lua_toboolean(state, 2) != 0;
            }

            return 0;
        }

        int LuaCameraGetRenderPriority(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<CameraComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            lua_pushinteger(state, registry.get<CameraComponent>(entity).renderPriority);
            return 1;
        }

        int LuaCameraSetRenderPriority(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity) || !lua_isinteger(state, 2))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<CameraComponent>(entity))
            {
                registry.get<CameraComponent>(entity).renderPriority = static_cast<int>(lua_tointeger(state, 2));
            }

            return 0;
        }

        int LuaColliderGetTrigger(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<ColliderComponent>(entity))
            {
                lua_pushboolean(state, 0);
                return 1;
            }

            lua_pushboolean(state, registry.get<ColliderComponent>(entity).isTrigger ? 1 : 0);
            return 1;
        }

        int LuaColliderSetTrigger(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity) || !lua_isboolean(state, 2))
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<ColliderComponent>(entity))
            {
                registry.get<ColliderComponent>(entity).isTrigger = lua_toboolean(state, 2) != 0;
            }

            return 0;
        }

        int LuaColliderGetShape(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (!registry.all_of<ColliderComponent>(entity))
            {
                lua_pushnil(state);
                return 1;
            }

            lua_pushstring(state, ColliderShapeTypeName(registry.get<ColliderComponent>(entity).shape));
            return 1;
        }

        int LuaColliderSetShape(lua_State* state)
        {
            EntityID entity = entt::null;
            if (!TryGetEntityFromLua(state, 1, entity) || !IsSceneEntityValid(entity))
            {
                return 0;
            }

            const auto shape = ParseColliderShapeType(LuaHelpers::ToString(state, 2));
            if (!shape.has_value())
            {
                return 0;
            }

            auto& registry = g_ActiveScriptScene->GetRegistry();
            if (registry.all_of<ColliderComponent>(entity))
            {
                registry.get<ColliderComponent>(entity).shape = *shape;
            }

            return 0;
        }

        void RegisterRuntimeTables(lua_State* state)
        {
            lua_getglobal(state, "Luma");
            if (!lua_istable(state, -1))
            {
                lua_pop(state, 1);
                lua_newtable(state);
            }

            lua_newtable(state);
            lua_pushcfunction(state, LuaTimeGetDeltaSeconds);
            lua_setfield(state, -2, "GetDeltaSeconds");
            lua_pushcfunction(state, LuaTimeGetElapsedSeconds);
            lua_setfield(state, -2, "GetElapsedSeconds");
            lua_setfield(state, -2, "Time");

            lua_newtable(state);
            lua_pushcfunction(state, LuaInputIsKeyDown);
            lua_setfield(state, -2, "IsKeyDown");
            lua_pushcfunction(state, LuaInputWasKeyPressed);
            lua_setfield(state, -2, "WasKeyPressed");
            lua_setfield(state, -2, "Input");

            lua_newtable(state);
            lua_pushcfunction(state, LuaEntityIsValid);
            lua_setfield(state, -2, "IsValid");
            lua_pushcfunction(state, LuaEntityGetName);
            lua_setfield(state, -2, "GetName");
            lua_pushcfunction(state, LuaEntitySetName);
            lua_setfield(state, -2, "SetName");
            lua_pushcfunction(state, LuaEntityGetTag);
            lua_setfield(state, -2, "GetTag");
            lua_pushcfunction(state, LuaEntitySetTag);
            lua_setfield(state, -2, "SetTag");
            lua_pushcfunction(state, LuaEntityFindByName);
            lua_setfield(state, -2, "FindByName");
            lua_pushcfunction(state, LuaEntityFindFirstByTag);
            lua_setfield(state, -2, "FindFirstByTag");
            lua_pushcfunction(state, LuaEntityHasComponent);
            lua_setfield(state, -2, "HasComponent");
            lua_pushcfunction(state, LuaEntityGetParent);
            lua_setfield(state, -2, "GetParent");
            lua_pushcfunction(state, LuaEntityGetChildren);
            lua_setfield(state, -2, "GetChildren");
            lua_pushcfunction(state, LuaEntityGetMaterial);
            lua_setfield(state, -2, "GetMaterial");
            lua_pushcfunction(state, LuaEntityGetRigidBody);
            lua_setfield(state, -2, "GetRigidBody");
            lua_pushcfunction(state, LuaEntityGetLight);
            lua_setfield(state, -2, "GetLight");
            lua_pushcfunction(state, LuaEntityGetAudioSource);
            lua_setfield(state, -2, "GetAudioSource");
            lua_pushcfunction(state, LuaEntityGetAudioListener);
            lua_setfield(state, -2, "GetAudioListener");
            lua_pushcfunction(state, LuaEntityAddComponent);
            lua_setfield(state, -2, "AddComponent");
            lua_pushcfunction(state, LuaEntityRemoveComponent);
            lua_setfield(state, -2, "RemoveComponent");
            lua_setfield(state, -2, "Entity");

            lua_newtable(state);
            lua_pushcfunction(state, LuaSceneCreateEntity);
            lua_setfield(state, -2, "CreateEntity");
            lua_pushcfunction(state, LuaSceneDestroyEntity);
            lua_setfield(state, -2, "DestroyEntity");
            lua_pushcfunction(state, LuaSceneSetParent);
            lua_setfield(state, -2, "SetParent");
            lua_pushcfunction(state, LuaSceneUnparent);
            lua_setfield(state, -2, "Unparent");
            lua_pushcfunction(state, LuaSceneGetRootEntities);
            lua_setfield(state, -2, "GetRootEntities");
            lua_pushcfunction(state, LuaSceneCreatePrimitive);
            lua_setfield(state, -2, "CreatePrimitive");
            lua_setfield(state, -2, "Scene");

            lua_newtable(state);
            lua_pushcfunction(state, LuaAudioIsInitialized);
            lua_setfield(state, -2, "IsInitialized");
            lua_pushcfunction(state, LuaAudioHasPlaybackDevice);
            lua_setfield(state, -2, "HasPlaybackDevice");
            lua_pushcfunction(state, LuaAudioPreload);
            lua_setfield(state, -2, "Preload");
            lua_pushcfunction(state, LuaAudioPlay2D);
            lua_setfield(state, -2, "Play2D");
            lua_pushcfunction(state, LuaAudioPlayOneShot);
            lua_setfield(state, -2, "PlayOneShot");
            lua_pushcfunction(state, LuaAudioPlay3D);
            lua_setfield(state, -2, "Play3D");
            lua_pushcfunction(state, LuaAudioStop);
            lua_setfield(state, -2, "Stop");
            lua_pushcfunction(state, LuaAudioPause);
            lua_setfield(state, -2, "Pause");
            lua_pushcfunction(state, LuaAudioResume);
            lua_setfield(state, -2, "Resume");
            lua_pushcfunction(state, LuaAudioIsPlaying);
            lua_setfield(state, -2, "IsPlaying");
            lua_pushcfunction(state, LuaAudioSetVolume);
            lua_setfield(state, -2, "SetVolume");
            lua_pushcfunction(state, LuaAudioSetPitch);
            lua_setfield(state, -2, "SetPitch");
            lua_pushcfunction(state, LuaAudioSetPosition);
            lua_setfield(state, -2, "SetPosition");
            lua_pushcfunction(state, LuaAudioSetMasterVolume);
            lua_setfield(state, -2, "SetMasterVolume");
            lua_pushcfunction(state, LuaAudioGetMasterVolume);
            lua_setfield(state, -2, "GetMasterVolume");
            lua_pushcfunction(state, LuaAudioSetListenerTransform);
            lua_setfield(state, -2, "SetListenerTransform");
            lua_setfield(state, -2, "Audio");

            lua_newtable(state);
            lua_pushcfunction(state, LuaAudioSourcePlay);
            lua_setfield(state, -2, "Play");
            lua_pushcfunction(state, LuaAudioSourcePlayOneShot);
            lua_setfield(state, -2, "PlayOneShot");
            lua_pushcfunction(state, LuaAudioSourceStop);
            lua_setfield(state, -2, "Stop");
            lua_pushcfunction(state, LuaAudioSourcePause);
            lua_setfield(state, -2, "Pause");
            lua_pushcfunction(state, LuaAudioSourceResume);
            lua_setfield(state, -2, "Resume");
            lua_pushcfunction(state, LuaAudioSourceIsPlaying);
            lua_setfield(state, -2, "IsPlaying");
            lua_pushcfunction(state, LuaAudioSourceGetClip);
            lua_setfield(state, -2, "GetClip");
            lua_pushcfunction(state, LuaAudioSourceSetClip);
            lua_setfield(state, -2, "SetClip");
            lua_pushcfunction(state, LuaAudioSourceGetVolume);
            lua_setfield(state, -2, "GetVolume");
            lua_pushcfunction(state, LuaAudioSourceSetVolume);
            lua_setfield(state, -2, "SetVolume");
            lua_pushcfunction(state, LuaAudioSourceGetPitch);
            lua_setfield(state, -2, "GetPitch");
            lua_pushcfunction(state, LuaAudioSourceSetPitch);
            lua_setfield(state, -2, "SetPitch");
            lua_pushcfunction(state, LuaAudioSourceGetLooping);
            lua_setfield(state, -2, "GetLooping");
            lua_pushcfunction(state, LuaAudioSourceSetLooping);
            lua_setfield(state, -2, "SetLooping");
            lua_pushcfunction(state, LuaAudioSourceGetSpatialized);
            lua_setfield(state, -2, "GetSpatialized");
            lua_pushcfunction(state, LuaAudioSourceSetSpatialized);
            lua_setfield(state, -2, "SetSpatialized");
            lua_pushcfunction(state, LuaAudioSourceGetMute);
            lua_setfield(state, -2, "GetMute");
            lua_pushcfunction(state, LuaAudioSourceSetMute);
            lua_setfield(state, -2, "SetMute");
            lua_setfield(state, -2, "AudioSource");

            lua_newtable(state);
            lua_pushcfunction(state, LuaAudioListenerGetEnabled);
            lua_setfield(state, -2, "GetEnabled");
            lua_pushcfunction(state, LuaAudioListenerSetEnabled);
            lua_setfield(state, -2, "SetEnabled");
            lua_pushcfunction(state, LuaAudioListenerGetVolume);
            lua_setfield(state, -2, "GetVolume");
            lua_pushcfunction(state, LuaAudioListenerSetVolume);
            lua_setfield(state, -2, "SetVolume");
            lua_setfield(state, -2, "AudioListener");

            lua_newtable(state);
            lua_pushcfunction(state, LuaTransformGetPosition);
            lua_setfield(state, -2, "GetPosition");
            lua_pushcfunction(state, LuaTransformSetPosition);
            lua_setfield(state, -2, "SetPosition");
            lua_pushcfunction(state, LuaTransformTranslate);
            lua_setfield(state, -2, "Translate");
            lua_pushcfunction(state, LuaTransformGetRotation);
            lua_setfield(state, -2, "GetRotation");
            lua_pushcfunction(state, LuaTransformSetRotation);
            lua_setfield(state, -2, "SetRotation");
            lua_pushcfunction(state, LuaTransformGetScale);
            lua_setfield(state, -2, "GetScale");
            lua_pushcfunction(state, LuaTransformSetScale);
            lua_setfield(state, -2, "SetScale");
            lua_setfield(state, -2, "Transform");

            lua_newtable(state);
            lua_pushcfunction(state, LuaRendererGetColor);
            lua_setfield(state, -2, "GetColor");
            lua_pushcfunction(state, LuaRendererSetColor);
            lua_setfield(state, -2, "SetColor");
            lua_pushcfunction(state, LuaRendererGetVisible);
            lua_setfield(state, -2, "GetVisible");
            lua_pushcfunction(state, LuaRendererSetVisible);
            lua_setfield(state, -2, "SetVisible");
            lua_pushcfunction(state, LuaRendererGetPrimitive);
            lua_setfield(state, -2, "GetPrimitive");
            lua_pushcfunction(state, LuaRendererSetPrimitive);
            lua_setfield(state, -2, "SetPrimitive");
            lua_setfield(state, -2, "Renderer");

            lua_newtable(state);
            lua_pushcfunction(state, LuaMaterialGetColor);
            lua_setfield(state, -2, "GetColor");
            lua_pushcfunction(state, LuaMaterialSetColor);
            lua_setfield(state, -2, "SetColor");
            lua_pushcfunction(state, LuaMaterialGetMetallic);
            lua_setfield(state, -2, "GetMetallic");
            lua_pushcfunction(state, LuaMaterialSetMetallic);
            lua_setfield(state, -2, "SetMetallic");
            lua_pushcfunction(state, LuaMaterialGetSmoothness);
            lua_setfield(state, -2, "GetSmoothness");
            lua_pushcfunction(state, LuaMaterialSetSmoothness);
            lua_setfield(state, -2, "SetSmoothness");
            lua_setfield(state, -2, "Material");

            lua_newtable(state);
            lua_pushcfunction(state, LuaRigidBodyGetVelocity);
            lua_setfield(state, -2, "GetVelocity");
            lua_pushcfunction(state, LuaRigidBodySetVelocity);
            lua_setfield(state, -2, "SetVelocity");
            lua_pushcfunction(state, LuaRigidBodyGetAngularVelocity);
            lua_setfield(state, -2, "GetAngularVelocity");
            lua_pushcfunction(state, LuaRigidBodySetAngularVelocity);
            lua_setfield(state, -2, "SetAngularVelocity");
            lua_pushcfunction(state, LuaRigidBodyAddImpulse);
            lua_setfield(state, -2, "AddImpulse");
            lua_pushcfunction(state, LuaRigidBodyWake);
            lua_setfield(state, -2, "Wake");
            lua_pushcfunction(state, LuaRigidBodyGetMass);
            lua_setfield(state, -2, "GetMass");
            lua_pushcfunction(state, LuaRigidBodySetMass);
            lua_setfield(state, -2, "SetMass");
            lua_pushcfunction(state, LuaRigidBodyGetUseGravity);
            lua_setfield(state, -2, "GetUseGravity");
            lua_pushcfunction(state, LuaRigidBodySetUseGravity);
            lua_setfield(state, -2, "SetUseGravity");
            lua_pushcfunction(state, LuaRigidBodyGetBodyType);
            lua_setfield(state, -2, "GetBodyType");
            lua_pushcfunction(state, LuaRigidBodySetBodyType);
            lua_setfield(state, -2, "SetBodyType");
            lua_pushcfunction(state, LuaRigidBodyIsSleeping);
            lua_setfield(state, -2, "IsSleeping");
            lua_setfield(state, -2, "RigidBody");

            lua_newtable(state);
            lua_pushcfunction(state, LuaLightGetIntensity);
            lua_setfield(state, -2, "GetIntensity");
            lua_pushcfunction(state, LuaLightSetIntensity);
            lua_setfield(state, -2, "SetIntensity");
            lua_pushcfunction(state, LuaLightGetColor);
            lua_setfield(state, -2, "GetColor");
            lua_pushcfunction(state, LuaLightSetColor);
            lua_setfield(state, -2, "SetColor");
            lua_pushcfunction(state, LuaLightGetRange);
            lua_setfield(state, -2, "GetRange");
            lua_pushcfunction(state, LuaLightSetRange);
            lua_setfield(state, -2, "SetRange");
            lua_setfield(state, -2, "Light");

            lua_newtable(state);
            lua_pushcfunction(state, LuaCameraGetFOV);
            lua_setfield(state, -2, "GetFOV");
            lua_pushcfunction(state, LuaCameraSetFOV);
            lua_setfield(state, -2, "SetFOV");
            lua_pushcfunction(state, LuaCameraGetNearClip);
            lua_setfield(state, -2, "GetNearClip");
            lua_pushcfunction(state, LuaCameraSetNearClip);
            lua_setfield(state, -2, "SetNearClip");
            lua_pushcfunction(state, LuaCameraGetFarClip);
            lua_setfield(state, -2, "GetFarClip");
            lua_pushcfunction(state, LuaCameraSetFarClip);
            lua_setfield(state, -2, "SetFarClip");
            lua_pushcfunction(state, LuaCameraGetPrimary);
            lua_setfield(state, -2, "GetPrimary");
            lua_pushcfunction(state, LuaCameraSetPrimary);
            lua_setfield(state, -2, "SetPrimary");
            lua_pushcfunction(state, LuaCameraGetActive);
            lua_setfield(state, -2, "GetActive");
            lua_pushcfunction(state, LuaCameraSetActive);
            lua_setfield(state, -2, "SetActive");
            lua_pushcfunction(state, LuaCameraGetRenderPriority);
            lua_setfield(state, -2, "GetRenderPriority");
            lua_pushcfunction(state, LuaCameraSetRenderPriority);
            lua_setfield(state, -2, "SetRenderPriority");
            lua_setfield(state, -2, "Camera");

            lua_newtable(state);
            lua_pushcfunction(state, LuaColliderGetTrigger);
            lua_setfield(state, -2, "GetTrigger");
            lua_pushcfunction(state, LuaColliderSetTrigger);
            lua_setfield(state, -2, "SetTrigger");
            lua_pushcfunction(state, LuaColliderGetShape);
            lua_setfield(state, -2, "GetShape");
            lua_pushcfunction(state, LuaColliderSetShape);
            lua_setfield(state, -2, "SetShape");
            lua_setfield(state, -2, "Collider");

            lua_setglobal(state, "Luma");
        }

        void CloneTableShallow(lua_State* state, const int sourceIndex)
        {
            const int absoluteSourceIndex = lua_absindex(state, sourceIndex);
            lua_newtable(state);
            lua_pushnil(state);
            while (lua_next(state, absoluteSourceIndex) != 0)
            {
                lua_pushvalue(state, -2);
                lua_insert(state, -2);
                lua_settable(state, -4);
            }
        }
    }

    LuaScriptRuntime::~LuaScriptRuntime()
    {
        Stop();
    }

    bool LuaScriptRuntime::HasInstance(const EntityID entity) const
    {
        return m_Instances.find(entity) != m_Instances.end();
    }

    bool LuaScriptRuntime::IsFaulted(const EntityID entity) const
    {
        if (const auto it = m_Instances.find(entity); it != m_Instances.end())
        {
            return it->second.faulted;
        }

        return m_LastErrors.find(entity) != m_LastErrors.end();
    }

    const std::filesystem::path* LuaScriptRuntime::FindInstanceSourcePath(const EntityID entity) const
    {
        if (const auto it = m_Instances.find(entity); it != m_Instances.end())
        {
            return &it->second.sourcePath;
        }

        return nullptr;
    }

    void LuaScriptRuntime::Start(Scene& scene)
    {
        Stop();

        if (!ScriptEngine::IsInitialized() && !ScriptEngine::Initialize())
        {
            LUMA_LOG_ERROR("Script", "LuaScriptRuntime failed to start because ScriptEngine did not initialize.");
            return;
        }

        ScriptEngine::RefreshPackagePaths();
        m_Running = true;
        m_Scene = &scene;

        if (lua_State* state = ScriptEngine::GetLuaState(); state != nullptr)
        {
            RegisterRuntimeTables(state);
        }

        SyncInstances(scene);
    }

    void LuaScriptRuntime::Update(Scene& scene, const float deltaTimeSeconds)
    {
        if (!m_Running)
        {
            return;
        }

        m_Scene = &scene;
        ProcessHotReloads(scene);
        SyncInstances(scene);
        std::vector<EntityID> orderedEntities;
        orderedEntities.reserve(m_Instances.size());
        for (const auto& [entity, instance] : m_Instances)
        {
            if (instance.enabled && !instance.faulted)
            {
                orderedEntities.push_back(entity);
            }
        }

        for (const EntityID entity : orderedEntities)
        {
            auto instanceIt = m_Instances.find(entity);
            if (instanceIt == m_Instances.end())
            {
                continue;
            }

            ScriptInstance& instance = instanceIt->second;
            if (!instance.started)
            {
                g_ActiveScriptScene = &scene;
                const bool started = CallMethod(scene, instance, "OnStart", 0.0f, false);
                g_ActiveScriptScene = nullptr;
                if (!started)
                {
                    instance.faulted = true;
                    continue;
                }

                instance.started = true;
            }

            g_ActiveScriptScene = &scene;
            const bool succeeded = CallMethod(scene, instance, "OnUpdate", deltaTimeSeconds, true);
            g_ActiveScriptScene = nullptr;
            if (!succeeded)
            {
                instance.faulted = true;
            }
        }

        for (const EntityID entity : orderedEntities)
        {
            auto instanceIt = m_Instances.find(entity);
            if (instanceIt == m_Instances.end())
            {
                continue;
            }

            ScriptInstance& instance = instanceIt->second;
            if (instance.faulted || !instance.enabled)
            {
                continue;
            }

            g_ActiveScriptScene = &scene;
            const bool succeeded = CallMethod(scene, instance, "OnLateUpdate", deltaTimeSeconds, true);
            g_ActiveScriptScene = nullptr;
            if (!succeeded)
            {
                instance.faulted = true;
            }
        }
    }

    void LuaScriptRuntime::RunFixedUpdates(Scene& scene, const std::uint32_t stepCount, const float fixedDeltaTimeSeconds)
    {
        if (!m_Running || stepCount == 0 || fixedDeltaTimeSeconds <= 0.0f)
        {
            return;
        }

        for (std::uint32_t stepIndex = 0; stepIndex < stepCount; ++stepIndex)
        {
            std::vector<EntityID> orderedEntities;
            orderedEntities.reserve(m_Instances.size());
            for (const auto& [entity, instance] : m_Instances)
            {
                if (instance.enabled && !instance.faulted)
                {
                    orderedEntities.push_back(entity);
                }
            }

            for (const EntityID entity : orderedEntities)
            {
                auto instanceIt = m_Instances.find(entity);
                if (instanceIt == m_Instances.end())
                {
                    continue;
                }

                ScriptInstance& instance = instanceIt->second;
                g_ActiveScriptScene = &scene;
                const bool succeeded = CallMethod(scene, instance, "OnFixedUpdate", fixedDeltaTimeSeconds, true);
                g_ActiveScriptScene = nullptr;
                if (!succeeded)
                {
                    instance.faulted = true;
                }
            }
        }
    }

    void LuaScriptRuntime::ProcessHotReloads(Scene& scene)
    {
        if (!IsHotReloadEnabled() || m_Definitions.empty())
        {
            return;
        }

        std::vector<std::string> changedScriptKeys;
        changedScriptKeys.reserve(m_Definitions.size());
        for (const auto& [key, definition] : m_Definitions)
        {
            if (definition.sourcePath.empty() || !definition.hasWriteTime)
            {
                continue;
            }

            std::filesystem::file_time_type currentWriteTime {};
            if (!TryGetFileWriteTime(definition.sourcePath, currentWriteTime) || currentWriteTime != definition.lastWriteTime)
            {
                changedScriptKeys.push_back(key);
            }
        }

        if (changedScriptKeys.empty())
        {
            return;
        }

        auto& registry = scene.GetRegistry();
        for (const std::string& changedKey : changedScriptKeys)
        {
            auto definitionIt = m_Definitions.find(changedKey);
            if (definitionIt == m_Definitions.end())
            {
                continue;
            }

            const ScriptDefinition definition = definitionIt->second;
            LUMA_LOG_INFO("Script", "Hot reloading Lua script: " + definition.sourcePath.string());

            ScriptEngine::InvalidateScriptMetadata(definition.assetPath);

            lua_State* state = ScriptEngine::GetLuaState();
            if (state != nullptr && definition.tableRef != LUA_NOREF)
            {
                luaL_unref(state, LUA_REGISTRYINDEX, definition.tableRef);
            }
            m_Definitions.erase(definitionIt);

            std::vector<EntityID> affectedEntities;
            affectedEntities.reserve(m_Instances.size());
            for (const auto& [entity, instance] : m_Instances)
            {
                if (NormalizePathKey(instance.assetPath) == changedKey)
                {
                    affectedEntities.push_back(entity);
                }
            }

            for (const EntityID entity : affectedEntities)
            {
                if (!registry.valid(entity) || !registry.all_of<LuaScriptComponent>(entity))
                {
                    DestroyInstance(scene, entity, true);
                    continue;
                }

                const LuaScriptComponent component = registry.get<LuaScriptComponent>(entity);
                DestroyInstance(scene, entity, true);
                if (!component.scriptAsset.empty())
                {
                    CreateInstance(scene, entity, component);
                }
            }
        }
    }

    void LuaScriptRuntime::DispatchPhysicsCallbacks(Scene& scene, const std::vector<PhysicsEvent>& events)
    {
        auto& registry = scene.GetRegistry();
        for (const PhysicsEvent& event : events)
        {
            const EntityID entity = static_cast<EntityID>(static_cast<entt::id_type>(event.entityA));
            const EntityID other = static_cast<EntityID>(static_cast<entt::id_type>(event.entityB));

            auto instanceIt = m_Instances.find(entity);
            if (instanceIt == m_Instances.end())
            {
                continue;
            }

            ScriptInstance& instance = instanceIt->second;
            if (!instance.enabled || instance.faulted)
            {
                continue;
            }

            if (!registry.valid(entity) || !registry.all_of<PhysicsEventsComponent>(entity))
            {
                continue;
            }

            const auto& events = registry.get<PhysicsEventsComponent>(entity);
            const char* methodName = nullptr;
            switch (event.type)
            {
            case PhysicsEventType::CollisionEnter:
                if (events.onCollisionEnter)
                {
                    methodName = "OnCollisionEnter";
                }
                break;
            case PhysicsEventType::CollisionStay:
                if (events.onCollisionStay)
                {
                    methodName = "OnCollisionStay";
                }
                break;
            case PhysicsEventType::CollisionExit:
                if (events.onCollisionExit)
                {
                    methodName = "OnCollisionExit";
                }
                break;
            case PhysicsEventType::TriggerEnter:
                if (events.onTriggerEnter)
                {
                    methodName = "OnTriggerEnter";
                }
                break;
            case PhysicsEventType::TriggerStay:
                if (events.onTriggerStay)
                {
                    methodName = "OnTriggerStay";
                }
                break;
            case PhysicsEventType::TriggerExit:
                if (events.onTriggerExit)
                {
                    methodName = "OnTriggerExit";
                }
                break;
            default:
                break;
            }

            if (methodName == nullptr)
            {
                continue;
            }

            g_ActiveScriptScene = &scene;
            const bool succeeded = CallMethod(scene, instance, methodName, other, true);
            g_ActiveScriptScene = nullptr;
            if (!succeeded)
            {
                instance.faulted = true;
            }
        }
    }

    bool LuaScriptRuntime::IsHotReloadEnabled() const
    {
        if (!Project::IsLoaded())
        {
            return true;
        }

        const auto& buildSettings = Project::GetBuildSettings();
        switch (Project::GetActiveBuildProfile())
        {
        case BuildProfile::Debug:
            return buildSettings.debug.enableHotReload;
        case BuildProfile::Development:
            return buildSettings.development.enableHotReload;
        case BuildProfile::Release:
            return buildSettings.release.enableHotReload;
        default:
            return true;
        }
    }

    void LuaScriptRuntime::Stop()
    {
        if (!m_Running)
        {
            m_Instances.clear();
            ClearDefinitions();
            m_LastErrors.clear();
            m_Scene = nullptr;
            return;
        }

        Scene* scene = m_Scene;
        if (scene != nullptr)
        {
            std::vector<EntityID> entities;
            entities.reserve(m_Instances.size());
            for (const auto& [entity, instance] : m_Instances)
            {
                (void)instance;
                entities.push_back(entity);
            }

            for (const EntityID entity : entities)
            {
                DestroyInstance(*scene, entity, true);
            }
        }
        else
        {
            if (lua_State* state = ScriptEngine::GetLuaState(); state != nullptr)
            {
                for (auto& [entity, instance] : m_Instances)
                {
                    (void)entity;
                    if (instance.tableRef != LUA_NOREF)
                    {
                        luaL_unref(state, LUA_REGISTRYINDEX, instance.tableRef);
                    }
                }
            }
            m_Instances.clear();
        }

        ClearDefinitions();
        m_LastErrors.clear();
        m_Scene = nullptr;
        m_Running = false;
        g_ActiveScriptScene = nullptr;
    }

    const std::string* LuaScriptRuntime::FindLastError(const EntityID entity) const
    {
        const auto it = m_LastErrors.find(entity);
        return it != m_LastErrors.end() ? &it->second : nullptr;
    }

    bool LuaScriptRuntime::SetProperty(const EntityID entity, const std::string& propertyName, const ScriptValue& value)
    {
        const auto instanceIt = m_Instances.find(entity);
        if (instanceIt == m_Instances.end())
        {
            return false;
        }

        lua_State* state = ScriptEngine::GetLuaState();
        if (state == nullptr)
        {
            return false;
        }

        const int stackBase = lua_gettop(state);
        lua_rawgeti(state, LUA_REGISTRYINDEX, instanceIt->second.tableRef);
        if (!lua_istable(state, -1))
        {
            lua_settop(state, stackBase);
            return false;
        }

        lua_getfield(state, -1, "Properties");
        if (!lua_istable(state, -1))
        {
            lua_pop(state, 1);
            lua_newtable(state);
        }

        g_ActiveScriptScene = m_Scene;
        SetNestedPropertyField(state, -1, propertyName, value);
        g_ActiveScriptScene = nullptr;
        lua_setfield(state, -2, "Properties");
        lua_settop(state, stackBase);
        return true;
    }

    void LuaScriptRuntime::SyncInstances(Scene& scene)
    {
        auto& registry = scene.GetRegistry();

        std::vector<EntityID> staleEntities;
        staleEntities.reserve(m_Instances.size());
        for (const auto& [entity, instance] : m_Instances)
        {
            (void)instance;
            if (!registry.valid(entity) || !registry.all_of<LuaScriptComponent>(entity))
            {
                staleEntities.push_back(entity);
                continue;
            }

            const LuaScriptComponent& component = registry.get<LuaScriptComponent>(entity);
            if (component.scriptAsset.empty())
            {
                staleEntities.push_back(entity);
            }
        }

        for (const EntityID entity : staleEntities)
        {
            DestroyInstance(scene, entity, true);
        }

        const auto view = registry.view<LuaScriptComponent>();
        for (const EntityID entity : view)
        {
            const LuaScriptComponent& component = view.get<LuaScriptComponent>(entity);
            if (component.scriptAsset.empty())
            {
                continue;
            }

            const auto instanceIt = m_Instances.find(entity);
            if (instanceIt == m_Instances.end())
            {
                CreateInstance(scene, entity, component);
                continue;
            }

            ScriptInstance& instance = instanceIt->second;
            const bool wasEnabled = instance.enabled;
            instance.enabled = component.enabled;
            if (instance.assetPath.generic_string() != std::filesystem::path(component.scriptAsset).generic_string())
            {
                DestroyInstance(scene, entity, true);
                CreateInstance(scene, entity, component);
                continue;
            }

            if (wasEnabled != instance.enabled)
            {
                const char* methodName = instance.enabled ? "OnEnable" : "OnDisable";
                g_ActiveScriptScene = &scene;
                const bool succeeded = CallMethod(scene, instance, methodName, 0.0f, false);
                g_ActiveScriptScene = nullptr;
                if (!succeeded)
                {
                    instance.faulted = true;
                    continue;
                }

                if (instance.enabled && !instance.started)
                {
                    g_ActiveScriptScene = &scene;
                    const bool started = CallMethod(scene, instance, "OnStart", 0.0f, false);
                    g_ActiveScriptScene = nullptr;
                    if (!started)
                    {
                        instance.faulted = true;
                        continue;
                    }

                    instance.started = true;
                }
            }
        }
    }

    bool LuaScriptRuntime::CreateInstance(Scene& scene, const EntityID entity, const LuaScriptComponent& component)
    {
        lua_State* state = ScriptEngine::GetLuaState();
        if (state == nullptr)
        {
            m_LastErrors[entity] = "ScriptEngine is not initialized.";
            return false;
        }

        ScriptDefinition definition {};
        const std::filesystem::path assetPath(component.scriptAsset);
        if (!EnsureScriptDefinition(assetPath, definition))
        {
            m_LastErrors[entity] = "Failed to resolve script definition.";
            return false;
        }

        lua_rawgeti(state, LUA_REGISTRYINDEX, definition.tableRef);
        if (!lua_istable(state, -1))
        {
            lua_pop(state, 1);
            m_LastErrors[entity] = "Script definition did not resolve to a Lua table.";
            return false;
        }

        lua_newtable(state);
        lua_newtable(state);
        lua_pushvalue(state, -3);
        lua_setfield(state, -2, "__index");
        lua_setmetatable(state, -2);

        lua_pushinteger(state, static_cast<lua_Integer>(static_cast<entt::id_type>(entity)));
        lua_setfield(state, -2, "entity");

        lua_getfield(state, -2, "Properties");
        if (lua_istable(state, -1))
        {
            CloneTableShallow(state, -1);
            lua_setfield(state, -3, "Properties");
        }
        lua_pop(state, 1);

        if (const LuaScriptAssetMetadata* metadata = ScriptEngine::GetScriptMetadata(assetPath))
        {
            g_ActiveScriptScene = &scene;
            ApplyResolvedPropertiesToInstanceTable(state, -1, *metadata, component);
            g_ActiveScriptScene = nullptr;
        }

        const int instanceRef = luaL_ref(state, LUA_REGISTRYINDEX);
        lua_pop(state, 1);

        ScriptInstance instance {};
        instance.entity = entity;
        instance.assetPath = assetPath;
        instance.sourcePath = definition.sourcePath;
        instance.tableRef = instanceRef;
        instance.enabled = component.enabled;
        instance.faulted = false;
        instance.started = false;

        m_Instances[entity] = instance;
        m_LastErrors.erase(entity);

        g_ActiveScriptScene = &scene;
        const bool created = CallMethod(scene, m_Instances[entity], "OnCreate", 0.0f, false);
        g_ActiveScriptScene = nullptr;
        if (!created)
        {
            m_Instances[entity].faulted = true;
            return false;
        }

        if (m_Instances[entity].enabled)
        {
            g_ActiveScriptScene = &scene;
            const bool enabled = CallMethod(scene, m_Instances[entity], "OnEnable", 0.0f, false);
            g_ActiveScriptScene = nullptr;
            if (!enabled)
            {
                m_Instances[entity].faulted = true;
                return false;
            }

            g_ActiveScriptScene = &scene;
            const bool started = CallMethod(scene, m_Instances[entity], "OnStart", 0.0f, false);
            g_ActiveScriptScene = nullptr;
            if (!started)
            {
                m_Instances[entity].faulted = true;
                return false;
            }

            m_Instances[entity].started = true;
        }

        return true;
    }

    void LuaScriptRuntime::DestroyInstance(Scene& scene, const EntityID entity, const bool callOnDestroy)
    {
        const auto instanceIt = m_Instances.find(entity);
        if (instanceIt == m_Instances.end())
        {
            m_LastErrors.erase(entity);
            return;
        }

        lua_State* state = ScriptEngine::GetLuaState();
        if (callOnDestroy)
        {
            g_ActiveScriptScene = &scene;
            CallMethod(scene, instanceIt->second, "OnDestroy", 0.0f, false);
            g_ActiveScriptScene = nullptr;
        }

        if (state != nullptr && instanceIt->second.tableRef != LUA_NOREF)
        {
            luaL_unref(state, LUA_REGISTRYINDEX, instanceIt->second.tableRef);
        }

        m_Instances.erase(instanceIt);
        m_LastErrors.erase(entity);
    }

    bool LuaScriptRuntime::EnsureScriptDefinition(const std::filesystem::path& assetPath, ScriptDefinition& outDefinition)
    {
        lua_State* state = ScriptEngine::GetLuaState();
        if (state == nullptr)
        {
            return false;
        }

        const std::string key = NormalizePathKey(assetPath);
        if (const auto it = m_Definitions.find(key); it != m_Definitions.end())
        {
            bool definitionStale = false;
            if (!it->second.sourcePath.empty() && it->second.hasWriteTime)
            {
                std::filesystem::file_time_type currentWriteTime {};
                definitionStale = !TryGetFileWriteTime(it->second.sourcePath, currentWriteTime) ||
                    currentWriteTime != it->second.lastWriteTime;
            }

            if (!definitionStale)
            {
                outDefinition = it->second;
                return true;
            }

            if (it->second.tableRef != LUA_NOREF)
            {
                luaL_unref(state, LUA_REGISTRYINDEX, it->second.tableRef);
            }
            m_Definitions.erase(it);
        }

        std::string resolveError;
        std::filesystem::path sourcePath;
        if (!ScriptEngine::ResolveScriptSourcePath(assetPath, sourcePath, &resolveError))
        {
            LUMA_LOG_ERROR("Script", "Lua script resolve failed | Script: " + MakeDisplayScriptPath(assetPath) + " | " + resolveError);
            return false;
        }

        const int stackBase = lua_gettop(state);
        const int tracebackIndex = LuaHelpers::PushTraceback(state);
        const std::string sourcePathString = sourcePath.string();

        if (luaL_loadfile(state, sourcePathString.c_str()) != LUA_OK)
        {
            const std::string error = LuaHelpers::PopError(state);
            lua_settop(state, stackBase);
            LUMA_LOG_ERROR("Script", FormatScriptLoadError(sourcePath, "load", error));
            return false;
        }

        if (lua_pcall(state, 0, 1, tracebackIndex) != LUA_OK)
        {
            const std::string error = LuaHelpers::PopError(state);
            lua_settop(state, stackBase);
            LUMA_LOG_ERROR("Script", FormatScriptLoadError(sourcePath, "execute", error));
            return false;
        }

        if (!lua_istable(state, -1))
        {
            lua_settop(state, stackBase);
            LUMA_LOG_ERROR(
                "Script",
                "Lua script execute failed | Script: " + MakeDisplayScriptPath(sourcePath) + " | Script must return a table.");
            return false;
        }

        ScriptDefinition definition {};
        definition.assetPath = assetPath;
        definition.sourcePath = sourcePath;
        definition.hasWriteTime = TryGetFileWriteTime(sourcePath, definition.lastWriteTime);
        definition.tableRef = luaL_ref(state, LUA_REGISTRYINDEX);
        m_Definitions.emplace(key, definition);
        outDefinition = definition;
        lua_settop(state, stackBase);
        return true;
    }

    bool LuaScriptRuntime::CallMethod(
        Scene& scene,
        ScriptInstance& instance,
        const char* methodName,
        const float optionalNumberArg,
        const bool passNumberArg)
    {
        lua_State* state = ScriptEngine::GetLuaState();
        if (state == nullptr)
        {
            return false;
        }

        const int stackBase = lua_gettop(state);
        const int tracebackIndex = LuaHelpers::PushTraceback(state);
        lua_rawgeti(state, LUA_REGISTRYINDEX, instance.tableRef);
        if (!lua_istable(state, -1))
        {
            lua_settop(state, stackBase);
            return false;
        }

        lua_getfield(state, -1, methodName);
        if (lua_isnil(state, -1))
        {
            lua_settop(state, stackBase);
            return true;
        }

        if (!lua_isfunction(state, -1))
        {
            const std::string error = FormatRuntimeScriptError(
                scene,
                instance.entity,
                instance.sourcePath,
                methodName,
                std::string(methodName) + " exists but is not a function.");
            m_LastErrors[instance.entity] = error;
            lua_settop(state, stackBase);
            LUMA_LOG_ERROR("Script", error);
            return false;
        }

        lua_pushvalue(state, -2);
        int argumentCount = 1;
        if (passNumberArg)
        {
            lua_pushnumber(state, optionalNumberArg);
            ++argumentCount;
        }

        if (lua_pcall(state, argumentCount, 0, tracebackIndex) != LUA_OK)
        {
            const std::string error = LuaHelpers::PopError(state);
            const std::string message =
                FormatRuntimeScriptError(scene, instance.entity, instance.sourcePath, methodName, error);
            m_LastErrors[instance.entity] = message;
            lua_settop(state, stackBase);
            LUMA_LOG_ERROR("Script", message);
            return false;
        }

        lua_settop(state, stackBase);
        return true;
    }

    bool LuaScriptRuntime::CallMethod(
        Scene& scene,
        ScriptInstance& instance,
        const char* methodName,
        const EntityID optionalEntityArg,
        const bool passEntityArg)
    {
        lua_State* state = ScriptEngine::GetLuaState();
        if (state == nullptr)
        {
            return false;
        }

        const int stackBase = lua_gettop(state);
        const int tracebackIndex = LuaHelpers::PushTraceback(state);
        lua_rawgeti(state, LUA_REGISTRYINDEX, instance.tableRef);
        if (!lua_istable(state, -1))
        {
            lua_settop(state, stackBase);
            return false;
        }

        lua_getfield(state, -1, methodName);
        if (lua_isnil(state, -1))
        {
            lua_settop(state, stackBase);
            return true;
        }

        if (!lua_isfunction(state, -1))
        {
            const std::string error = FormatRuntimeScriptError(
                scene,
                instance.entity,
                instance.sourcePath,
                methodName,
                std::string(methodName) + " exists but is not a function.");
            m_LastErrors[instance.entity] = error;
            lua_settop(state, stackBase);
            LUMA_LOG_ERROR("Script", error);
            return false;
        }

        lua_pushvalue(state, -2);
        int argumentCount = 1;
        if (passEntityArg)
        {
            lua_pushinteger(state, static_cast<lua_Integer>(static_cast<entt::id_type>(optionalEntityArg)));
            ++argumentCount;
        }

        if (lua_pcall(state, argumentCount, 0, tracebackIndex) != LUA_OK)
        {
            const std::string error = LuaHelpers::PopError(state);
            const std::string message =
                FormatRuntimeScriptError(scene, instance.entity, instance.sourcePath, methodName, error);
            m_LastErrors[instance.entity] = message;
            lua_settop(state, stackBase);
            LUMA_LOG_ERROR("Script", message);
            return false;
        }

        lua_settop(state, stackBase);
        return true;
    }

    std::string LuaScriptRuntime::NormalizePathKey(const std::filesystem::path& path) const
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

    void LuaScriptRuntime::ClearDefinitions()
    {
        lua_State* state = ScriptEngine::GetLuaState();
        if (state != nullptr)
        {
            for (auto& [key, definition] : m_Definitions)
            {
                (void)key;
                if (definition.tableRef != LUA_NOREF)
                {
                    luaL_unref(state, LUA_REGISTRYINDEX, definition.tableRef);
                }
            }
        }
        m_Definitions.clear();
    }
}
