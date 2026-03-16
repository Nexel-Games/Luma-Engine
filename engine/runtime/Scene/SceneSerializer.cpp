#include "Luma/Scene/SceneSerializer.h"

#include <exception>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Scene/AudioListenerComponent.h"
#include "Luma/Scene/AudioSourceComponent.h"
#include "Luma/Scene/BuoyancyComponent.h"
#include "Luma/Scene/CameraComponent.h"
#include "Luma/Scene/CharacterControllerComponent.h"
#include "Luma/Scene/ColliderComponent.h"
#include "Luma/Scene/D6JointComponent.h"
#include "Luma/Scene/DestructibleComponent.h"
#include "Luma/Scene/DirectionalLightComponent.h"
#include "Luma/Scene/PointLightComponent.h"
#include "Luma/Scene/FixedJointComponent.h"
#include "Luma/Scene/ForceFieldComponent.h"
#include "Luma/Scene/HingeJointComponent.h"
#include "Luma/Scene/IDComponent.h"
#include "Luma/Scene/JointComponent.h"
#include "Luma/Scene/LuaScriptComponent.h"
#include "Luma/Scene/MaterialComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/PhysicsEventsComponent.h"
#include "Luma/Scene/PrefabInstanceComponent.h"
#include "Luma/Scene/PostProcessComponent.h"
#include "Luma/Scene/RagdollComponent.h"
#include "Luma/Scene/RelationshipComponent.h"
#include "Luma/Scene/RigidBodyComponent.h"
#include "Luma/Scene/Scene.h"
#include "Luma/Scene/SliderJointComponent.h"
#include "Luma/Scene/SkyLightComponent.h"
#include "Luma/Scene/SpotLightComponent.h"
#include "Luma/Scene/TagComponent.h"
#include "Luma/Scene/TransformComponent.h"
#include "Luma/Scene/VehicleComponent.h"
#include "Luma/Scene/VehicleInputComponent.h"
#include "Luma/Scene/WheelColliderComponent.h"
#include "Luma/Scripting/ScriptEngine.h"

namespace Luma
{
    namespace
    {
        using json = nlohmann::json;

        template <typename Func>
        void VisitEntityHierarchyInOrder(const Scene& scene, const Func& visitor)
        {
            const auto& registry = scene.GetRegistry();
            std::function<void(EntityID)> visitRecursive;
            visitRecursive = [&](const EntityID entity)
            {
                if (!registry.valid(entity))
                {
                    return;
                }

                visitor(entity);

                if (!registry.all_of<RelationshipComponent>(entity))
                {
                    return;
                }

                const auto& relationship = registry.get<RelationshipComponent>(entity);
                for (const EntityID child : relationship.children)
                {
                    visitRecursive(child);
                }
            };

            const auto roots = scene.GetRootEntities();
            for (const EntityID root : roots)
            {
                visitRecursive(root);
            }
        }

        template <typename T, std::size_t N>
        json ArrayToJson(const std::array<T, N>& values)
        {
            json result = json::array();
            for (const T& value : values)
            {
                result.push_back(value);
            }
            return result;
        }

        template <typename T, std::size_t N>
        bool ReadArrayField(
            const json& object,
            const char* key,
            std::array<T, N>& outValues,
            std::string& outError)
        {
            const auto found = object.find(key);
            if (found == object.end())
            {
                return true;
            }

            if (!found->is_array() || found->size() != N)
            {
                outError = std::string("Scene field '") + key + "' must be an array of length " + std::to_string(N) + ".";
                return false;
            }

            for (std::size_t i = 0; i < N; ++i)
            {
                outValues[i] = (*found)[i].get<T>();
            }

            return true;
        }

        template <typename Enum>
        int EnumToInt(const Enum value)
        {
            return static_cast<int>(value);
        }

        template <typename Enum>
        Enum IntToEnum(const int value)
        {
            return static_cast<Enum>(value);
        }

        json ScriptValueToJson(const ScriptValue& value)
        {
            json result;
            result["type"] = EnumToInt(value.type);

            switch (value.type)
            {
            case ScriptValueType::Bool:
                result["value"] = value.boolValue;
                break;
            case ScriptValueType::Int:
                result["value"] = value.intValue;
                break;
            case ScriptValueType::Float:
                result["value"] = value.floatValue;
                break;
            case ScriptValueType::String:
                result["value"] = value.stringValue;
                break;
            case ScriptValueType::Entity:
                result["value"] = value.entityValue;
                break;
            case ScriptValueType::Vec2:
                result["value"] = ArrayToJson(value.vec2Value);
                break;
            case ScriptValueType::Vec3:
                result["value"] = ArrayToJson(value.vec3Value);
                break;
            case ScriptValueType::Vec4:
                result["value"] = ArrayToJson(value.vec4Value);
                break;
            case ScriptValueType::None:
            default:
                result["value"] = nullptr;
                break;
            }

            return result;
        }

        bool ReadScriptValueFromJson(const json& object, ScriptValue& outValue, std::string& outError)
        {
            if (!object.is_object())
            {
                outError = "Script property override must be an object.";
                return false;
            }

            outValue = {};
            outValue.type = IntToEnum<ScriptValueType>(object.value("type", EnumToInt(ScriptValueType::None)));
            const auto valueIt = object.find("value");
            if (valueIt == object.end())
            {
                return true;
            }

            switch (outValue.type)
            {
            case ScriptValueType::Bool:
                outValue.boolValue = valueIt->get<bool>();
                return true;
            case ScriptValueType::Int:
                outValue.intValue = valueIt->get<int>();
                return true;
            case ScriptValueType::Float:
                outValue.floatValue = valueIt->get<float>();
                return true;
            case ScriptValueType::String:
                outValue.stringValue = valueIt->get<std::string>();
                return true;
            case ScriptValueType::Entity:
                outValue.entityValue = valueIt->get<UUID>();
                return true;
            case ScriptValueType::Vec2:
                if (!ReadArrayField(object, "value", outValue.vec2Value, outError))
                {
                    return false;
                }
                return true;
            case ScriptValueType::Vec3:
                if (!ReadArrayField(object, "value", outValue.vec3Value, outError))
                {
                    return false;
                }
                return true;
            case ScriptValueType::Vec4:
                if (!ReadArrayField(object, "value", outValue.vec4Value, outError))
                {
                    return false;
                }
                return true;
            case ScriptValueType::None:
            default:
                return true;
            }
        }
    }

    bool SceneSerializer::Serialize(const Scene& scene, const std::filesystem::path& scenePath, std::string& outError)
    {
        outError.clear();
        try
        {
            std::error_code ec;
            const std::filesystem::path parentPath = scenePath.parent_path();
            if (!parentPath.empty())
            {
                std::filesystem::create_directories(parentPath, ec);
                if (ec)
                {
                    outError = "Failed to create scene directory: " + parentPath.string();
                    return false;
                }
            }

            const auto& registry = scene.GetRegistry();
            json root;
            root["schemaVersion"] = 3;
            root["entities"] = json::array();

            VisitEntityHierarchyInOrder(scene, [&](const EntityID entity)
            {
                if (!registry.all_of<IDComponent, TagComponent, TransformComponent, RelationshipComponent>(entity))
                {
                    return;
                }

                const auto& id = registry.get<IDComponent>(entity);
                const auto& tag = registry.get<TagComponent>(entity);
                const auto& transform = registry.get<TransformComponent>(entity);
                const auto& relationship = registry.get<RelationshipComponent>(entity);

                json entityJson;
                entityJson["uuid"] = id.id;
                entityJson["name"] = tag.name;
                entityJson["tag"] = tag.tag;
                entityJson["layer"] = tag.layer;

                if (relationship.parent != entt::null &&
                    registry.valid(relationship.parent) &&
                    registry.all_of<IDComponent>(relationship.parent))
                {
                    entityJson["parent"] = registry.get<IDComponent>(relationship.parent).id;
                }

                entityJson["transform"] = {
                    { "position", ArrayToJson(transform.position) },
                    { "rotation", ArrayToJson(transform.rotation) },
                    { "scale", ArrayToJson(transform.scale) }
                };

                if (const auto* component = registry.try_get<MeshRendererComponent>(entity))
                {
                    entityJson["meshRenderer"] = {
                        { "visible", component->visible },
                        { "usePrimitive", component->usePrimitive },
                        { "primitive", EnumToInt(component->primitive) },
                        { "meshSource", component->meshSource },
                        { "importedSceneSource", component->importedSceneSource },
                        { "meshPartIndex", component->meshPartIndex },
                        { "meshLod", component->meshLod },
                        { "autoStreamLod", component->autoStreamLod },
                        { "maxAutoLod", component->maxAutoLod },
                        { "lodNearDistance", component->lodNearDistance },
                        { "lodFarDistance", component->lodFarDistance },
                        { "streamSectionsByDistance", component->streamSectionsByDistance },
                        { "sectionLoadDistance", component->sectionLoadDistance },
                        { "staticLighting", component->staticLighting },
                        { "color", ArrayToJson(component->color) }
                    };
                }

                if (const auto* component = registry.try_get<MaterialComponent>(entity))
                {
                    entityJson["material"] = {
                        { "name", component->name },
                        { "shader", component->shader },
                        { "sharedMaterial", component->sharedMaterial },
                        { "renderingMode", EnumToInt(component->renderingMode) },
                        { "albedoColor", ArrayToJson(component->albedoColor) },
                        { "albedoTexture", component->albedoTexture },
                        { "metallicTexture", component->metallicTexture },
                        { "metallic", component->metallic },
                        { "smoothness", component->smoothness },
                        { "smoothnessSource", EnumToInt(component->smoothnessSource) },
                        { "enableHighlights", component->enableHighlights },
                        { "enableReflections", component->enableReflections },
                        { "normalTexture", component->normalTexture },
                        { "normalScale", component->normalScale },
                        { "heightTexture", component->heightTexture },
                        { "heightScale", component->heightScale },
                        { "occlusionTexture", component->occlusionTexture },
                        { "occlusionStrength", component->occlusionStrength },
                        { "emissionEnabled", component->emissionEnabled },
                        { "emissionTexture", component->emissionTexture },
                        { "emissionColor", ArrayToJson(component->emissionColor) },
                        { "emissionIntensity", component->emissionIntensity },
                        { "globalIllumination", EnumToInt(component->globalIllumination) },
                        { "detailMaskTexture", component->detailMaskTexture },
                        { "tiling", ArrayToJson(component->tiling) },
                        { "offset", ArrayToJson(component->offset) },
                        { "detailAlbedoTexture", component->detailAlbedoTexture },
                        { "detailNormalTexture", component->detailNormalTexture },
                        { "detailNormalScale", component->detailNormalScale },
                        { "detailTiling", ArrayToJson(component->detailTiling) },
                        { "detailOffset", ArrayToJson(component->detailOffset) },
                        { "uvSet", component->uvSet }
                    };
                }

                if (const auto* component = registry.try_get<CameraComponent>(entity))
                {
                    entityJson["camera"] = {
                        { "primary", component->primary },
                        { "active", component->active },
                        { "projection", EnumToInt(component->projection) },
                        { "fovDegrees", component->fovDegrees },
                        { "orthographicSize", component->orthographicSize },
                        { "nearClip", component->nearClip },
                        { "farClip", component->farClip },
                        { "sensorWidth", component->sensorWidth },
                        { "sensorHeight", component->sensorHeight },
                        { "focalLength", component->focalLength },
                        { "useViewportAspectRatio", component->useViewportAspectRatio },
                        { "aspectRatio", component->aspectRatio },
                        { "constrainAspectRatio", component->constrainAspectRatio },
                        { "clearMode", EnumToInt(component->clearMode) },
                        { "clearColor", ArrayToJson(component->clearColor) },
                        { "cullingMask", component->cullingMask },
                        { "hdr", component->hdr },
                        { "allowPostProcess", component->allowPostProcess },
                        { "allowMSAA", component->allowMSAA },
                        { "allowMotionBlur", component->allowMotionBlur },
                        { "exposure", component->exposure },
                        { "renderPriority", component->renderPriority }
                    };
                }

                if (const auto* component = registry.try_get<LuaScriptComponent>(entity))
                {
                    json scriptOverrides = json::object();
                    const LuaScriptAssetMetadata* metadata = nullptr;
                    if (!component->scriptAsset.empty())
                    {
                        metadata = ScriptEngine::GetScriptMetadata(component->scriptAsset);
                    }

                    std::vector<std::string> propertyNames;
                    propertyNames.reserve(component->propertyOverrides.size());
                    for (const auto& [propertyName, propertyValue] : component->propertyOverrides)
                    {
                        if (metadata != nullptr && !IsScriptPropertyOverrideValid(*metadata, propertyName, propertyValue))
                        {
                            continue;
                        }
                        propertyNames.push_back(propertyName);
                    }

                    std::sort(propertyNames.begin(), propertyNames.end());
                    for (const std::string& propertyName : propertyNames)
                    {
                        const auto overrideIt = component->propertyOverrides.find(propertyName);
                        if (overrideIt == component->propertyOverrides.end())
                        {
                            continue;
                        }
                        scriptOverrides[propertyName] = ScriptValueToJson(overrideIt->second);
                    }

                    entityJson["luaScript"] = {
                        { "enabled", component->enabled },
                        { "scriptAsset", component->scriptAsset },
                        { "propertyOverrides", std::move(scriptOverrides) }
                    };
                }

                if (const auto* component = registry.try_get<AudioSourceComponent>(entity))
                {
                    entityJson["audioSource"] = {
                        { "clipAsset", component->clipAsset },
                        { "playOnAwake", component->playOnAwake },
                        { "looping", component->looping },
                        { "spatialized", component->spatialized },
                        { "mute", component->mute },
                        { "volume", component->volume },
                        { "pitch", component->pitch },
                        { "minDistance", component->minDistance },
                        { "maxDistance", component->maxDistance }
                    };
                }

                if (const auto* component = registry.try_get<AudioListenerComponent>(entity))
                {
                    entityJson["audioListener"] = {
                        { "enabled", component->enabled },
                        { "volume", component->volume }
                    };
                }

                if (const auto* component = registry.try_get<PrefabInstanceComponent>(entity))
                {
                    entityJson["prefabInstance"] = {
                        { "prefabAsset", component->prefabAsset },
                        { "sourceEntityId", component->sourceEntityId },
                        { "isRoot", component->isRoot }
                    };
                }

                if (const auto* component = registry.try_get<DirectionalLightComponent>(entity))
                {
                    entityJson["directionalLight"] = {
                        { "active", component->active },
                        { "color", ArrayToJson(component->color) },
                        { "intensity", component->intensity },
                        { "castShadows", component->castShadows }
                    };
                }

                if (const auto* component = registry.try_get<PointLightComponent>(entity))
                {
                    entityJson["pointLight"] = {
                        { "active", component->active },
                        { "color", ArrayToJson(component->color) },
                        { "mode", static_cast<std::uint32_t>(component->mode) },
                        { "temperature", component->temperature },
                        { "intensity", component->intensity },
                        { "indirectMultiplier", component->indirectMultiplier },
                        { "range", component->range },
                        { "attenuation", component->attenuation },
                        { "castShadows", component->castShadows },
                        { "shadowType", static_cast<std::uint32_t>(component->shadowType) },
                        { "shadowBias", component->shadowBias },
                        { "shadowResolution", component->shadowResolution },
                        { "bakedShadowRadius", component->bakedShadowRadius },
                        { "drawHalo", component->drawHalo },
                        { "renderMode", static_cast<std::uint32_t>(component->renderMode) },
                        { "cullingMask", component->cullingMask }
                    };
                }

                if (const auto* component = registry.try_get<SpotLightComponent>(entity))
                {
                    entityJson["spotLight"] = {
                        { "active", component->active },
                        { "color", ArrayToJson(component->color) },
                        { "intensity", component->intensity },
                        { "range", component->range },
                        { "innerConeAngle", component->innerConeAngle },
                        { "outerConeAngle", component->outerConeAngle },
                        { "castShadows", component->castShadows },
                        { "volumetricScatteringIntensity", component->volumetricScatteringIntensity }
                    };
                }

                if (const auto* component = registry.try_get<SkyLightComponent>(entity))
                {
                    entityJson["skyLight"] = {
                        { "active", component->active },
                        { "color", ArrayToJson(component->color) },
                        { "intensity", component->intensity },
                        { "skyLightType", EnumToInt(component->skyLightType) },
                        { "castShadows", component->castShadows },
                        { "skyColor", ArrayToJson(component->skyColor) },
                        { "colorIntensity", component->colorIntensity },
                        { "environmentMap", component->environmentMap },
                        { "irradianceMap", component->irradianceMap },
                        { "prefilteredReflectionMap", component->prefilteredReflectionMap },
                        { "brdfLut", component->brdfLut },
                        { "rotation", component->rotation },
                        { "diffuseIntensity", component->diffuseIntensity },
                        { "reflectionIntensity", component->reflectionIntensity },
                        { "exposureEV", component->exposureEV },
                        { "skyboxExposureEV", component->skyboxExposureEV },
                        { "sunIntensityMultiplier", component->sunIntensityMultiplier },
                        { "sunSpecularMultiplier", component->sunSpecularMultiplier },
                        { "autoExposureEnabled", component->autoExposureEnabled },
                        { "autoExposureMinEV", component->autoExposureMinEV },
                        { "autoExposureMaxEV", component->autoExposureMaxEV },
                        { "autoExposureSpeedUp", component->autoExposureSpeedUp },
                        { "autoExposureSpeedDown", component->autoExposureSpeedDown },
                        { "ambientOcclusionStrength", component->ambientOcclusionStrength },
                        { "affectAmbientOcclusion", component->affectAmbientOcclusion },
                        { "lowerHemisphereIsBlack", component->lowerHemisphereIsBlack },
                        { "lowerHemisphereColor", ArrayToJson(component->lowerHemisphereColor) },
                        { "blendFactor", component->blendFactor },
                        { "priority", component->priority },
                        { "realTimeCapture", component->realTimeCapture },
                        { "captureUpdateInterval", component->captureUpdateInterval },
                        { "volumetricScatteringIntensity", component->volumetricScatteringIntensity },
                        { "affectFog", component->affectFog },
                        { "updateMode", EnumToInt(component->updateMode) },
                        { "rebuildIBLRequested", component->rebuildIBLRequested }
                    };
                }

                if (const auto* component = registry.try_get<PostProcessComponent>(entity))
                {
                    entityJson["postProcess"] = {
                        { "active", component->active },
                        { "priority", component->priority },
                        { "unbound", component->unbound },
                        { "volumeExtents", ArrayToJson(component->volumeExtents) },
                        { "blendDistance", component->blendDistance },
                        { "toneMappingEnabled", component->toneMappingEnabled },
                        { "toneMappingOperator", static_cast<int>(component->toneMappingOperator) },
                        { "exposureCompensationEV", component->exposureCompensationEV },
                        { "eyeAdaptationCompensationEV", component->eyeAdaptationCompensationEV },
                        { "whitePoint", component->whitePoint },
                        { "colorFilter", ArrayToJson(component->colorFilter) },
                        { "colorBalance", ArrayToJson(component->colorBalance) },
                        { "saturation", component->saturation },
                        { "contrast", component->contrast },
                        { "gamma", component->gamma },
                        { "filmCurveShoulder", component->filmCurveShoulder },
                        { "filmCurveLinear", component->filmCurveLinear },
                        { "filmCurveToe", component->filmCurveToe },
                        { "bloomEnabled", component->bloomEnabled },
                        { "bloomIntensity", component->bloomIntensity },
                        { "bloomThreshold", component->bloomThreshold },
                        { "bloomKnee", component->bloomKnee }
                    };
                }

                if (const auto* component = registry.try_get<DestructibleComponent>(entity))
                {
                    entityJson["destructible"] = {
                        { "active", component->active },
                        { "blastAsset", component->blastAsset },
                        { "intactMeshOverride", component->intactMeshOverride },
                        { "visibleIntactMesh", component->visibleIntactMesh },
                        { "fractureOnImpact", component->fractureOnImpact },
                        { "accumulateDamage", component->accumulateDamage },
                        { "worldSupport", component->worldSupport },
                        { "stressDamage", component->stressDamage },
                        { "activationMode", EnumToInt(component->activationMode) },
                        { "chunkSize", EnumToInt(component->chunkSize) },
                        { "desiredChunkCount", component->desiredChunkCount },
                        { "damageThreshold", component->damageThreshold },
                        { "impactDamageScale", component->impactDamageScale },
                        { "damageSpread", component->damageSpread },
                        { "chunkMassScale", component->chunkMassScale },
                        { "maxChunkSpeed", component->maxChunkSpeed },
                        { "debrisLifetime", component->debrisLifetime },
                        { "supportDepth", component->supportDepth }
                    };
                }

                if (const auto* component = registry.try_get<RigidBodyComponent>(entity))
                {
                    entityJson["rigidBody"] = {
                        { "active", component->active },
                        { "bodyType", EnumToInt(component->bodyType) },
                        { "enableGravity", component->enableGravity },
                        { "startAwake", component->startAwake },
                        { "enableCCD", component->enableCCD },
                        { "mass", component->mass },
                        { "linearDamping", component->linearDamping },
                        { "angularDamping", component->angularDamping },
                        { "maxLinearVelocity", component->maxLinearVelocity },
                        { "maxAngularVelocity", component->maxAngularVelocity },
                        { "lockLinearAxes", ArrayToJson(component->lockLinearAxes) },
                        { "lockAngularAxes", ArrayToJson(component->lockAngularAxes) },
                        { "linearVelocity", ArrayToJson(component->linearVelocity) },
                        { "angularVelocity", ArrayToJson(component->angularVelocity) },
                        { "sleeping", component->sleeping }
                    };
                }

                if (const auto* component = registry.try_get<ColliderComponent>(entity))
                {
                    entityJson["collider"] = {
                        { "active", component->active },
                        { "isTrigger", component->isTrigger },
                        { "shape", EnumToInt(component->shape) },
                        { "center", ArrayToJson(component->center) },
                        { "boxHalfExtents", ArrayToJson(component->boxHalfExtents) },
                        { "sphereRadius", component->sphereRadius },
                        { "capsuleRadius", component->capsuleRadius },
                        { "capsuleHalfHeight", component->capsuleHalfHeight },
                        { "meshConvex", component->meshConvex },
                        { "meshSource", component->meshSource },
                        { "material", {
                            { "staticFriction", component->material.staticFriction },
                            { "dynamicFriction", component->material.dynamicFriction },
                            { "restitution", component->material.restitution }
                        } }
                    };
                }

                if (const auto* component = registry.try_get<JointComponent>(entity))
                {
                    entityJson["joint"] = {
                        { "active", component->active },
                        { "connectedBodyA", component->connectedBodyA },
                        { "connectedBodyB", component->connectedBodyB },
                        { "collideConnectedBodies", component->collideConnectedBodies },
                        { "enableBreak", component->enableBreak },
                        { "breakForce", component->breakForce },
                        { "breakTorque", component->breakTorque },
                        { "projectionMode", EnumToInt(component->projectionMode) },
                        { "solverPositionIterations", component->solverPositionIterations },
                        { "solverVelocityIterations", component->solverVelocityIterations },
                        { "projectionLinearTolerance", component->projectionLinearTolerance },
                        { "projectionAngularToleranceDegrees", component->projectionAngularToleranceDegrees }
                    };
                }

                if (const auto* component = registry.try_get<FixedJointComponent>(entity))
                {
                    entityJson["fixedJoint"] = {
                        { "maintainInitialOffset", component->maintainInitialOffset }
                    };
                }

                if (const auto* component = registry.try_get<HingeJointComponent>(entity))
                {
                    entityJson["hingeJoint"] = {
                        { "axis", ArrayToJson(component->axis) },
                        { "enableLimits", component->enableLimits },
                        { "lowerLimitDegrees", component->lowerLimitDegrees },
                        { "upperLimitDegrees", component->upperLimitDegrees },
                        { "enableMotor", component->enableMotor },
                        { "motorVelocityDegreesPerSecond", component->motorVelocityDegreesPerSecond },
                        { "motorMaxForce", component->motorMaxForce }
                    };
                }

                if (const auto* component = registry.try_get<SliderJointComponent>(entity))
                {
                    entityJson["sliderJoint"] = {
                        { "axis", ArrayToJson(component->axis) },
                        { "enableLimits", component->enableLimits },
                        { "lowerLimit", component->lowerLimit },
                        { "upperLimit", component->upperLimit },
                        { "enableMotor", component->enableMotor },
                        { "motorSpeed", component->motorSpeed },
                        { "motorMaxForce", component->motorMaxForce }
                    };
                }

                if (const auto* component = registry.try_get<D6JointComponent>(entity))
                {
                    entityJson["d6Joint"] = {
                        { "linearMotion", ArrayToJson(component->linearMotion) },
                        { "angularMotion", ArrayToJson(component->angularMotion) },
                        { "linearLimit", component->linearLimit },
                        { "twistLowerLimitDegrees", component->twistLowerLimitDegrees },
                        { "twistUpperLimitDegrees", component->twistUpperLimitDegrees },
                        { "swingYLimitDegrees", component->swingYLimitDegrees },
                        { "swingZLimitDegrees", component->swingZLimitDegrees },
                        { "enableLinearDrive", component->enableLinearDrive },
                        { "linearDrivePositionTarget", ArrayToJson(component->linearDrivePositionTarget) },
                        { "linearDriveVelocityTarget", ArrayToJson(component->linearDriveVelocityTarget) },
                        { "linearDriveStiffness", component->linearDriveStiffness },
                        { "linearDriveDamping", component->linearDriveDamping },
                        { "linearDriveForceLimit", component->linearDriveForceLimit },
                        { "enableAngularDrive", component->enableAngularDrive },
                        { "angularDrivePositionTarget", ArrayToJson(component->angularDrivePositionTarget) },
                        { "angularDriveVelocityTarget", ArrayToJson(component->angularDriveVelocityTarget) },
                        { "angularDriveStiffness", component->angularDriveStiffness },
                        { "angularDriveDamping", component->angularDriveDamping },
                        { "angularDriveForceLimit", component->angularDriveForceLimit }
                    };
                }

                if (const auto* component = registry.try_get<CharacterControllerComponent>(entity))
                {
                    entityJson["characterController"] = {
                        { "active", component->active },
                        { "movementMode", EnumToInt(component->movementMode) },
                        { "radius", component->radius },
                        { "height", component->height },
                        { "stepOffset", component->stepOffset },
                        { "slopeLimitDegrees", component->slopeLimitDegrees },
                        { "skinWidth", component->skinWidth },
                        { "minMoveDistance", component->minMoveDistance },
                        { "gravityScale", component->gravityScale },
                        { "collisionLayer", component->collisionLayer },
                        { "collisionMask", component->collisionMask },
                        { "isGrounded", component->isGrounded }
                    };
                }

                if (const auto* component = registry.try_get<WheelColliderComponent>(entity))
                {
                    entityJson["wheelCollider"] = {
                        { "active", component->active },
                        { "radius", component->radius },
                        { "width", component->width },
                        { "wheelMass", component->wheelMass },
                        { "suspensionRestLength", component->suspensionRestLength },
                        { "suspensionMaxCompression", component->suspensionMaxCompression },
                        { "suspensionMaxDroop", component->suspensionMaxDroop },
                        { "suspensionStiffness", component->suspensionStiffness },
                        { "suspensionDamping", component->suspensionDamping },
                        { "suspensionTravel", component->suspensionTravel },
                        { "tireFriction", component->tireFriction },
                        { "tireFrictionScale", component->tireFrictionScale },
                        { "steerable", component->steerable },
                        { "driven", component->driven },
                        { "handbrakeAffected", component->handbrakeAffected },
                        { "axleType", EnumToInt(component->axleType) },
                        { "visualWheelEntity", component->visualWheelEntity },
                        { "suspensionAttachPoint", ArrayToJson(component->suspensionAttachPoint) },
                        { "wheelRotationAxis", ArrayToJson(component->wheelRotationAxis) },
                        { "suspensionAxis", ArrayToJson(component->suspensionAxis) }
                    };
                }

                if (const auto* component = registry.try_get<VehicleComponent>(entity))
                {
                    entityJson["vehicle"] = {
                        { "active", component->active },
                        { "simulationEnabled", component->simulationEnabled },
                        { "vehicleType", EnumToInt(component->vehicleType) },
                        { "inputSource", EnumToInt(component->inputSource) },
                        { "useCenterOfMassOverride", component->useCenterOfMassOverride },
                        { "centerOfMassOffset", ArrayToJson(component->centerOfMassOffset) },
                        { "chassisRigidBody", component->chassisRigidBody },
                        { "wheelEntities", component->wheelEntities },
                        { "dragCoefficient", component->dragCoefficient },
                        { "rollingResistance", component->rollingResistance },
                        { "aeroDownforce", component->aeroDownforce },
                        { "engineTorque", component->engineTorque },
                        { "idleRPM", component->idleRPM },
                        { "maxRPM", component->maxRPM },
                        { "reverseGearRatio", component->reverseGearRatio },
                        { "gearRatios", component->gearRatios },
                        { "differentialRatio", component->differentialRatio },
                        { "brakeForce", component->brakeForce },
                        { "handbrakeForce", component->handbrakeForce },
                        { "frontBrakeBias", component->frontBrakeBias },
                        { "frontDriveBias", component->frontDriveBias },
                        { "tireFrictionScale", component->tireFrictionScale },
                        { "suspensionStiffness", component->suspensionStiffness },
                        { "suspensionDamping", component->suspensionDamping },
                        { "suspensionTravel", component->suspensionTravel },
                        { "maxSteerAngleDegrees", component->maxSteerAngleDegrees },
                        { "steerSensitivity", component->steerSensitivity },
                        { "shiftUpRPM", component->shiftUpRPM },
                        { "shiftDownRPM", component->shiftDownRPM },
                        { "automaticTransmission", component->automaticTransmission },
                        { "enableABS", component->enableABS },
                        { "enableTCS", component->enableTCS },
                        { "ackermannSteering", component->ackermannSteering },
                        { "autoFlip", component->autoFlip },
                        { "useSubstepping", component->useSubstepping },
                        { "sleepWhenInactive", component->sleepWhenInactive },
                        { "inputMap", component->inputMap },
                        { "tuningAsset", component->tuningAsset }
                    };
                }

                if (const auto* component = registry.try_get<VehicleInputComponent>(entity))
                {
                    entityJson["vehicleInput"] = {
                        { "active", component->active },
                        { "throttle", component->throttle },
                        { "brake", component->brake },
                        { "steering", component->steering },
                        { "handbrake", component->handbrake },
                        { "clutch", component->clutch },
                        { "gearUpRequested", component->gearUpRequested },
                        { "gearDownRequested", component->gearDownRequested },
                        { "resetRequested", component->resetRequested }
                    };
                }

                if (const auto* component = registry.try_get<ForceFieldComponent>(entity))
                {
                    entityJson["forceField"] = {
                        { "active", component->active },
                        { "shape", EnumToInt(component->shape) },
                        { "type", EnumToInt(component->type) },
                        { "center", ArrayToJson(component->center) },
                        { "boxHalfExtents", ArrayToJson(component->boxHalfExtents) },
                        { "sphereRadius", component->sphereRadius },
                        { "capsuleRadius", component->capsuleRadius },
                        { "capsuleHalfHeight", component->capsuleHalfHeight },
                        { "direction", ArrayToJson(component->direction) },
                        { "strength", component->strength },
                        { "falloff", component->falloff },
                        { "affectDynamicBodiesOnly", component->affectDynamicBodiesOnly },
                        { "affectCharacters", component->affectCharacters }
                    };
                }

                if (const auto* component = registry.try_get<BuoyancyComponent>(entity))
                {
                    json floatPoints = json::array();
                    for (const auto& point : component->floatPoints)
                    {
                        floatPoints.push_back(ArrayToJson(point));
                    }

                    entityJson["buoyancy"] = {
                        { "active", component->active },
                        { "waterLevel", component->waterLevel },
                        { "waterVolumeEntity", component->waterVolumeEntity },
                        { "density", component->density },
                        { "drag", component->drag },
                        { "angularDrag", component->angularDrag },
                        { "floatPoints", std::move(floatPoints) }
                    };
                }

                if (const auto* component = registry.try_get<PhysicsEventsComponent>(entity))
                {
                    entityJson["physicsEvents"] = {
                        { "onCollisionEnter", component->onCollisionEnter },
                        { "onCollisionStay", component->onCollisionStay },
                        { "onCollisionExit", component->onCollisionExit },
                        { "onTriggerEnter", component->onTriggerEnter },
                        { "onTriggerStay", component->onTriggerStay },
                        { "onTriggerExit", component->onTriggerExit },
                        { "contactImpulseThreshold", component->contactImpulseThreshold }
                    };
                }

                if (const auto* component = registry.try_get<RagdollComponent>(entity))
                {
                    entityJson["ragdoll"] = {
                        { "active", component->active },
                        { "skeletalMeshAsset", component->skeletalMeshAsset },
                        { "physicsAsset", component->physicsAsset },
                        { "animationPhysicsBlend", component->animationPhysicsBlend },
                        { "startSimulated", component->startSimulated }
                    };
                }

                root["entities"].push_back(std::move(entityJson));
            });

            std::ofstream output(scenePath, std::ios::binary | std::ios::trunc);
            if (!output)
            {
                outError = "Failed to open scene file for writing: " + scenePath.string();
                return false;
            }

            output << root.dump(2);
            if (!output.good())
            {
                outError = "Failed to write scene file: " + scenePath.string();
                return false;
            }

            return true;
        }
        catch (const std::exception& e)
        {
            outError = std::string("Scene serialization failed: ") + e.what();
            return false;
        }
    }

    bool SceneSerializer::Deserialize(const std::filesystem::path& scenePath, Scene& scene, std::string& outError)
    {
        outError.clear();
        try
        {
            std::ifstream input(scenePath, std::ios::binary);
            if (!input)
            {
                outError = "Failed to open scene file: " + scenePath.string();
                return false;
            }

            json root = json::parse(input, nullptr, true, true);
            if (!root.is_object())
            {
                outError = "Scene root must be a JSON object.";
                return false;
            }

            const auto entitiesIt = root.find("entities");
            if (entitiesIt == root.end() || !entitiesIt->is_array())
            {
                outError = "Scene must contain an 'entities' array.";
                return false;
            }

            struct EntityRecord
            {
                UUID uuid = 0;
                std::string name = "Entity";
                std::string tag = "Untagged";
                std::string layer = "Default";
                UUID parentUuid = 0;
                const json* data = nullptr;
            };

            std::vector<EntityRecord> records;
            records.reserve(entitiesIt->size());
            std::unordered_set<UUID> seenUuids;

            for (const json& entityJson : *entitiesIt)
            {
                if (!entityJson.is_object())
                {
                    outError = "Scene entity entry must be a JSON object.";
                    return false;
                }

                const UUID uuid = entityJson.at("uuid").get<UUID>();
                if (uuid == 0)
                {
                    outError = "Scene entity UUID cannot be zero.";
                    return false;
                }
                if (!seenUuids.insert(uuid).second)
                {
                    outError = "Scene contains duplicate entity UUIDs.";
                    return false;
                }

                EntityRecord record;
                record.uuid = uuid;
                record.name = entityJson.value("name", entityJson.value("tag", std::string("Entity")));
                record.tag = entityJson.contains("name")
                    ? entityJson.value("tag", std::string("Untagged"))
                    : std::string("Untagged");
                record.layer = entityJson.value("layer", std::string("Default"));
                record.parentUuid = entityJson.value("parent", static_cast<UUID>(0));
                record.data = &entityJson;
                records.push_back(std::move(record));
            }

            for (const EntityRecord& record : records)
            {
                if (record.parentUuid != 0 && !seenUuids.contains(record.parentUuid))
                {
                    outError = "Scene references a missing parent UUID.";
                    return false;
                }
            }

            scene.Clear();
            auto& registry = scene.GetRegistry();
            std::unordered_map<UUID, EntityID> entityByUuid;
            entityByUuid.reserve(records.size());

            for (const EntityRecord& record : records)
            {
                Entity entity = scene.CreateEntity(record.name);
                scene.SetEntityUUID(entity.GetHandle(), record.uuid);
                entityByUuid.emplace(record.uuid, entity.GetHandle());
            }

            for (const EntityRecord& record : records)
            {
                const auto foundEntity = entityByUuid.find(record.uuid);
                if (foundEntity == entityByUuid.end())
                {
                    outError = "Failed to reconstruct entity UUID map.";
                    return false;
                }

                Entity entity(foundEntity->second, &registry);
                const json& entityJson = *record.data;

                auto& tag = entity.GetComponent<TagComponent>();
                tag.name = record.name;
                tag.tag = record.tag;
                tag.layer = record.layer;

                if (const auto transformIt = entityJson.find("transform");
                    transformIt != entityJson.end() && transformIt->is_object())
                {
                    auto& transform = entity.GetComponent<TransformComponent>();
                    if (!ReadArrayField(*transformIt, "position", transform.position, outError) ||
                        !ReadArrayField(*transformIt, "rotation", transform.rotation, outError) ||
                        !ReadArrayField(*transformIt, "scale", transform.scale, outError))
                    {
                        return false;
                    }
                    transform.dirty = true;
                }

                if (const auto componentIt = entityJson.find("meshRenderer");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<MeshRendererComponent>();
                    component.visible = componentIt->value("visible", component.visible);
                    component.usePrimitive = componentIt->value("usePrimitive", component.usePrimitive);
                    component.primitive = IntToEnum<PrimitiveType>(componentIt->value("primitive", EnumToInt(component.primitive)));
                    component.meshSource = componentIt->value("meshSource", component.meshSource);
                    component.importedSceneSource = componentIt->value("importedSceneSource", component.importedSceneSource);
                    component.meshPartIndex = componentIt->value("meshPartIndex", component.meshPartIndex);
                    component.meshLod = componentIt->value("meshLod", component.meshLod);
                    component.autoStreamLod = componentIt->value("autoStreamLod", component.autoStreamLod);
                    component.maxAutoLod = componentIt->value("maxAutoLod", component.maxAutoLod);
                    component.lodNearDistance = componentIt->value("lodNearDistance", component.lodNearDistance);
                    component.lodFarDistance = componentIt->value("lodFarDistance", component.lodFarDistance);
                    component.streamSectionsByDistance = componentIt->value("streamSectionsByDistance", component.streamSectionsByDistance);
                    component.sectionLoadDistance = componentIt->value("sectionLoadDistance", component.sectionLoadDistance);
                    component.staticLighting = componentIt->value("staticLighting", component.staticLighting);
                    if (!ReadArrayField(*componentIt, "color", component.color, outError))
                    {
                        return false;
                    }
                }

                if (const auto componentIt = entityJson.find("material");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<MaterialComponent>();
                    component.name = componentIt->value("name", component.name);
                    component.shader = componentIt->value("shader", component.shader);
                    component.sharedMaterial = componentIt->value("sharedMaterial", component.sharedMaterial);
                    component.renderingMode =
                        IntToEnum<MaterialRenderingMode>(componentIt->value("renderingMode", EnumToInt(component.renderingMode)));
                    component.albedoTexture = componentIt->value("albedoTexture", component.albedoTexture);
                    component.metallicTexture = componentIt->value("metallicTexture", component.metallicTexture);
                    component.metallic = componentIt->value("metallic", component.metallic);
                    component.smoothness = componentIt->value("smoothness", component.smoothness);
                    component.smoothnessSource =
                        IntToEnum<MaterialSmoothnessSource>(
                            componentIt->value("smoothnessSource", EnumToInt(component.smoothnessSource)));
                    component.enableHighlights = componentIt->value("enableHighlights", component.enableHighlights);
                    component.enableReflections = componentIt->value("enableReflections", component.enableReflections);
                    component.normalTexture = componentIt->value("normalTexture", component.normalTexture);
                    component.normalScale = componentIt->value("normalScale", component.normalScale);
                    component.heightTexture = componentIt->value("heightTexture", component.heightTexture);
                    component.heightScale = componentIt->value("heightScale", component.heightScale);
                    component.occlusionTexture = componentIt->value("occlusionTexture", component.occlusionTexture);
                    component.occlusionStrength = componentIt->value("occlusionStrength", component.occlusionStrength);
                    component.emissionEnabled = componentIt->value("emissionEnabled", component.emissionEnabled);
                    component.emissionTexture = componentIt->value("emissionTexture", component.emissionTexture);
                    component.emissionIntensity = componentIt->value("emissionIntensity", component.emissionIntensity);
                    component.globalIllumination =
                        IntToEnum<MaterialGlobalIlluminationMode>(
                            componentIt->value("globalIllumination", EnumToInt(component.globalIllumination)));
                    component.detailMaskTexture = componentIt->value("detailMaskTexture", component.detailMaskTexture);
                    component.detailAlbedoTexture = componentIt->value("detailAlbedoTexture", component.detailAlbedoTexture);
                    component.detailNormalTexture = componentIt->value("detailNormalTexture", component.detailNormalTexture);
                    component.detailNormalScale = componentIt->value("detailNormalScale", component.detailNormalScale);
                    component.uvSet = componentIt->value("uvSet", component.uvSet);
                    if (!ReadArrayField(*componentIt, "albedoColor", component.albedoColor, outError) ||
                        !ReadArrayField(*componentIt, "emissionColor", component.emissionColor, outError) ||
                        !ReadArrayField(*componentIt, "tiling", component.tiling, outError) ||
                        !ReadArrayField(*componentIt, "offset", component.offset, outError) ||
                        !ReadArrayField(*componentIt, "detailTiling", component.detailTiling, outError) ||
                        !ReadArrayField(*componentIt, "detailOffset", component.detailOffset, outError))
                    {
                        return false;
                    }
                }
                else if (const auto meshRendererIt = entityJson.find("meshRenderer");
                         meshRendererIt != entityJson.end() && meshRendererIt->is_object())
                {
                    std::vector<std::string> legacyOverrides;
                    if (const auto materialOverridesIt = meshRendererIt->find("materialOverrides");
                        materialOverridesIt != meshRendererIt->end() && materialOverridesIt->is_array())
                    {
                        legacyOverrides.reserve(materialOverridesIt->size());
                        for (const auto& entry : *materialOverridesIt)
                        {
                            if (entry.is_string())
                            {
                                legacyOverrides.push_back(entry.get<std::string>());
                            }
                        }
                    }
                    else
                    {
                        const std::string legacyMaterialAsset = meshRendererIt->value("materialAsset", std::string {});
                        if (!legacyMaterialAsset.empty())
                        {
                            legacyOverrides.push_back(legacyMaterialAsset);
                        }
                    }

                    if (!legacyOverrides.empty())
                    {
                        if (entity.HasComponent<MeshRendererComponent>())
                        {
                            entity.GetComponent<MeshRendererComponent>().materialOverrides = std::move(legacyOverrides);
                        }
                    }
                }

                if (const auto componentIt = entityJson.find("camera");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<CameraComponent>();
                    component.primary = componentIt->value("primary", component.primary);
                    component.active = componentIt->value("active", component.active);
                    component.projection = IntToEnum<CameraProjectionMode>(componentIt->value("projection", EnumToInt(component.projection)));
                    component.fovDegrees = componentIt->value("fovDegrees", component.fovDegrees);
                    component.orthographicSize = componentIt->value("orthographicSize", component.orthographicSize);
                    component.nearClip = componentIt->value("nearClip", component.nearClip);
                    component.farClip = componentIt->value("farClip", component.farClip);
                    component.sensorWidth = componentIt->value("sensorWidth", component.sensorWidth);
                    component.sensorHeight = componentIt->value("sensorHeight", component.sensorHeight);
                    component.focalLength = componentIt->value("focalLength", component.focalLength);
                    component.useViewportAspectRatio = componentIt->value("useViewportAspectRatio", component.useViewportAspectRatio);
                    component.aspectRatio = componentIt->value("aspectRatio", component.aspectRatio);
                    component.constrainAspectRatio = componentIt->value("constrainAspectRatio", component.constrainAspectRatio);
                    component.clearMode = IntToEnum<CameraClearMode>(componentIt->value("clearMode", EnumToInt(component.clearMode)));
                    component.cullingMask = componentIt->value("cullingMask", component.cullingMask);
                    component.hdr = componentIt->value("hdr", component.hdr);
                    component.allowPostProcess = componentIt->value("allowPostProcess", component.allowPostProcess);
                    component.allowMSAA = componentIt->value("allowMSAA", component.allowMSAA);
                    component.allowMotionBlur = componentIt->value("allowMotionBlur", component.allowMotionBlur);
                    component.exposure = componentIt->value("exposure", component.exposure);
                    component.renderPriority = componentIt->value(
                        "renderPriority",
                        componentIt->value("priority", component.renderPriority));
                    if (componentIt->contains("clearColor") &&
                        !ReadArrayField(*componentIt, "clearColor", component.clearColor, outError))
                    {
                        return false;
                    }
                }

                if (const auto componentIt = entityJson.find("luaScript");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<LuaScriptComponent>();
                    component.enabled = componentIt->value("enabled", component.enabled);
                    component.scriptAsset = componentIt->value("scriptAsset", component.scriptAsset);
                    component.propertyOverrides.clear();
                    if (const auto overridesIt = componentIt->find("propertyOverrides");
                        overridesIt != componentIt->end() && overridesIt->is_object())
                    {
                        for (auto overrideIt = overridesIt->begin(); overrideIt != overridesIt->end(); ++overrideIt)
                        {
                            ScriptValue value {};
                            if (!ReadScriptValueFromJson(overrideIt.value(), value, outError))
                            {
                                LUMA_LOG_WARN(
                                    "Scene",
                                    "Skipping invalid Lua script property override '" + overrideIt.key() + "': " + outError);
                                outError.clear();
                                continue;
                            }

                            component.propertyOverrides.emplace(overrideIt.key(), std::move(value));
                        }
                    }
                }

                if (const auto componentIt = entityJson.find("audioSource");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<AudioSourceComponent>();
                    component.clipAsset = componentIt->value("clipAsset", component.clipAsset);
                    component.playOnAwake = componentIt->value("playOnAwake", component.playOnAwake);
                    component.looping = componentIt->value("looping", component.looping);
                    component.spatialized = componentIt->value("spatialized", component.spatialized);
                    component.mute = componentIt->value("mute", component.mute);
                    component.volume = componentIt->value("volume", component.volume);
                    component.pitch = componentIt->value("pitch", component.pitch);
                    component.minDistance = componentIt->value("minDistance", component.minDistance);
                    component.maxDistance = componentIt->value("maxDistance", component.maxDistance);
                    component.runtimeHandle = 0;
                }

                if (const auto componentIt = entityJson.find("audioListener");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<AudioListenerComponent>();
                    component.enabled = componentIt->value("enabled", component.enabled);
                    component.volume = componentIt->value("volume", component.volume);
                }

                if (const auto componentIt = entityJson.find("prefabInstance");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<PrefabInstanceComponent>();
                    component.prefabAsset = componentIt->value("prefabAsset", component.prefabAsset);
                    component.sourceEntityId = componentIt->value("sourceEntityId", component.sourceEntityId);
                    component.isRoot = componentIt->value("isRoot", component.isRoot);
                }

                if (const auto componentIt = entityJson.find("directionalLight");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<DirectionalLightComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.intensity = componentIt->value("intensity", component.intensity);
                    component.castShadows = componentIt->value("castShadows", component.castShadows);
                    if (!ReadArrayField(*componentIt, "color", component.color, outError))
                    {
                        return false;
                    }
                }

                if (const auto componentIt = entityJson.find("pointLight");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<PointLightComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.mode =
                        static_cast<PointLightMode>(componentIt->value("mode", static_cast<std::uint32_t>(component.mode)));
                    component.temperature = componentIt->value("temperature", component.temperature);
                    component.intensity = componentIt->value("intensity", component.intensity);
                    component.indirectMultiplier =
                        componentIt->value("indirectMultiplier", component.indirectMultiplier);
                    component.range = componentIt->value("range", component.range);
                    component.attenuation = componentIt->value("attenuation", component.attenuation);
                    component.castShadows = componentIt->value("castShadows", component.castShadows);
                    component.shadowType = static_cast<PointLightShadowType>(
                        componentIt->value("shadowType", static_cast<std::uint32_t>(component.shadowType)));
                    component.shadowBias = componentIt->value("shadowBias", component.shadowBias);
                    component.shadowResolution = componentIt->value("shadowResolution", component.shadowResolution);
                    component.bakedShadowRadius =
                        componentIt->value("bakedShadowRadius", component.bakedShadowRadius);
                    component.drawHalo = componentIt->value("drawHalo", component.drawHalo);
                    component.renderMode = static_cast<PointLightRenderMode>(
                        componentIt->value("renderMode", static_cast<std::uint32_t>(component.renderMode)));
                    component.cullingMask = componentIt->value("cullingMask", component.cullingMask);
                    if (!ReadArrayField(*componentIt, "color", component.color, outError))
                    {
                        return false;
                    }
                }

                if (const auto componentIt = entityJson.find("spotLight");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<SpotLightComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.intensity = componentIt->value("intensity", component.intensity);
                    component.range = componentIt->value("range", component.range);
                    component.innerConeAngle = componentIt->value("innerConeAngle", component.innerConeAngle);
                    component.outerConeAngle = componentIt->value("outerConeAngle", component.outerConeAngle);
                    component.castShadows = componentIt->value("castShadows", component.castShadows);
                    component.volumetricScatteringIntensity =
                        componentIt->value("volumetricScatteringIntensity", component.volumetricScatteringIntensity);
                    if (!ReadArrayField(*componentIt, "color", component.color, outError))
                    {
                        return false;
                    }
                }

                if (const auto componentIt = entityJson.find("skyLight");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<SkyLightComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.intensity = componentIt->value("intensity", component.intensity);
                    component.skyLightType = IntToEnum<SceneSkyLightType>(componentIt->value("skyLightType", EnumToInt(component.skyLightType)));
                    component.castShadows = componentIt->value("castShadows", component.castShadows);
                    component.colorIntensity = componentIt->value("colorIntensity", component.colorIntensity);
                    component.environmentMap = componentIt->value("environmentMap", component.environmentMap);
                    component.irradianceMap = componentIt->value("irradianceMap", component.irradianceMap);
                    component.prefilteredReflectionMap = componentIt->value("prefilteredReflectionMap", component.prefilteredReflectionMap);
                    component.brdfLut = componentIt->value("brdfLut", component.brdfLut);
                    component.rotation = componentIt->value("rotation", component.rotation);
                    component.diffuseIntensity = componentIt->value("diffuseIntensity", component.diffuseIntensity);
                    component.reflectionIntensity = componentIt->value("reflectionIntensity", component.reflectionIntensity);
                    component.exposureEV = componentIt->value("exposureEV", component.exposureEV);
                    component.skyboxExposureEV = componentIt->value("skyboxExposureEV", component.skyboxExposureEV);
                    component.sunIntensityMultiplier = componentIt->value("sunIntensityMultiplier", component.sunIntensityMultiplier);
                    component.sunSpecularMultiplier = componentIt->value("sunSpecularMultiplier", component.sunSpecularMultiplier);
                    component.autoExposureEnabled = componentIt->value("autoExposureEnabled", component.autoExposureEnabled);
                    component.autoExposureMinEV = componentIt->value("autoExposureMinEV", component.autoExposureMinEV);
                    component.autoExposureMaxEV = componentIt->value("autoExposureMaxEV", component.autoExposureMaxEV);
                    component.autoExposureSpeedUp = componentIt->value("autoExposureSpeedUp", component.autoExposureSpeedUp);
                    component.autoExposureSpeedDown = componentIt->value("autoExposureSpeedDown", component.autoExposureSpeedDown);
                    component.ambientOcclusionStrength = componentIt->value("ambientOcclusionStrength", component.ambientOcclusionStrength);
                    component.affectAmbientOcclusion = componentIt->value("affectAmbientOcclusion", component.affectAmbientOcclusion);
                    component.lowerHemisphereIsBlack = componentIt->value("lowerHemisphereIsBlack", component.lowerHemisphereIsBlack);
                    component.blendFactor = componentIt->value("blendFactor", component.blendFactor);
                    component.priority = componentIt->value("priority", component.priority);
                    component.realTimeCapture = componentIt->value("realTimeCapture", component.realTimeCapture);
                    component.captureUpdateInterval = componentIt->value("captureUpdateInterval", component.captureUpdateInterval);
                    component.volumetricScatteringIntensity = componentIt->value("volumetricScatteringIntensity", component.volumetricScatteringIntensity);
                    component.affectFog = componentIt->value("affectFog", component.affectFog);
                    component.updateMode = IntToEnum<SceneSkyUpdateMode>(componentIt->value("updateMode", EnumToInt(component.updateMode)));
                    component.rebuildIBLRequested = componentIt->value("rebuildIBLRequested", component.rebuildIBLRequested);
                    if (!ReadArrayField(*componentIt, "color", component.color, outError) ||
                        !ReadArrayField(*componentIt, "skyColor", component.skyColor, outError) ||
                        !ReadArrayField(*componentIt, "lowerHemisphereColor", component.lowerHemisphereColor, outError))
                    {
                        return false;
                    }
                }

                if (const auto componentIt = entityJson.find("postProcess");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<PostProcessComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.priority = componentIt->value("priority", component.priority);
                    component.unbound = componentIt->value("unbound", component.unbound);
                    component.blendDistance = componentIt->value("blendDistance", component.blendDistance);
                    component.toneMappingEnabled = componentIt->value("toneMappingEnabled", component.toneMappingEnabled);
                    component.toneMappingOperator =
                        static_cast<ToneMappingOperator>(componentIt->value(
                            "toneMappingOperator",
                            static_cast<int>(component.toneMappingOperator)));
                    component.exposureCompensationEV =
                        componentIt->value("exposureCompensationEV", component.exposureCompensationEV);
                    component.eyeAdaptationCompensationEV =
                        componentIt->value("eyeAdaptationCompensationEV", component.eyeAdaptationCompensationEV);
                    component.whitePoint = componentIt->value("whitePoint", component.whitePoint);
                    component.saturation = componentIt->value("saturation", component.saturation);
                    component.contrast = componentIt->value("contrast", component.contrast);
                    component.gamma = componentIt->value("gamma", component.gamma);
                    component.filmCurveShoulder = componentIt->value("filmCurveShoulder", component.filmCurveShoulder);
                    component.filmCurveLinear = componentIt->value("filmCurveLinear", component.filmCurveLinear);
                    component.filmCurveToe = componentIt->value("filmCurveToe", component.filmCurveToe);
                    component.bloomEnabled = componentIt->value("bloomEnabled", component.bloomEnabled);
                    component.bloomIntensity = componentIt->value("bloomIntensity", component.bloomIntensity);
                    component.bloomThreshold = componentIt->value("bloomThreshold", component.bloomThreshold);
                    component.bloomKnee = componentIt->value("bloomKnee", component.bloomKnee);
                    if (!ReadArrayField(*componentIt, "volumeExtents", component.volumeExtents, outError) ||
                        !ReadArrayField(*componentIt, "colorFilter", component.colorFilter, outError) ||
                        !ReadArrayField(*componentIt, "colorBalance", component.colorBalance, outError))
                    {
                        return false;
                    }
                }

                if (const auto componentIt = entityJson.find("destructible");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<DestructibleComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.blastAsset = componentIt->value("blastAsset", component.blastAsset);
                    component.intactMeshOverride = componentIt->value("intactMeshOverride", component.intactMeshOverride);
                    component.visibleIntactMesh = componentIt->value("visibleIntactMesh", component.visibleIntactMesh);
                    component.fractureOnImpact = componentIt->value("fractureOnImpact", component.fractureOnImpact);
                    component.accumulateDamage = componentIt->value("accumulateDamage", component.accumulateDamage);
                    component.worldSupport = componentIt->value("worldSupport", component.worldSupport);
                    component.stressDamage = componentIt->value("stressDamage", component.stressDamage);
                    component.activationMode = IntToEnum<DestructionActivationMode>(
                        componentIt->value("activationMode", EnumToInt(component.activationMode)));
                    component.chunkSize = IntToEnum<DestructionChunkSize>(
                        componentIt->value("chunkSize", EnumToInt(component.chunkSize)));
                    component.desiredChunkCount = std::max(0, componentIt->value("desiredChunkCount", component.desiredChunkCount));
                    component.damageThreshold = componentIt->value("damageThreshold", component.damageThreshold);
                    component.impactDamageScale = componentIt->value("impactDamageScale", component.impactDamageScale);
                    component.damageSpread = componentIt->value("damageSpread", component.damageSpread);
                    component.chunkMassScale = componentIt->value("chunkMassScale", component.chunkMassScale);
                    component.maxChunkSpeed = componentIt->value("maxChunkSpeed", component.maxChunkSpeed);
                    component.debrisLifetime = componentIt->value("debrisLifetime", component.debrisLifetime);
                    component.supportDepth = componentIt->value("supportDepth", component.supportDepth);
                }

                if (const auto componentIt = entityJson.find("rigidBody");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<RigidBodyComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.bodyType = IntToEnum<RigidBodyType>(componentIt->value("bodyType", EnumToInt(component.bodyType)));
                    component.enableGravity = componentIt->value("enableGravity", component.enableGravity);
                    component.startAwake = componentIt->value("startAwake", component.startAwake);
                    component.enableCCD = componentIt->value("enableCCD", component.enableCCD);
                    component.mass = componentIt->value("mass", component.mass);
                    component.linearDamping = componentIt->value("linearDamping", component.linearDamping);
                    component.angularDamping = componentIt->value("angularDamping", component.angularDamping);
                    component.maxLinearVelocity = componentIt->value("maxLinearVelocity", component.maxLinearVelocity);
                    component.maxAngularVelocity = componentIt->value("maxAngularVelocity", component.maxAngularVelocity);
                    component.sleeping = componentIt->value("sleeping", component.sleeping);
                    if (!ReadArrayField(*componentIt, "lockLinearAxes", component.lockLinearAxes, outError) ||
                        !ReadArrayField(*componentIt, "lockAngularAxes", component.lockAngularAxes, outError) ||
                        !ReadArrayField(*componentIt, "linearVelocity", component.linearVelocity, outError) ||
                        !ReadArrayField(*componentIt, "angularVelocity", component.angularVelocity, outError))
                    {
                        return false;
                    }
                }

                if (const auto componentIt = entityJson.find("collider");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<ColliderComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.isTrigger = componentIt->value("isTrigger", component.isTrigger);
                    component.shape = IntToEnum<ColliderShapeType>(componentIt->value("shape", EnumToInt(component.shape)));
                    component.sphereRadius = componentIt->value("sphereRadius", component.sphereRadius);
                    component.capsuleRadius = componentIt->value("capsuleRadius", component.capsuleRadius);
                    component.capsuleHalfHeight = componentIt->value("capsuleHalfHeight", component.capsuleHalfHeight);
                    component.meshConvex = componentIt->value("meshConvex", component.meshConvex);
                    component.meshSource = componentIt->value("meshSource", component.meshSource);
                    if (!ReadArrayField(*componentIt, "center", component.center, outError) ||
                        !ReadArrayField(*componentIt, "boxHalfExtents", component.boxHalfExtents, outError))
                    {
                        return false;
                    }

                    if (const auto materialIt = componentIt->find("material");
                        materialIt != componentIt->end() && materialIt->is_object())
                    {
                        component.material.staticFriction = materialIt->value("staticFriction", component.material.staticFriction);
                        component.material.dynamicFriction = materialIt->value("dynamicFriction", component.material.dynamicFriction);
                        component.material.restitution = materialIt->value("restitution", component.material.restitution);
                    }
                }

                if (const auto componentIt = entityJson.find("joint");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<JointComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.connectedBodyA = componentIt->value("connectedBodyA", component.connectedBodyA);
                    component.connectedBodyB = componentIt->value("connectedBodyB", component.connectedBodyB);
                    component.collideConnectedBodies = componentIt->value("collideConnectedBodies", component.collideConnectedBodies);
                    component.enableBreak = componentIt->value("enableBreak", component.enableBreak);
                    component.breakForce = componentIt->value("breakForce", component.breakForce);
                    component.breakTorque = componentIt->value("breakTorque", component.breakTorque);
                    component.projectionMode = IntToEnum<JointProjectionMode>(componentIt->value("projectionMode", EnumToInt(component.projectionMode)));
                    component.solverPositionIterations = componentIt->value("solverPositionIterations", component.solverPositionIterations);
                    component.solverVelocityIterations = componentIt->value("solverVelocityIterations", component.solverVelocityIterations);
                    component.projectionLinearTolerance = componentIt->value("projectionLinearTolerance", component.projectionLinearTolerance);
                    component.projectionAngularToleranceDegrees =
                        componentIt->value("projectionAngularToleranceDegrees", component.projectionAngularToleranceDegrees);
                }

                if (const auto componentIt = entityJson.find("fixedJoint");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<FixedJointComponent>();
                    component.maintainInitialOffset = componentIt->value("maintainInitialOffset", component.maintainInitialOffset);
                }

                if (const auto componentIt = entityJson.find("hingeJoint");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<HingeJointComponent>();
                    component.enableLimits = componentIt->value("enableLimits", component.enableLimits);
                    component.lowerLimitDegrees = componentIt->value("lowerLimitDegrees", component.lowerLimitDegrees);
                    component.upperLimitDegrees = componentIt->value("upperLimitDegrees", component.upperLimitDegrees);
                    component.enableMotor = componentIt->value("enableMotor", component.enableMotor);
                    component.motorVelocityDegreesPerSecond =
                        componentIt->value("motorVelocityDegreesPerSecond", component.motorVelocityDegreesPerSecond);
                    component.motorMaxForce = componentIt->value("motorMaxForce", component.motorMaxForce);
                    if (!ReadArrayField(*componentIt, "axis", component.axis, outError))
                    {
                        return false;
                    }
                }

                if (const auto componentIt = entityJson.find("sliderJoint");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<SliderJointComponent>();
                    component.enableLimits = componentIt->value("enableLimits", component.enableLimits);
                    component.lowerLimit = componentIt->value("lowerLimit", component.lowerLimit);
                    component.upperLimit = componentIt->value("upperLimit", component.upperLimit);
                    component.enableMotor = componentIt->value("enableMotor", component.enableMotor);
                    component.motorSpeed = componentIt->value("motorSpeed", component.motorSpeed);
                    component.motorMaxForce = componentIt->value("motorMaxForce", component.motorMaxForce);
                    if (!ReadArrayField(*componentIt, "axis", component.axis, outError))
                    {
                        return false;
                    }
                }

                if (const auto componentIt = entityJson.find("d6Joint");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<D6JointComponent>();
                    component.linearLimit = componentIt->value("linearLimit", component.linearLimit);
                    component.twistLowerLimitDegrees = componentIt->value("twistLowerLimitDegrees", component.twistLowerLimitDegrees);
                    component.twistUpperLimitDegrees = componentIt->value("twistUpperLimitDegrees", component.twistUpperLimitDegrees);
                    component.swingYLimitDegrees = componentIt->value("swingYLimitDegrees", component.swingYLimitDegrees);
                    component.swingZLimitDegrees = componentIt->value("swingZLimitDegrees", component.swingZLimitDegrees);
                    component.enableLinearDrive = componentIt->value("enableLinearDrive", component.enableLinearDrive);
                    component.linearDriveStiffness = componentIt->value("linearDriveStiffness", component.linearDriveStiffness);
                    component.linearDriveDamping = componentIt->value("linearDriveDamping", component.linearDriveDamping);
                    component.linearDriveForceLimit = componentIt->value("linearDriveForceLimit", component.linearDriveForceLimit);
                    component.enableAngularDrive = componentIt->value("enableAngularDrive", component.enableAngularDrive);
                    component.angularDriveStiffness = componentIt->value("angularDriveStiffness", component.angularDriveStiffness);
                    component.angularDriveDamping = componentIt->value("angularDriveDamping", component.angularDriveDamping);
                    component.angularDriveForceLimit = componentIt->value("angularDriveForceLimit", component.angularDriveForceLimit);
                    if (!ReadArrayField(*componentIt, "linearMotion", component.linearMotion, outError) ||
                        !ReadArrayField(*componentIt, "angularMotion", component.angularMotion, outError) ||
                        !ReadArrayField(*componentIt, "linearDrivePositionTarget", component.linearDrivePositionTarget, outError) ||
                        !ReadArrayField(*componentIt, "linearDriveVelocityTarget", component.linearDriveVelocityTarget, outError) ||
                        !ReadArrayField(*componentIt, "angularDrivePositionTarget", component.angularDrivePositionTarget, outError) ||
                        !ReadArrayField(*componentIt, "angularDriveVelocityTarget", component.angularDriveVelocityTarget, outError))
                    {
                        return false;
                    }
                }

                if (const auto componentIt = entityJson.find("characterController");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<CharacterControllerComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.movementMode = IntToEnum<CharacterMovementMode>(componentIt->value("movementMode", EnumToInt(component.movementMode)));
                    component.radius = componentIt->value("radius", component.radius);
                    component.height = componentIt->value("height", component.height);
                    component.stepOffset = componentIt->value("stepOffset", component.stepOffset);
                    component.slopeLimitDegrees = componentIt->value("slopeLimitDegrees", component.slopeLimitDegrees);
                    component.skinWidth = componentIt->value("skinWidth", component.skinWidth);
                    component.minMoveDistance = componentIt->value("minMoveDistance", component.minMoveDistance);
                    component.gravityScale = componentIt->value("gravityScale", component.gravityScale);
                    component.collisionLayer = componentIt->value("collisionLayer", component.collisionLayer);
                    component.collisionMask = componentIt->value("collisionMask", component.collisionMask);
                    component.isGrounded = componentIt->value("isGrounded", component.isGrounded);
                }

                if (const auto componentIt = entityJson.find("wheelCollider");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<WheelColliderComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.radius = componentIt->value("radius", component.radius);
                    component.width = componentIt->value("width", component.width);
                    component.wheelMass = componentIt->value("wheelMass", component.wheelMass);
                    component.suspensionRestLength = componentIt->value("suspensionRestLength", component.suspensionRestLength);
                    component.suspensionMaxCompression = componentIt->value("suspensionMaxCompression", component.suspensionMaxCompression);
                    component.suspensionMaxDroop = componentIt->value("suspensionMaxDroop", component.suspensionMaxDroop);
                    component.suspensionStiffness = componentIt->value("suspensionStiffness", component.suspensionStiffness);
                    component.suspensionDamping = componentIt->value("suspensionDamping", component.suspensionDamping);
                    component.suspensionTravel = componentIt->value("suspensionTravel", component.suspensionTravel);
                    component.tireFriction = componentIt->value("tireFriction", component.tireFriction);
                    component.tireFrictionScale = componentIt->value("tireFrictionScale", component.tireFrictionScale);
                    component.steerable = componentIt->value("steerable", component.steerable);
                    component.driven = componentIt->value("driven", component.driven);
                    component.handbrakeAffected = componentIt->value("handbrakeAffected", component.handbrakeAffected);
                    component.axleType = IntToEnum<VehicleAxleType>(componentIt->value("axleType", EnumToInt(component.axleType)));
                    component.visualWheelEntity = componentIt->value("visualWheelEntity", component.visualWheelEntity);
                    if ((componentIt->contains("suspensionAttachPoint") &&
                            !ReadArrayField(*componentIt, "suspensionAttachPoint", component.suspensionAttachPoint, outError)) ||
                        (componentIt->contains("wheelRotationAxis") &&
                            !ReadArrayField(*componentIt, "wheelRotationAxis", component.wheelRotationAxis, outError)) ||
                        (componentIt->contains("suspensionAxis") &&
                            !ReadArrayField(*componentIt, "suspensionAxis", component.suspensionAxis, outError)))
                    {
                        return false;
                    }
                }

                if (const auto componentIt = entityJson.find("vehicle");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<VehicleComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.simulationEnabled = componentIt->value("simulationEnabled", component.simulationEnabled);
                    component.vehicleType = IntToEnum<VehicleType>(componentIt->value("vehicleType", EnumToInt(component.vehicleType)));
                    component.inputSource = IntToEnum<VehicleInputSource>(componentIt->value("inputSource", EnumToInt(component.inputSource)));
                    component.useCenterOfMassOverride = componentIt->value("useCenterOfMassOverride", component.useCenterOfMassOverride);
                    component.chassisRigidBody = componentIt->value("chassisRigidBody", component.chassisRigidBody);
                    component.wheelEntities = componentIt->value("wheelEntities", component.wheelEntities);
                    component.dragCoefficient = componentIt->value("dragCoefficient", component.dragCoefficient);
                    component.rollingResistance = componentIt->value("rollingResistance", component.rollingResistance);
                    component.aeroDownforce = componentIt->value("aeroDownforce", component.aeroDownforce);
                    component.engineTorque = componentIt->value("engineTorque", component.engineTorque);
                    component.idleRPM = componentIt->value("idleRPM", component.idleRPM);
                    component.maxRPM = componentIt->value("maxRPM", component.maxRPM);
                    component.reverseGearRatio = componentIt->value("reverseGearRatio", component.reverseGearRatio);
                    component.gearRatios = componentIt->value("gearRatios", component.gearRatios);
                    if (component.gearRatios.empty())
                    {
                        component.gearRatios.push_back(componentIt->value("gearRatio", 3.5f));
                    }
                    component.differentialRatio = componentIt->value("differentialRatio", component.differentialRatio);
                    component.brakeForce = componentIt->value("brakeForce", component.brakeForce);
                    component.handbrakeForce = componentIt->value("handbrakeForce", component.handbrakeForce);
                    component.frontBrakeBias = componentIt->value("frontBrakeBias", component.frontBrakeBias);
                    component.frontDriveBias = componentIt->value("frontDriveBias", component.frontDriveBias);
                    component.tireFrictionScale = componentIt->value("tireFrictionScale", component.tireFrictionScale);
                    component.suspensionStiffness = componentIt->value("suspensionStiffness", component.suspensionStiffness);
                    component.suspensionDamping = componentIt->value("suspensionDamping", component.suspensionDamping);
                    component.suspensionTravel = componentIt->value("suspensionTravel", component.suspensionTravel);
                    component.maxSteerAngleDegrees = componentIt->value("maxSteerAngleDegrees", component.maxSteerAngleDegrees);
                    component.steerSensitivity = componentIt->value("steerSensitivity", component.steerSensitivity);
                    component.shiftUpRPM = componentIt->value("shiftUpRPM", component.shiftUpRPM);
                    component.shiftDownRPM = componentIt->value("shiftDownRPM", component.shiftDownRPM);
                    component.automaticTransmission = componentIt->value("automaticTransmission", component.automaticTransmission);
                    component.enableABS = componentIt->value("enableABS", component.enableABS);
                    component.enableTCS = componentIt->value("enableTCS", component.enableTCS);
                    component.ackermannSteering = componentIt->value("ackermannSteering", component.ackermannSteering);
                    component.autoFlip = componentIt->value("autoFlip", component.autoFlip);
                    component.useSubstepping = componentIt->value("useSubstepping", component.useSubstepping);
                    component.sleepWhenInactive = componentIt->value("sleepWhenInactive", component.sleepWhenInactive);
                    component.inputMap = componentIt->value("inputMap", component.inputMap);
                    component.tuningAsset = componentIt->value("tuningAsset", component.tuningAsset);
                    if (componentIt->contains("centerOfMassOffset") &&
                        !ReadArrayField(*componentIt, "centerOfMassOffset", component.centerOfMassOffset, outError))
                    {
                        return false;
                    }
                }

                if (const auto componentIt = entityJson.find("vehicleInput");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<VehicleInputComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.throttle = componentIt->value("throttle", component.throttle);
                    component.brake = componentIt->value("brake", component.brake);
                    component.steering = componentIt->value("steering", component.steering);
                    component.handbrake = componentIt->value("handbrake", component.handbrake);
                    component.clutch = componentIt->value("clutch", component.clutch);
                    component.gearUpRequested = componentIt->value("gearUpRequested", component.gearUpRequested);
                    component.gearDownRequested = componentIt->value("gearDownRequested", component.gearDownRequested);
                    component.resetRequested = componentIt->value("resetRequested", component.resetRequested);
                }

                if (const auto componentIt = entityJson.find("forceField");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<ForceFieldComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.shape = IntToEnum<ForceFieldShape>(componentIt->value("shape", EnumToInt(component.shape)));
                    component.type = IntToEnum<ForceFieldType>(componentIt->value("type", EnumToInt(component.type)));
                    component.sphereRadius = componentIt->value("sphereRadius", component.sphereRadius);
                    component.capsuleRadius = componentIt->value("capsuleRadius", component.capsuleRadius);
                    component.capsuleHalfHeight = componentIt->value("capsuleHalfHeight", component.capsuleHalfHeight);
                    component.strength = componentIt->value("strength", component.strength);
                    component.falloff = componentIt->value("falloff", component.falloff);
                    component.affectDynamicBodiesOnly = componentIt->value("affectDynamicBodiesOnly", component.affectDynamicBodiesOnly);
                    component.affectCharacters = componentIt->value("affectCharacters", component.affectCharacters);
                    if (!ReadArrayField(*componentIt, "center", component.center, outError) ||
                        !ReadArrayField(*componentIt, "boxHalfExtents", component.boxHalfExtents, outError) ||
                        !ReadArrayField(*componentIt, "direction", component.direction, outError))
                    {
                        return false;
                    }
                }

                if (const auto componentIt = entityJson.find("buoyancy");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<BuoyancyComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.waterLevel = componentIt->value("waterLevel", component.waterLevel);
                    component.waterVolumeEntity = componentIt->value("waterVolumeEntity", component.waterVolumeEntity);
                    component.density = componentIt->value("density", component.density);
                    component.drag = componentIt->value("drag", component.drag);
                    component.angularDrag = componentIt->value("angularDrag", component.angularDrag);

                    if (const auto floatPointsIt = componentIt->find("floatPoints");
                        floatPointsIt != componentIt->end())
                    {
                        if (!floatPointsIt->is_array() || floatPointsIt->size() != component.floatPoints.size())
                        {
                            outError = "Scene field 'floatPoints' must contain four float points.";
                            return false;
                        }

                        for (std::size_t i = 0; i < component.floatPoints.size(); ++i)
                        {
                            if (!(*floatPointsIt)[i].is_array() || (*floatPointsIt)[i].size() != component.floatPoints[i].size())
                            {
                                outError = "Scene field 'floatPoints' contains an invalid entry.";
                                return false;
                            }

                            for (std::size_t axis = 0; axis < component.floatPoints[i].size(); ++axis)
                            {
                                component.floatPoints[i][axis] = (*floatPointsIt)[i][axis].get<float>();
                            }
                        }
                    }
                }

                if (const auto componentIt = entityJson.find("physicsEvents");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<PhysicsEventsComponent>();
                    component.onCollisionEnter = componentIt->value("onCollisionEnter", component.onCollisionEnter);
                    component.onCollisionStay = componentIt->value("onCollisionStay", component.onCollisionStay);
                    component.onCollisionExit = componentIt->value("onCollisionExit", component.onCollisionExit);
                    component.onTriggerEnter = componentIt->value("onTriggerEnter", component.onTriggerEnter);
                    component.onTriggerStay = componentIt->value("onTriggerStay", component.onTriggerStay);
                    component.onTriggerExit = componentIt->value("onTriggerExit", component.onTriggerExit);
                    component.contactImpulseThreshold = componentIt->value("contactImpulseThreshold", component.contactImpulseThreshold);
                }

                if (const auto componentIt = entityJson.find("ragdoll");
                    componentIt != entityJson.end() && componentIt->is_object())
                {
                    auto& component = entity.AddOrReplaceComponent<RagdollComponent>();
                    component.active = componentIt->value("active", component.active);
                    component.skeletalMeshAsset = componentIt->value("skeletalMeshAsset", component.skeletalMeshAsset);
                    component.physicsAsset = componentIt->value("physicsAsset", component.physicsAsset);
                    component.animationPhysicsBlend = componentIt->value("animationPhysicsBlend", component.animationPhysicsBlend);
                    component.startSimulated = componentIt->value("startSimulated", component.startSimulated);
                }
            }

            for (const EntityRecord& record : records)
            {
                if (record.parentUuid == 0)
                {
                    continue;
                }

                const auto foundChild = entityByUuid.find(record.uuid);
                const auto foundParent = entityByUuid.find(record.parentUuid);
                if (foundChild == entityByUuid.end() || foundParent == entityByUuid.end())
                {
                    outError = "Scene parent-child relationship could not be resolved.";
                    return false;
                }

                scene.SetParent(foundChild->second, foundParent->second);
            }

            scene.UpdateWorldTransforms();
            return true;
        }
        catch (const std::exception& e)
        {
            outError = std::string("Scene deserialization failed: ") + e.what();
            return false;
        }
    }
}
