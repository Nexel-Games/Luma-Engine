#include "Luma/Layers/TriangleLayer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>
#include <nlohmann/json.hpp>
#include "Luma/Asset/Core/MeshAssetIO.h"
#include "Luma/Asset/Import/BuiltInImporters.h"
#include "Luma/Asset/Streaming/FileResourceStreamProvider.h"
#include "Luma/Audio/Core/AudioSystem.h"
#include "Luma/Core/App/Project.h"
#include "Luma/Core/App/RenderPipeline.h"
#include "Luma/Core/Foundation/Logging.h"
#include "Luma/Editor/Core/EditorTaskManager.h"
#include "Luma/Editor/Plugins/BuiltInPluginRegistry.h"
#include "Luma/Editor/Rendering/SceneViewBuilder.h"
#include "Luma/Editor/UI/TooltipAPI.h"
#include "Luma/Input/Input.h"
#include "Luma/Renderer/PrimitiveMeshFactory.h"
#include "Luma/RHI/IRenderBackend.h"
#include "Luma/Scene/CameraComponent.h"
#include "Luma/Scene/AudioListenerComponent.h"
#include "Luma/Scene/AudioSourceComponent.h"
#include "Luma/Scene/CharacterControllerComponent.h"
#include "Luma/Scene/ColliderComponent.h"
#include "Luma/Scene/D6JointComponent.h"
#include "Luma/Scene/DirectionalLightComponent.h"
#include "Luma/Scene/PointLightComponent.h"
#include "Luma/Scene/PostProcessComponent.h"
#include "Luma/Scene/FixedJointComponent.h"
#include "Luma/Scene/ForceFieldComponent.h"
#include "Luma/Scene/HingeJointComponent.h"
#include "Luma/Scene/IDComponent.h"
#include "Luma/Scene/JointComponent.h"
#include "Luma/Scene/MaterialComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/BuoyancyComponent.h"
#include "Luma/Scene/PrefabInstanceComponent.h"
#include "Luma/Scene/PrefabSerializer.h"
#include "Luma/Scene/PhysicsEventsComponent.h"
#include "Luma/Scene/RagdollComponent.h"
#include "Luma/Scene/RelationshipComponent.h"
#include "Luma/Scene/RigidBodyComponent.h"
#include "Luma/Scene/SliderJointComponent.h"
#include "Luma/Scene/SkyLightComponent.h"
#include "Luma/Scene/SpotLightComponent.h"
#include "Luma/Scene/TagComponent.h"
#include "Luma/Scene/TransformComponent.h"
#include "Luma/Scene/SceneSerializer.h"
#include "Luma/Scene/VehicleComponent.h"
#include "Luma/Scene/WheelColliderComponent.h"

namespace Luma
{
    namespace
    {
        enum class ContentItemType
        {
            Folder = 0,
            Scene,
            Script,
            Image,
            Mesh,
            Material,
            Shader,
            Audio,
            Procedural,
            Package,
            Other
        };

        std::string ToLowerString(std::string value)
        {
            std::transform(
                value.begin(),
                value.end(),
                value.begin(),
                [](const unsigned char c)
                {
                    return static_cast<char>(std::tolower(c));
                });
            return value;
        }

        std::string TrimCopy(std::string value)
        {
            auto notWhitespace = [](const unsigned char c)
            {
                return !std::isspace(c);
            };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notWhitespace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notWhitespace).base(), value.end());
            return value;
        }

        std::filesystem::path NormalizePathForComparison(const std::filesystem::path& path)
        {
            if (path.empty())
            {
                return {};
            }

            std::error_code ec;
            std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
            if (ec)
            {
                normalized = path.lexically_normal();
            }
            return normalized.lexically_normal();
        }

        bool PathIsWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root)
        {
            if (path.empty() || root.empty())
            {
                return false;
            }

            std::string normalizedPath = NormalizePathForComparison(path).generic_string();
            std::string normalizedRoot = NormalizePathForComparison(root).generic_string();
#if defined(_WIN32)
            normalizedPath = ToLowerString(normalizedPath);
            normalizedRoot = ToLowerString(normalizedRoot);
#endif
            if (normalizedPath.size() < normalizedRoot.size())
            {
                return false;
            }
            if (normalizedPath.compare(0, normalizedRoot.size(), normalizedRoot) != 0)
            {
                return false;
            }
            if (normalizedPath.size() == normalizedRoot.size())
            {
                return true;
            }

            const char separator = normalizedPath[normalizedRoot.size()];
            return separator == '/' || separator == '\\';
        }

        std::string SanitizeFileStem(std::string value)
        {
            value = TrimCopy(std::move(value));
            if (value.empty())
            {
                return "NewPrefab";
            }

            for (char& character : value)
            {
                const unsigned char code = static_cast<unsigned char>(character);
                if (code < 32 ||
                    character == '<' ||
                    character == '>' ||
                    character == ':' ||
                    character == '"' ||
                    character == '/' ||
                    character == '\\' ||
                    character == '|' ||
                    character == '?' ||
                    character == '*')
                {
                    character = '_';
                }
            }

            while (!value.empty() && (value.back() == ' ' || value.back() == '.'))
            {
                value.pop_back();
            }

            return value.empty() ? "NewPrefab" : value;
        }

        constexpr float kDegreesToRadians = 3.14159265359f / 180.0f;

        std::array<float, 3> RotateByEulerDegrees(const std::array<float, 3>& point, const std::array<float, 3>& degrees)
        {
            const float rx = degrees[0] * kDegreesToRadians;
            const float ry = degrees[1] * kDegreesToRadians;
            const float rz = degrees[2] * kDegreesToRadians;

            const float cosX = std::cos(rx);
            const float sinX = std::sin(rx);
            const float cosY = std::cos(ry);
            const float sinY = std::sin(ry);
            const float cosZ = std::cos(rz);
            const float sinZ = std::sin(rz);

            std::array<float, 3> rotated = point;

            rotated = {
                rotated[0],
                rotated[1] * cosX - rotated[2] * sinX,
                rotated[1] * sinX + rotated[2] * cosX
            };

            rotated = {
                rotated[0] * cosY + rotated[2] * sinY,
                rotated[1],
                -rotated[0] * sinY + rotated[2] * cosY
            };

            rotated = {
                rotated[0] * cosZ - rotated[1] * sinZ,
                rotated[0] * sinZ + rotated[1] * cosZ,
                rotated[2]
            };

            return rotated;
        }

        std::array<float, 3> NormalizeOrFallback(
            const std::array<float, 3>& value,
            const std::array<float, 3>& fallback)
        {
            const float lengthSquared = value[0] * value[0] + value[1] * value[1] + value[2] * value[2];
            if (lengthSquared <= 1.0e-6f)
            {
                return fallback;
            }

            const float invLength = 1.0f / std::sqrt(lengthSquared);
            return { value[0] * invLength, value[1] * invLength, value[2] * invLength };
        }

        const nlohmann::json* FindEntityJsonByUuid(const nlohmann::json& root, const UUID uuid)
        {
            const auto entitiesIt = root.find("entities");
            if (entitiesIt == root.end() || !entitiesIt->is_array())
            {
                return nullptr;
            }

            for (const auto& entityJson : *entitiesIt)
            {
                if (entityJson.is_object() && entityJson.value("uuid", static_cast<UUID>(0)) == uuid)
                {
                    return &entityJson;
                }
            }

            return nullptr;
        }

        void CollectJsonDifferencePaths(
            const nlohmann::json& source,
            const nlohmann::json& current,
            const std::string& pathPrefix,
            std::vector<std::string>& outPaths)
        {
            if (source.type() != current.type())
            {
                outPaths.push_back(pathPrefix);
                return;
            }

            if (source.is_object())
            {
                std::vector<std::string> keys;
                keys.reserve(source.size() + current.size());
                for (auto it = source.begin(); it != source.end(); ++it)
                {
                    keys.push_back(it.key());
                }
                for (auto it = current.begin(); it != current.end(); ++it)
                {
                    if (std::find(keys.begin(), keys.end(), it.key()) == keys.end())
                    {
                        keys.push_back(it.key());
                    }
                }

                std::sort(keys.begin(), keys.end());
                for (const std::string& key : keys)
                {
                    if (key == "uuid" || key == "parent" || key == "prefabInstance")
                    {
                        continue;
                    }

                    const auto sourceIt = source.find(key);
                    const auto currentIt = current.find(key);
                    const std::string childPath = pathPrefix.empty() ? key : (pathPrefix + "." + key);
                    if (sourceIt == source.end() || currentIt == current.end())
                    {
                        outPaths.push_back((sourceIt == source.end() ? "+" : "-") + childPath);
                        continue;
                    }

                    CollectJsonDifferencePaths(*sourceIt, *currentIt, childPath, outPaths);
                }
                return;
            }

            if (source.is_array())
            {
                if (source.size() != current.size())
                {
                    outPaths.push_back(pathPrefix);
                    return;
                }

                for (std::size_t index = 0; index < source.size(); ++index)
                {
                    const std::string childPath = pathPrefix + "[" + std::to_string(index) + "]";
                    CollectJsonDifferencePaths(source[index], current[index], childPath, outPaths);
                }
                return;
            }

            if (source != current)
            {
                outPaths.push_back(pathPrefix);
            }
        }

        std::string StripPrefabOverridePrefix(const std::string_view path)
        {
            if (!path.empty() && (path.front() == '+' || path.front() == '-'))
            {
                return std::string(path.substr(1));
            }
            return std::string(path);
        }

        std::string EscapeJsonPointerToken(const std::string_view token)
        {
            std::string escaped;
            escaped.reserve(token.size());
            for (const char character : token)
            {
                if (character == '~')
                {
                    escaped += "~0";
                }
                else if (character == '/')
                {
                    escaped += "~1";
                }
                else
                {
                    escaped += character;
                }
            }
            return escaped;
        }

        std::vector<std::string> ParseOverridePathTokens(const std::string_view path)
        {
            std::vector<std::string> tokens;
            std::string current;
            for (std::size_t index = 0; index < path.size(); ++index)
            {
                const char character = path[index];
                if (character == '.')
                {
                    if (!current.empty())
                    {
                        tokens.push_back(std::move(current));
                        current.clear();
                    }
                    continue;
                }

                if (character == '[')
                {
                    if (!current.empty())
                    {
                        tokens.push_back(std::move(current));
                        current.clear();
                    }

                    ++index;
                    std::string indexToken;
                    while (index < path.size() && path[index] != ']')
                    {
                        indexToken += path[index];
                        ++index;
                    }
                    if (!indexToken.empty())
                    {
                        tokens.push_back(std::move(indexToken));
                    }
                    continue;
                }

                current += character;
            }

            if (!current.empty())
            {
                tokens.push_back(std::move(current));
            }

            return tokens;
        }

        nlohmann::json::json_pointer BuildJsonPointer(const std::vector<std::string>& tokens)
        {
            std::string pointerValue;
            for (const std::string& token : tokens)
            {
                pointerValue += "/";
                pointerValue += EscapeJsonPointerToken(token);
            }
            return nlohmann::json::json_pointer(pointerValue);
        }

        bool JsonPointerExists(const nlohmann::json& object, const nlohmann::json::json_pointer& pointer)
        {
            try
            {
                object.at(pointer);
                return true;
            }
            catch (const std::exception&)
            {
                return false;
            }
        }

        bool EraseJsonPath(nlohmann::json& object, const std::vector<std::string>& tokens)
        {
            if (tokens.empty())
            {
                return false;
            }
            if (tokens.size() == 1)
            {
                if (!object.is_object())
                {
                    return false;
                }
                return object.erase(tokens.front()) > 0;
            }

            std::vector<std::string> parentTokens(tokens.begin(), tokens.end() - 1);
            const nlohmann::json::json_pointer parentPointer = BuildJsonPointer(parentTokens);
            if (!JsonPointerExists(object, parentPointer))
            {
                return false;
            }

            nlohmann::json& parent = object[parentPointer];
            const std::string& lastToken = tokens.back();
            if (parent.is_object())
            {
                return parent.erase(lastToken) > 0;
            }
            if (parent.is_array())
            {
                const int index = std::atoi(lastToken.c_str());
                if (index < 0 || static_cast<std::size_t>(index) >= parent.size())
                {
                    return false;
                }
                parent.erase(parent.begin() + index);
                return true;
            }
            return false;
        }

        std::string TruncateMiddle(const std::string_view value, const std::size_t maxLength)
        {
            if (value.size() <= maxLength)
            {
                return std::string(value);
            }
            if (maxLength <= 3)
            {
                return std::string(value.substr(0, maxLength));
            }

            const std::size_t prefixLength = (maxLength - 3) / 2;
            const std::size_t suffixLength = maxLength - 3 - prefixLength;
            return std::string(value.substr(0, prefixLength)) + "..." +
                std::string(value.substr(value.size() - suffixLength));
        }

        bool IsPopupBlockingViewportRect(const ImVec2& rectMin, const ImVec2& rectMax)
        {
            if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
            {
                return false;
            }

            ImGuiContext* context = GImGui;
            if (context == nullptr || context->HoveredWindow == nullptr)
            {
                return false;
            }

            ImGuiWindow* hoveredWindow = context->HoveredWindow;
            const bool hoveredWindowIsPopup =
                (hoveredWindow->Flags & ImGuiWindowFlags_Popup) != 0 ||
                (hoveredWindow->Flags & ImGuiWindowFlags_Modal) != 0;
            if (!hoveredWindowIsPopup)
            {
                return false;
            }

            const ImRect viewportRect(rectMin, rectMax);
            const ImRect popupRect = hoveredWindow->Rect();
            return viewportRect.Overlaps(popupRect) && popupRect.Contains(context->IO.MousePos);
        }

        bool IsMeshAssetPathCandidate(const std::filesystem::path& path)
        {
            const std::string extension = ToLowerString(path.extension().string());
            return extension == ".lumamesh" ||
                extension == ".obj" ||
                extension == ".fbx" ||
                extension == ".gltf" ||
                extension == ".glb";
        }

        constexpr std::string_view kDefaultGridMaterialAsset = "default_materials/GridMaterial/GridMaterial.lumamat";
        constexpr std::string_view kDefaultGenericTextureAsset = "default_materials/tool_textures/generic.png";

        void InitializeDefaultMaterialComponent(MaterialComponent& material)
        {
            material.name = "New Material";
            material.shader = "Standard";
            material.sharedMaterial = std::string(kDefaultGridMaterialAsset);
            material.albedoTexture = std::string(kDefaultGenericTextureAsset);
        }

        std::string SanitizeAssetStem(std::string value)
        {
            value = TrimCopy(std::move(value));
            if (value.empty())
            {
                return "New Material";
            }

            for (char& character : value)
            {
                const unsigned char c = static_cast<unsigned char>(character);
                if (std::isalnum(c) || c == '_' || c == '-' || c == ' ')
                {
                    continue;
                }
                character = '_';
            }

            return value;
        }

        bool HasEnabledAudioListener(entt::registry& registry)
        {
            const auto view = registry.view<AudioListenerComponent>();
            for (const EntityID entity : view)
            {
                if (view.get<AudioListenerComponent>(entity).enabled)
                {
                    return true;
                }
            }
            return false;
        }

        nlohmann::json MaterialComponentToJson(const MaterialComponent& material)
        {
            using json = nlohmann::json;
            return json {
                { "name", material.name },
                { "shader", material.shader },
                { "sharedMaterial", material.sharedMaterial },
                { "renderingMode", static_cast<int>(material.renderingMode) },
                { "albedoColor", material.albedoColor },
                { "albedoTexture", material.albedoTexture },
                { "metallicTexture", material.metallicTexture },
                { "metallic", material.metallic },
                { "smoothness", material.smoothness },
                { "smoothnessSource", static_cast<int>(material.smoothnessSource) },
                { "enableHighlights", material.enableHighlights },
                { "enableReflections", material.enableReflections },
                { "normalTexture", material.normalTexture },
                { "normalScale", material.normalScale },
                { "heightTexture", material.heightTexture },
                { "heightScale", material.heightScale },
                { "occlusionTexture", material.occlusionTexture },
                { "occlusionStrength", material.occlusionStrength },
                { "emissionEnabled", material.emissionEnabled },
                { "emissionTexture", material.emissionTexture },
                { "emissionColor", material.emissionColor },
                { "emissionIntensity", material.emissionIntensity },
                { "globalIllumination", static_cast<int>(material.globalIllumination) },
                { "detailMaskTexture", material.detailMaskTexture },
                { "tiling", material.tiling },
                { "offset", material.offset },
                { "detailAlbedoTexture", material.detailAlbedoTexture },
                { "detailNormalTexture", material.detailNormalTexture },
                { "detailNormalScale", material.detailNormalScale },
                { "detailTiling", material.detailTiling },
                { "detailOffset", material.detailOffset },
                { "uvSet", material.uvSet }
            };
        }

        bool MaterialPropertiesEqual(const MaterialComponent& lhs, const MaterialComponent& rhs)
        {
            return lhs.name == rhs.name &&
                lhs.shader == rhs.shader &&
                lhs.renderingMode == rhs.renderingMode &&
                lhs.albedoColor == rhs.albedoColor &&
                lhs.albedoTexture == rhs.albedoTexture &&
                lhs.metallicTexture == rhs.metallicTexture &&
                lhs.metallic == rhs.metallic &&
                lhs.smoothness == rhs.smoothness &&
                lhs.smoothnessSource == rhs.smoothnessSource &&
                lhs.enableHighlights == rhs.enableHighlights &&
                lhs.enableReflections == rhs.enableReflections &&
                lhs.normalTexture == rhs.normalTexture &&
                lhs.normalScale == rhs.normalScale &&
                lhs.heightTexture == rhs.heightTexture &&
                lhs.heightScale == rhs.heightScale &&
                lhs.occlusionTexture == rhs.occlusionTexture &&
                lhs.occlusionStrength == rhs.occlusionStrength &&
                lhs.emissionEnabled == rhs.emissionEnabled &&
                lhs.emissionTexture == rhs.emissionTexture &&
                lhs.emissionColor == rhs.emissionColor &&
                lhs.emissionIntensity == rhs.emissionIntensity &&
                lhs.globalIllumination == rhs.globalIllumination &&
                lhs.detailMaskTexture == rhs.detailMaskTexture &&
                lhs.tiling == rhs.tiling &&
                lhs.offset == rhs.offset &&
                lhs.detailAlbedoTexture == rhs.detailAlbedoTexture &&
                lhs.detailNormalTexture == rhs.detailNormalTexture &&
                lhs.detailNormalScale == rhs.detailNormalScale &&
                lhs.detailTiling == rhs.detailTiling &&
                lhs.detailOffset == rhs.detailOffset &&
                lhs.uvSet == rhs.uvSet;
        }

        std::size_t ResolveMeshRendererMaterialSlotCount(
            const MeshRendererComponent& meshRenderer,
        const std::vector<Assets::MeshScenePart>* importedSceneParts)
        {
            if (importedSceneParts != nullptr && !importedSceneParts->empty())
            {
                return importedSceneParts->size();
            }

            if (meshRenderer.meshPartIndex != std::numeric_limits<std::uint32_t>::max())
            {
                return 1;
            }

            return std::max<std::size_t>(1, meshRenderer.materialOverrides.size());
        }

        std::size_t ResolveMeshRendererMaterialSlotIndex(
            const MeshRendererComponent& meshRenderer,
            const std::size_t importedPartIndex)
        {
            if (!meshRenderer.importedSceneSource.empty())
            {
                return importedPartIndex;
            }

            if (meshRenderer.meshPartIndex != std::numeric_limits<std::uint32_t>::max())
            {
                return 0;
            }

            return importedPartIndex;
        }

        const std::string* ResolveMeshRendererMaterialOverride(
            const MeshRendererComponent& meshRenderer,
            const std::size_t importedPartIndex)
        {
            const std::size_t slotIndex = ResolveMeshRendererMaterialSlotIndex(meshRenderer, importedPartIndex);
            if (slotIndex >= meshRenderer.materialOverrides.size())
            {
                return nullptr;
            }

            const std::string& overridePath = meshRenderer.materialOverrides[slotIndex];
            return overridePath.empty() ? nullptr : &overridePath;
        }

        MaterialRenderProxy BuildImportedMaterialRenderProxy(
            const Assets::MeshMaterialInfo& materialInfo,
            const std::filesystem::path& sourcePath)
        {
            MaterialRenderProxy proxy;
            proxy.sourcePath = sourcePath;
            proxy.name = materialInfo.name;
            proxy.shadingModel = 0u;
            proxy.baseColor = materialInfo.baseColorTint;
            proxy.subsurfaceColor = materialInfo.baseColorTint;
            proxy.emissiveColor = materialInfo.emissiveColor;
            proxy.emissiveIntensity = materialInfo.emissiveIntensity;
            proxy.metallic = materialInfo.metallic;
            proxy.roughness = materialInfo.roughness;
            proxy.specular = materialInfo.specular;
            proxy.ambientOcclusion = materialInfo.ambientOcclusion;
            proxy.normalStrength = materialInfo.normalStrength;
            proxy.opacity = materialInfo.opacity;
            proxy.albedoTexture = materialInfo.albedoTexture;
            proxy.normalTexture = materialInfo.normalTexture;
            proxy.heightTexture = materialInfo.heightTexture;
            proxy.ormTexture = materialInfo.ormTexture;
            proxy.metallicTexture = materialInfo.metallicTexture;
            proxy.roughnessTexture = materialInfo.roughnessTexture;
            proxy.ambientOcclusionTexture = materialInfo.ambientOcclusionTexture;
            proxy.emissiveTexture = materialInfo.emissiveTexture;
            proxy.opacityTexture = materialInfo.opacityTexture;
            proxy.featureFlags =
                (materialInfo.twoSided ? MaterialFeature_TwoSided : MaterialFeature_None) |
                MaterialFeature_CastShadows |
                MaterialFeature_ReceiveShadows |
                MaterialFeature_ReceiveDecals;
            return proxy;
        }

        bool LooksLikeHeightTexture(const std::filesystem::path& texturePath)
        {
            if (texturePath.empty())
            {
                return false;
            }

            const std::string stem = ToLowerString(texturePath.stem().string());
            return stem.find("bump") != std::string::npos ||
                stem.find("height") != std::string::npos ||
                stem.find("disp") != std::string::npos ||
                stem.find("displace") != std::string::npos ||
                stem.find("parallax") != std::string::npos;
        }

        std::uint32_t ResolveMaterialBlendMode(const MaterialRenderingMode renderingMode)
        {
            switch (renderingMode)
            {
            case MaterialRenderingMode::Cutout:
                return 1u;
            case MaterialRenderingMode::Fade:
            case MaterialRenderingMode::Transparent:
                return 2u;
            case MaterialRenderingMode::Opaque:
            default:
                return 0u;
            }
        }

        MaterialRenderProxy BuildMaterialRenderProxyFromComponent(
            const MaterialComponent& material,
            const std::filesystem::path& sourcePath)
        {
            MaterialRenderProxy proxy;
            proxy.sourcePath = sourcePath;
            proxy.name = material.name;
            proxy.blendMode = ResolveMaterialBlendMode(material.renderingMode);
            proxy.baseColor = material.albedoColor;
            proxy.emissiveColor = material.emissionColor;
            proxy.emissiveIntensity = material.emissionEnabled ? material.emissionIntensity : 0.0f;
            proxy.metallic = material.metallic;
            proxy.roughness = 1.0f - std::clamp(material.smoothness, 0.0f, 1.0f);
            proxy.specular = material.enableHighlights ? 0.5f : 0.0f;
            proxy.ambientOcclusion = material.occlusionStrength;
            proxy.normalStrength = material.normalScale;
            proxy.opacity = material.albedoColor[3];
            proxy.opacityMaskClipValue = 0.333f;
            // Unity-style height maps should not deform the mesh by default.
            // Keep the texture assignment for future parallax support, but do
            // not push live vertex displacement in the current runtime path.
            proxy.displacementScale = 0.0f;
            proxy.uvTiling = material.tiling;
            proxy.uvOffset = material.offset;
            proxy.albedoTexture = material.albedoTexture;
            proxy.metallicTexture = material.metallicTexture;
            proxy.normalTexture = material.normalTexture;
            proxy.ambientOcclusionTexture = material.occlusionTexture;
            proxy.emissiveTexture = material.emissionTexture;
            proxy.heightTexture = material.heightTexture;
            if (!proxy.normalTexture.empty() &&
                (proxy.heightTexture.empty() || proxy.heightTexture == proxy.normalTexture) &&
                LooksLikeHeightTexture(proxy.normalTexture))
            {
                proxy.heightTexture = proxy.normalTexture;
                proxy.normalTexture.clear();
            }
            proxy.featureFlags =
                MaterialFeature_CastShadows |
                MaterialFeature_ReceiveShadows |
                MaterialFeature_ReceiveDecals;
            return proxy;
        }

        bool LoadMaterialComponentFromJsonAsset(
            const std::filesystem::path& assetPath,
            MaterialComponent& outMaterial,
            std::string& outError)
        {
            outError.clear();
            if (assetPath.empty())
            {
                outError = "Material asset path is empty.";
                return false;
            }

            std::ifstream stream(assetPath);
            if (!stream.is_open())
            {
                outError = "Failed to open material asset: " + assetPath.string();
                return false;
            }

            nlohmann::json root;
            try
            {
                stream >> root;
            }
            catch (const std::exception& exception)
            {
                outError = "Failed to parse material asset: " + std::string(exception.what());
                return false;
            }

            auto material = outMaterial;
            material.name = root.value("name", material.name);
            material.shader = root.value("shader", material.shader);
            material.sharedMaterial = root.value("sharedMaterial", material.sharedMaterial);
            material.renderingMode = static_cast<MaterialRenderingMode>(
                root.value("renderingMode", static_cast<int>(material.renderingMode)));
            material.albedoColor = root.value("albedoColor", material.albedoColor);
            material.albedoTexture = root.value("albedoTexture", material.albedoTexture);
            material.metallicTexture = root.value("metallicTexture", material.metallicTexture);
            material.metallic = root.value("metallic", material.metallic);
            material.smoothness = root.value("smoothness", material.smoothness);
            material.smoothnessSource = static_cast<MaterialSmoothnessSource>(
                root.value("smoothnessSource", static_cast<int>(material.smoothnessSource)));
            material.enableHighlights = root.value("enableHighlights", material.enableHighlights);
            material.enableReflections = root.value("enableReflections", material.enableReflections);
            material.normalTexture = root.value("normalTexture", material.normalTexture);
            material.normalScale = root.value("normalScale", material.normalScale);
            material.heightTexture = root.value("heightTexture", material.heightTexture);
            material.heightScale = root.value("heightScale", material.heightScale);
            material.occlusionTexture = root.value("occlusionTexture", material.occlusionTexture);
            material.occlusionStrength = root.value("occlusionStrength", material.occlusionStrength);
            material.emissionEnabled = root.value("emissionEnabled", material.emissionEnabled);
            material.emissionTexture = root.value("emissionTexture", material.emissionTexture);
            material.emissionColor = root.value("emissionColor", material.emissionColor);
            material.emissionIntensity = root.value("emissionIntensity", material.emissionIntensity);
            material.globalIllumination = static_cast<MaterialGlobalIlluminationMode>(
                root.value("globalIllumination", static_cast<int>(material.globalIllumination)));
            material.detailMaskTexture = root.value("detailMaskTexture", material.detailMaskTexture);
            material.tiling = root.value("tiling", material.tiling);
            material.offset = root.value("offset", material.offset);
            material.detailAlbedoTexture = root.value("detailAlbedoTexture", material.detailAlbedoTexture);
            material.detailNormalTexture = root.value("detailNormalTexture", material.detailNormalTexture);
            material.detailNormalScale = root.value("detailNormalScale", material.detailNormalScale);
            material.detailTiling = root.value("detailTiling", material.detailTiling);
            material.detailOffset = root.value("detailOffset", material.detailOffset);
            material.uvSet = root.value("uvSet", material.uvSet);

            outMaterial = std::move(material);
            return true;
        }

        bool IsRawSceneMeshPath(const std::filesystem::path& path)
        {
            const std::string extension = ToLowerString(path.extension().string());
            return extension == ".obj" ||
                extension == ".fbx" ||
                extension == ".gltf" ||
                extension == ".glb";
        }

        std::string TooltipLabelFromImGuiLabel(const char* label)
        {
            return UI::Tooltip::VisibleLabel(label);
        }

        void ShowItemTooltip(const std::string& tooltip)
        {
            UI::Tooltip::Show(tooltip);
        }

        void ShowItemTooltip(const char* tooltip)
        {
            UI::Tooltip::Show(tooltip == nullptr ? std::string_view {} : std::string_view(tooltip));
        }

        void ShowItemTooltipFromLabel(const char* label, const char* prefix = nullptr)
        {
            if (prefix != nullptr && prefix[0] != '\0')
            {
                UI::Tooltip::ShowForItemLabel(label, prefix);
            }
            else
            {
                UI::Tooltip::ShowForItemLabel(label);
            }
        }

        template <typename... Args>
        bool CheckboxWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::Checkbox(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Toggle ");
            return changed;
        }

        template <typename... Args>
        bool ComboWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::Combo(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Select ");
            return changed;
        }

        template <typename... Args>
        bool ButtonWithTooltip(const char* label, Args&&... args)
        {
            const bool pressed = ImGui::Button(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return pressed;
        }

        template <typename... Args>
        bool SmallButtonWithTooltip(const char* label, Args&&... args)
        {
            const bool pressed = ImGui::SmallButton(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return pressed;
        }

        template <typename... Args>
        bool InputTextWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::InputText(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }

        template <typename... Args>
        bool InputTextWithHintWithTooltip(const char* label, const char* hint, Args&&... args)
        {
            const bool changed = ImGui::InputTextWithHint(label, hint, std::forward<Args>(args)...);
            const std::string visibleLabel = TooltipLabelFromImGuiLabel(label);
            if (!visibleLabel.empty())
            {
                ShowItemTooltip("Edit " + visibleLabel);
            }
            else
            {
                ShowItemTooltip(hint);
            }
            return changed;
        }

        template <typename... Args>
        bool SelectableWithTooltip(const char* label, Args&&... args)
        {
            const bool selected = ImGui::Selectable(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return selected;
        }

        template <typename... Args>
        bool MenuItemWithTooltip(const char* label, Args&&... args)
        {
            const bool activated = ImGui::MenuItem(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label);
            return activated;
        }

        template <typename... Args>
        bool DragFloatWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragFloat(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool DragFloat2WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragFloat2(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool DragFloat3WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::DragFloat3(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool SliderFloatWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::SliderFloat(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        template <typename... Args>
        bool ColorEdit3WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::ColorEdit3(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }

        template <typename... Args>
        bool ColorEdit4WithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::ColorEdit4(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Edit ");
            return changed;
        }

        template <typename... Args>
        bool InputIntWithTooltip(const char* label, Args&&... args)
        {
            const bool changed = ImGui::InputInt(label, std::forward<Args>(args)...);
            ShowItemTooltipFromLabel(label, "Adjust ");
            return changed;
        }

        std::vector<std::string> TokenizeCommandLine(const std::string& input)
        {
            std::vector<std::string> tokens;
            std::string token;
            bool inQuotes = false;

            for (std::size_t i = 0; i < input.size(); ++i)
            {
                const char c = input[i];
                if (c == '"')
                {
                    inQuotes = !inQuotes;
                    continue;
                }

                if (!inQuotes && std::isspace(static_cast<unsigned char>(c)))
                {
                    if (!token.empty())
                    {
                        tokens.push_back(token);
                        token.clear();
                    }
                    continue;
                }

                token.push_back(c);
            }

            if (!token.empty())
            {
                tokens.push_back(token);
            }

            return tokens;
        }

        std::string LogLevelLabel(const LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Trace:
                return "Trace";
            case LogLevel::Info:
                return "Info";
            case LogLevel::Warn:
                return "Warn";
            case LogLevel::Error:
                return "Error";
            case LogLevel::Fatal:
                return "Fatal";
            default:
                return "Info";
            }
        }

        ContentItemType DetectContentItemType(const std::filesystem::path& path, const bool isDirectory)
        {
            if (isDirectory)
            {
                return ContentItemType::Folder;
            }

            const std::string extension = ToLowerString(path.extension().string());
            if (extension == ".scene" || extension == ".lumascene")
            {
                return ContentItemType::Scene;
            }
            if (extension == ".lua" || extension == ".lumascript" || extension == ".cs" || extension == ".cpp" || extension == ".h" ||
                extension == ".hpp" || extension == ".py")
            {
                return ContentItemType::Script;
            }
            if (extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".tga" ||
                extension == ".bmp" || extension == ".dds" || extension == ".hdr" || extension == ".exr" ||
                extension == ".lumatex" || extension == ".lumasky")
            {
                return ContentItemType::Image;
            }
            if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb" ||
                extension == ".lumamesh")
            {
                return ContentItemType::Mesh;
            }
            if (extension == ".material" || extension == ".mat" || extension == ".lumamat" || extension == ".mtl")
            {
                return ContentItemType::Material;
            }
            if (extension == ".glsl" || extension == ".hlsl" || extension == ".vert" || extension == ".frag" || extension == ".comp")
            {
                return ContentItemType::Shader;
            }
            if (extension == ".wav" || extension == ".mp3" || extension == ".ogg" || extension == ".flac" || extension == ".lumaaudio")
            {
                return ContentItemType::Audio;
            }
            if (extension == ".lpro" || extension == ".lumaproc" || extension == ".procjson" || extension == ".lumaprocedural")
            {
                return ContentItemType::Procedural;
            }
            if (extension == ".lpk" || extension == ".lumapkg" || extension == ".lpkmanifest")
            {
                return ContentItemType::Package;
            }

            return ContentItemType::Other;
        }

        const char* ContentTypeLabel(const ContentItemType type)
        {
            switch (type)
            {
            case ContentItemType::Folder:
                return "Folder";
            case ContentItemType::Scene:
                return "Scene";
            case ContentItemType::Script:
                return "Script";
            case ContentItemType::Image:
                return "Image";
            case ContentItemType::Mesh:
                return "Mesh";
            case ContentItemType::Material:
                return "Material";
            case ContentItemType::Shader:
                return "Shader";
            case ContentItemType::Audio:
                return "Audio";
            case ContentItemType::Procedural:
                return "Procedural";
            case ContentItemType::Package:
                return "Package";
            case ContentItemType::Other:
            default:
                return "Asset";
            }
        }

        ImU32 ContentTypeColor(const ContentItemType type)
        {
            switch (type)
            {
            case ContentItemType::Folder:
                return IM_COL32(232, 146, 36, 255);
            case ContentItemType::Scene:
                return IM_COL32(90, 149, 255, 255);
            case ContentItemType::Script:
                return IM_COL32(86, 198, 142, 255);
            case ContentItemType::Image:
                return IM_COL32(70, 120, 190, 255);
            case ContentItemType::Mesh:
                return IM_COL32(130, 90, 170, 255);
            case ContentItemType::Material:
                return IM_COL32(110, 150, 90, 255);
            case ContentItemType::Shader:
                return IM_COL32(150, 70, 70, 255);
            case ContentItemType::Audio:
                return IM_COL32(170, 150, 70, 255);
            case ContentItemType::Procedural:
                return IM_COL32(95, 135, 185, 255);
            case ContentItemType::Package:
                return IM_COL32(150, 110, 190, 255);
            case ContentItemType::Other:
            default:
                return IM_COL32(90, 90, 90, 255);
            }
        }

        const char* SkyLightTypeLabel(const SceneSkyLightType type)
        {
            switch (type)
            {
            case SceneSkyLightType::Color:
                return "Color";
            case SceneSkyLightType::HDRI:
                return "HDRI";
            case SceneSkyLightType::Procedural:
                return "Procedural";
            default:
                return "HDRI";
            }
        }

        const char* SkyUpdateModeLabel(const SceneSkyUpdateMode mode)
        {
            switch (mode)
            {
            case SceneSkyUpdateMode::Static:
                return "Static";
            case SceneSkyUpdateMode::Dynamic:
                return "Dynamic";
            default:
                return "Static";
            }
        }

        const char* ToneMappingOperatorLabel(const ToneMappingOperator toneMappingOperator)
        {
            switch (toneMappingOperator)
            {
            case ToneMappingOperator::Linear:
                return "Linear";
            case ToneMappingOperator::Reinhard:
                return "Reinhard";
            case ToneMappingOperator::ACES:
                return "ACES";
            default:
                return "ACES";
            }
        }

        const char* MaterialRenderingModeLabel(const MaterialRenderingMode mode)
        {
            switch (mode)
            {
            case MaterialRenderingMode::Opaque:
                return "Opaque";
            case MaterialRenderingMode::Cutout:
                return "Cutout";
            case MaterialRenderingMode::Fade:
                return "Fade";
            case MaterialRenderingMode::Transparent:
                return "Transparent";
            default:
                return "Opaque";
            }
        }

        const char* MaterialSmoothnessSourceLabel(const MaterialSmoothnessSource source)
        {
            switch (source)
            {
            case MaterialSmoothnessSource::MetallicAlpha:
                return "Metallic Alpha";
            case MaterialSmoothnessSource::AlbedoAlpha:
                return "Albedo Alpha";
            default:
                return "Metallic Alpha";
            }
        }

        const char* MaterialGlobalIlluminationModeLabel(const MaterialGlobalIlluminationMode mode)
        {
            switch (mode)
            {
            case MaterialGlobalIlluminationMode::Realtime:
                return "Realtime";
            case MaterialGlobalIlluminationMode::Baked:
                return "Baked";
            case MaterialGlobalIlluminationMode::None:
                return "None";
            default:
                return "Realtime";
            }
        }

        const char* RigidBodyTypeLabel(const RigidBodyType bodyType)
        {
            switch (bodyType)
            {
            case RigidBodyType::Static:
                return "Static";
            case RigidBodyType::Dynamic:
                return "Dynamic";
            case RigidBodyType::Kinematic:
                return "Kinematic";
            default:
                return "Dynamic";
            }
        }

        const char* ColliderShapeLabel(const ColliderShapeType shape)
        {
            switch (shape)
            {
            case ColliderShapeType::Box:
                return "Box";
            case ColliderShapeType::Sphere:
                return "Sphere";
            case ColliderShapeType::Capsule:
                return "Capsule";
            case ColliderShapeType::Cylinder:
                return "Cylinder";
            case ColliderShapeType::Mesh:
                return "Mesh";
            default:
                return "Box";
            }
        }

        void ApplyPrimitiveColliderDefaults(ColliderComponent& collider, const PrimitiveType primitive)
        {
            collider.active = true;
            collider.meshConvex = true;

            switch (primitive)
            {
            case PrimitiveType::Plane:
                collider.shape = ColliderShapeType::Box;
                collider.boxHalfExtents = { 0.5f, 0.02f, 0.5f };
                collider.meshSource.clear();
                break;
            case PrimitiveType::Sphere:
                collider.shape = ColliderShapeType::Sphere;
                collider.sphereRadius = 0.5f;
                collider.boxHalfExtents = { 0.5f, 0.5f, 0.5f };
                collider.meshSource.clear();
                break;
            case PrimitiveType::Capsule:
                collider.shape = ColliderShapeType::Capsule;
                collider.capsuleRadius = 0.25f;
                collider.capsuleHalfHeight = 0.25f;
                collider.boxHalfExtents = { 0.25f, 0.5f, 0.25f };
                collider.meshSource.clear();
                break;
            case PrimitiveType::Cylinder:
                collider.shape = ColliderShapeType::Cylinder;
                collider.capsuleRadius = 0.5f;
                collider.capsuleHalfHeight = 0.5f;
                collider.boxHalfExtents = { 0.5f, 0.5f, 0.5f };
                collider.meshSource.clear();
                break;
            case PrimitiveType::Cone:
                collider.shape = ColliderShapeType::Mesh;
                collider.meshSource = "PrimitiveCone";
                collider.boxHalfExtents = { 0.5f, 0.5f, 0.5f };
                break;
            case PrimitiveType::Torus:
                collider.shape = ColliderShapeType::Mesh;
                collider.meshSource = "PrimitiveTorus";
                collider.boxHalfExtents = { 0.5f, 0.15f, 0.5f };
                break;
            case PrimitiveType::Cube:
            default:
                collider.shape = ColliderShapeType::Box;
                collider.boxHalfExtents = { 0.5f, 0.5f, 0.5f };
                collider.meshSource.clear();
                break;
            }
        }

        void EnsurePrimitiveColliderForEntity(
            entt::registry& registry,
            const EntityID entity,
            const PrimitiveType primitive)
        {
            if (entity == entt::null || !registry.valid(entity))
            {
                return;
            }

            ColliderComponent* collider = registry.try_get<ColliderComponent>(entity);
            if (collider == nullptr)
            {
                collider = &registry.emplace<ColliderComponent>(entity);
            }

            ApplyPrimitiveColliderDefaults(*collider, primitive);
        }

        const char* JointProjectionModeLabel(const JointProjectionMode mode)
        {
            switch (mode)
            {
            case JointProjectionMode::None:
                return "None";
            case JointProjectionMode::PositionOnly:
                return "Position Only";
            case JointProjectionMode::PositionAndRotation:
                return "Position And Rotation";
            default:
                return "Position And Rotation";
            }
        }

        const char* JointMotionModeLabel(const JointMotionMode mode)
        {
            switch (mode)
            {
            case JointMotionMode::Locked:
                return "Locked";
            case JointMotionMode::Limited:
                return "Limited";
            case JointMotionMode::Free:
                return "Free";
            default:
                return "Locked";
            }
        }

        const char* CharacterMovementModeLabel(const CharacterMovementMode mode)
        {
            switch (mode)
            {
            case CharacterMovementMode::Walk:
                return "Walk";
            case CharacterMovementMode::Fly:
                return "Fly";
            case CharacterMovementMode::Swim:
                return "Swim";
            default:
                return "Walk";
            }
        }

        const char* ForceFieldShapeLabel(const ForceFieldShape shape)
        {
            switch (shape)
            {
            case ForceFieldShape::Box:
                return "Box";
            case ForceFieldShape::Sphere:
                return "Sphere";
            case ForceFieldShape::Capsule:
                return "Capsule";
            default:
                return "Box";
            }
        }

        const char* ForceFieldTypeLabel(const ForceFieldType type)
        {
            switch (type)
            {
            case ForceFieldType::Directional:
                return "Directional";
            case ForceFieldType::Radial:
                return "Radial";
            case ForceFieldType::Custom:
                return "Custom";
            default:
                return "Directional";
            }
        }

        ImGuizmo::OPERATION ToImGuizmoOperation(const std::uint8_t operation)
        {
            switch (operation)
            {
            case 1:
                return ImGuizmo::ROTATE;
            case 2:
                return ImGuizmo::SCALE;
            case 0:
            default:
                return ImGuizmo::TRANSLATE;
            }
        }

        std::string BuildSkySignature(
            const std::filesystem::path& imagePath,
            const SkyLightComponent& skyLight)
        {
            std::ostringstream signature;
            signature
                << imagePath.string()
                << "|type=" << static_cast<int>(skyLight.skyLightType)
                << "|active=" << (skyLight.active ? 1 : 0)
                << "|intensity=" << skyLight.intensity
                << "|color=" << skyLight.color[0] << "," << skyLight.color[1] << "," << skyLight.color[2]
                << "|skyColor=" << skyLight.skyColor[0] << "," << skyLight.skyColor[1] << "," << skyLight.skyColor[2]
                << "|colorIntensity=" << skyLight.colorIntensity
                << "|rotation=" << skyLight.rotation
                << "|diffuse=" << skyLight.diffuseIntensity
                << "|reflection=" << skyLight.reflectionIntensity
                << "|blend=" << skyLight.blendFactor
                << "|lowerBlack=" << (skyLight.lowerHemisphereIsBlack ? 1 : 0)
                << "|lowerColor=" << skyLight.lowerHemisphereColor[0] << ","
                << skyLight.lowerHemisphereColor[1] << "," << skyLight.lowerHemisphereColor[2];
            return signature.str();
        }

        std::uint8_t LinearToSRGB8(const float linearValue)
        {
            const float clamped = std::clamp(linearValue, 0.0f, 1.0f);
            const float srgb = std::pow(clamped, 1.0f / 2.2f);
            return static_cast<std::uint8_t>(std::clamp(srgb * 255.0f, 0.0f, 255.0f));
        }

        struct Vec3
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
        };

        Vec3 operator+(const Vec3& lhs, const Vec3& rhs)
        {
            return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
        }

        Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
        {
            return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
        }

        Vec3 operator*(const Vec3& value, const float scalar)
        {
            return { value.x * scalar, value.y * scalar, value.z * scalar };
        }

        float Dot(const Vec3& lhs, const Vec3& rhs)
        {
            return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
        }

        Vec3 Cross(const Vec3& lhs, const Vec3& rhs)
        {
            return {
                lhs.y * rhs.z - lhs.z * rhs.y,
                lhs.z * rhs.x - lhs.x * rhs.z,
                lhs.x * rhs.y - lhs.y * rhs.x
            };
        }

        Vec3 Normalize(const Vec3& value)
        {
            const float lengthSq = Dot(value, value);
            if (lengthSq <= 1.0e-8f)
            {
                return {};
            }

            const float invLength = 1.0f / std::sqrt(lengthSq);
            return value * invLength;
        }

        float Length(const Vec3& value)
        {
            return std::sqrt(Dot(value, value));
        }

        Vec3 Lerp(const Vec3& lhs, const Vec3& rhs, const float t)
        {
            const float clampedT = std::clamp(t, 0.0f, 1.0f);
            return {
                lhs.x + (rhs.x - lhs.x) * clampedT,
                lhs.y + (rhs.y - lhs.y) * clampedT,
                lhs.z + (rhs.z - lhs.z) * clampedT
            };
        }

        Vec3 MultiplyComponents(const Vec3& lhs, const Vec3& rhs)
        {
            return { lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z };
        }

        float SmoothStep(const float edge0, const float edge1, const float x)
        {
            const float width = std::max(edge1 - edge0, 1.0e-6f);
            const float t = std::clamp((x - edge0) / width, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        Vec3 MaxVec3(const Vec3& value, const float minValue)
        {
            return {
                std::max(value.x, minValue),
                std::max(value.y, minValue),
                std::max(value.z, minValue)
            };
        }

        Vec3 RotateYDegrees(const Vec3& value, const float degrees)
        {
            constexpr float kPi = 3.14159265359f;
            const float radians = degrees * (kPi / 180.0f);
            const float c = std::cos(radians);
            const float s = std::sin(radians);
            return {
                c * value.x + s * value.z,
                value.y,
                -s * value.x + c * value.z
            };
        }

        std::array<float, 2> DirToEquirectUV(const Vec3& value)
        {
            const Vec3 direction = Normalize(value);
            constexpr float kInvTwoPi = 0.15915494309f;
            constexpr float kInvPi = 0.31830988618f;
            const float u = std::atan2(direction.z, direction.x) * kInvTwoPi + 0.5f;
            const float v = 0.5f - std::asin(std::clamp(direction.y, -1.0f, 1.0f)) * kInvPi;
            return { u - std::floor(u), std::clamp(v, 0.0f, 1.0f) };
        }

        Vec3 EquirectUVToDir(const float u, const float v)
        {
            constexpr float kPi = 3.14159265359f;
            const float azimuth = (u - 0.5f) * 2.0f * kPi;
            const float elevation = (0.5f - v) * kPi;
            const float cosElevation = std::cos(elevation);
            return Normalize({
                std::cos(azimuth) * cosElevation,
                std::sin(elevation),
                std::sin(azimuth) * cosElevation
            });
        }

        Vec3 SampleEquirectNearest(
            const std::vector<float>& pixels,
            const int width,
            const int height,
            const float u,
            const float v)
        {
            if (width <= 0 || height <= 0 || pixels.empty())
            {
                return {};
            }

            const float wrappedU = u - std::floor(u);
            const float clampedV = std::clamp(v, 0.0f, 1.0f);
            const int sourceX = std::clamp(static_cast<int>(wrappedU * static_cast<float>(width)), 0, width - 1);
            const int sourceY = std::clamp(static_cast<int>(clampedV * static_cast<float>(height)), 0, height - 1);
            const std::size_t sourceIndex =
                (static_cast<std::size_t>(sourceY) * static_cast<std::size_t>(width) + static_cast<std::size_t>(sourceX)) * 4ULL;
            return {
                pixels[sourceIndex + 0],
                pixels[sourceIndex + 1],
                pixels[sourceIndex + 2]
            };
        }

        Vec3 EvaluateProceduralSkyLikeLuma(
            const Vec3& direction,
            const Vec3& skyTint,
            const float rotationDegrees,
            const Vec3& lowerHemisphereColor,
            const bool lowerHemisphereIsBlack)
        {
            const float y = std::clamp(direction.y, -1.0f, 1.0f);
            const float t = std::clamp(y * 0.5f + 0.5f, 0.0f, 1.0f);

            const Vec3 zenith = skyTint * 1.15f;
            const Vec3 horizon = Lerp({ 0.75f, 0.83f, 0.96f }, skyTint, 0.45f);
            Vec3 sky = Lerp(horizon, zenith, std::pow(t, 0.55f));

            const Vec3 ground = lowerHemisphereIsBlack ? Vec3 {} : lowerHemisphereColor;
            if (y < 0.0f)
            {
                const float gt = std::clamp((y + 0.15f) / 0.15f, 0.0f, 1.0f);
                sky = Lerp(ground, horizon * 0.65f, gt);
            }

            constexpr float kPi = 3.14159265359f;
            const float radians = rotationDegrees * (kPi / 180.0f);
            const Vec3 sunDir = Normalize({ std::cos(radians), 0.35f, std::sin(radians) });
            const float sunN = std::max(Dot(Normalize(direction), sunDir), 0.0f);
            const float sunDisc = SmoothStep(0.9989f, 0.99978f, sunN) * 1.85f;
            const float sunCore = SmoothStep(0.99982f, 0.99996f, sunN) * 2.10f;
            const float sunGlow = std::pow(sunN, 20.0f) * 0.34f;
            const Vec3 sunColor { 1.0f, 0.93f, 0.82f };
            return sky + sunColor * (sunDisc + sunCore + sunGlow);
        }

        Vec3 ComputeSkyColorLikeLuma(
            const Vec3& inputDirection,
            const SkyLightComponent& skyLight,
            const bool hasEnvironment,
            const std::vector<float>& environmentPixels,
            const int environmentWidth,
            const int environmentHeight)
        {
            const Vec3 direction = RotateYDegrees(Normalize(inputDirection), skyLight.rotation);
            const Vec3 skyTint = {
                std::max(0.0f, skyLight.skyColor[0] * skyLight.colorIntensity),
                std::max(0.0f, skyLight.skyColor[1] * skyLight.colorIntensity),
                std::max(0.0f, skyLight.skyColor[2] * skyLight.colorIntensity)
            };
            const Vec3 lowerColor = {
                std::max(0.0f, skyLight.lowerHemisphereColor[0]),
                std::max(0.0f, skyLight.lowerHemisphereColor[1]),
                std::max(0.0f, skyLight.lowerHemisphereColor[2])
            };

            Vec3 baseSky = skyTint;
            if (skyLight.lowerHemisphereIsBlack && direction.y < 0.0f)
            {
                baseSky = Lerp(lowerColor, baseSky, std::clamp(direction.y + 1.0f, 0.0f, 1.0f));
            }

            Vec3 environmentColor {};
            if (hasEnvironment)
            {
                const std::array<float, 2> uv = DirToEquirectUV(direction);
                environmentColor = SampleEquirectNearest(environmentPixels, environmentWidth, environmentHeight, uv[0], uv[1]);
            }

            Vec3 skyColor {};
            if (skyLight.skyLightType == SceneSkyLightType::Procedural)
            {
                skyColor = EvaluateProceduralSkyLikeLuma(
                    direction,
                    skyTint,
                    skyLight.rotation,
                    lowerColor,
                    skyLight.lowerHemisphereIsBlack);
            }
            else
            {
                skyColor = Lerp(baseSky, environmentColor, std::clamp(skyLight.blendFactor, 0.0f, 1.0f));
            }

            const float skyScale = std::max(0.0f, skyLight.intensity) *
                                   std::max(0.0f, skyLight.diffuseIntensity) *
                                   std::max(0.0f, skyLight.reflectionIntensity);
            return MaxVec3(skyColor * skyScale, 0.0f);
        }

        struct Mat4
        {
            std::array<float, 16> elements = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f
            };
        };

        struct Vec4
        {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 1.0f;
        };

        Vec4 Multiply(const Mat4& matrix, const Vec4& vector)
        {
            return {
                matrix.elements[0] * vector.x + matrix.elements[4] * vector.y + matrix.elements[8] * vector.z +
                    matrix.elements[12] * vector.w,
                matrix.elements[1] * vector.x + matrix.elements[5] * vector.y + matrix.elements[9] * vector.z +
                    matrix.elements[13] * vector.w,
                matrix.elements[2] * vector.x + matrix.elements[6] * vector.y + matrix.elements[10] * vector.z +
                    matrix.elements[14] * vector.w,
                matrix.elements[3] * vector.x + matrix.elements[7] * vector.y + matrix.elements[11] * vector.z +
                    matrix.elements[15] * vector.w
            };
        }

        Mat4 Multiply(const Mat4& lhs, const Mat4& rhs)
        {
            Mat4 result {};
            for (int column = 0; column < 4; ++column)
            {
                for (int row = 0; row < 4; ++row)
                {
                    float value = 0.0f;
                    for (int k = 0; k < 4; ++k)
                    {
                        value += lhs.elements[k * 4 + row] * rhs.elements[column * 4 + k];
                    }
                    result.elements[column * 4 + row] = value;
                }
            }
            return result;
        }

        Mat4 BuildPerspective(const float fovRadians, const float aspectRatio, const float nearPlane, const float farPlane)
        {
            Mat4 result {};
            result.elements.fill(0.0f);

            const float tanHalfFov = std::tan(fovRadians * 0.5f);
            if (std::abs(tanHalfFov) <= 1.0e-6f || std::abs(aspectRatio) <= 1.0e-6f)
            {
                result.elements = {
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f
                };
                return result;
            }

            const float f = 1.0f / tanHalfFov;
            result.elements[0] = f / aspectRatio;
            result.elements[5] = f;
            result.elements[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
            result.elements[11] = -1.0f;
            result.elements[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
            return result;
        }

        Mat4 BuildPerspectiveZeroToOne(
            const float fovRadians,
            const float aspectRatio,
            const float nearPlane,
            const float farPlane)
        {
            Mat4 result {};
            result.elements.fill(0.0f);

            const float tanHalfFov = std::tan(fovRadians * 0.5f);
            if (std::abs(tanHalfFov) <= 1.0e-6f || std::abs(aspectRatio) <= 1.0e-6f)
            {
                result.elements = {
                    1.0f, 0.0f, 0.0f, 0.0f,
                    0.0f, 1.0f, 0.0f, 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f
                };
                return result;
            }

            const float f = 1.0f / tanHalfFov;
            result.elements[0] = f / aspectRatio;
            result.elements[5] = f;
            result.elements[10] = farPlane / (nearPlane - farPlane);
            result.elements[11] = -1.0f;
            result.elements[14] = (farPlane * nearPlane) / (nearPlane - farPlane);
            return result;
        }

        Mat4 BuildLookAt(const Vec3& eye, const Vec3& center, const Vec3& worldUp)
        {
            const Vec3 forward = Normalize(center - eye);
            const Vec3 right = Normalize(Cross(forward, worldUp));
            const Vec3 up = Cross(right, forward);

            Mat4 result {};
            result.elements = {
                right.x, up.x, -forward.x, 0.0f,
                right.y, up.y, -forward.y, 0.0f,
                right.z, up.z, -forward.z, 0.0f,
                -Dot(right, eye), -Dot(up, eye), Dot(forward, eye), 1.0f
            };
            return result;
        }

        const char* PrimitiveTypeLabel(const PrimitiveType type)
        {
            switch (type)
            {
            case PrimitiveType::Cube:
                return "Cube";
            case PrimitiveType::Plane:
                return "Plane";
            case PrimitiveType::Sphere:
                return "Sphere";
            case PrimitiveType::Cylinder:
                return "Cylinder";
            case PrimitiveType::Capsule:
                return "Capsule";
            case PrimitiveType::Cone:
                return "Cone";
            case PrimitiveType::Torus:
                return "Torus";
            default:
                return "Primitive";
            }
        }

        Vec3 RotateByEulerDegrees(const Vec3& point, const std::array<float, 3>& degrees)
        {
            constexpr float kPi = 3.14159265359f;
            const float rx = degrees[0] * (kPi / 180.0f);
            const float ry = degrees[1] * (kPi / 180.0f);
            const float rz = degrees[2] * (kPi / 180.0f);

            const float cosX = std::cos(rx);
            const float sinX = std::sin(rx);
            const float cosY = std::cos(ry);
            const float sinY = std::sin(ry);
            const float cosZ = std::cos(rz);
            const float sinZ = std::sin(rz);

            Vec3 p = point;

            const Vec3 rotatedX {
                p.x,
                p.y * cosX - p.z * sinX,
                p.y * sinX + p.z * cosX
            };
            p = rotatedX;

            const Vec3 rotatedY {
                p.x * cosY + p.z * sinY,
                p.y,
                -p.x * sinY + p.z * cosY
            };
            p = rotatedY;

            const Vec3 rotatedZ {
                p.x * cosZ - p.y * sinZ,
                p.x * sinZ + p.y * cosZ,
                p.z
            };
            return rotatedZ;
        }

        Mat4 BuildTransformMatrix(
            const std::array<float, 3>& translation,
            const std::array<float, 3>& rotationDegrees,
            const std::array<float, 3>& scale)
        {
            const Vec3 xAxis = RotateByEulerDegrees(Vec3 { scale[0], 0.0f, 0.0f }, rotationDegrees);
            const Vec3 yAxis = RotateByEulerDegrees(Vec3 { 0.0f, scale[1], 0.0f }, rotationDegrees);
            const Vec3 zAxis = RotateByEulerDegrees(Vec3 { 0.0f, 0.0f, scale[2] }, rotationDegrees);

            Mat4 result {};
            result.elements = {
                xAxis.x, xAxis.y, xAxis.z, 0.0f,
                yAxis.x, yAxis.y, yAxis.z, 0.0f,
                zAxis.x, zAxis.y, zAxis.z, 0.0f,
                translation[0], translation[1], translation[2], 1.0f
            };
            return result;
        }

        std::uint64_t HashBytes(const void* data, const std::size_t size, std::uint64_t seed)
        {
            constexpr std::uint64_t kFnvPrime = 1099511628211ull;
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            for (std::size_t i = 0; i < size; ++i)
            {
                seed ^= static_cast<std::uint64_t>(bytes[i]);
                seed *= kFnvPrime;
            }
            return seed;
        }

        std::filesystem::path ResolveEngineAssetPath(const std::filesystem::path& relativePath)
        {
            if (relativePath.empty())
            {
                return {};
            }

            std::error_code ec;
            std::filesystem::path current = std::filesystem::current_path(ec);
            if (ec)
            {
                current.clear();
            }

            for (std::filesystem::path probe = current; !probe.empty(); probe = probe.parent_path())
            {
                const std::filesystem::path candidate = probe / relativePath;
                if (std::filesystem::exists(candidate, ec) && !ec)
                {
                    const std::filesystem::path resolved = std::filesystem::weakly_canonical(candidate, ec);
                    return ec ? candidate.lexically_normal() : resolved;
                }

                const std::filesystem::path parent = probe.parent_path();
                if (parent == probe)
                {
                    break;
                }
            }

            return {};
        }

        bool EnsureCameraActorMeshLoaded(
            CameraActorMeshState& state,
            std::string& outError)
        {
            outError.clear();
            const std::filesystem::path cameraMeshPath = ResolveEngineAssetPath("assets/Actors/Camera.obj");
            if (cameraMeshPath.empty())
            {
                outError = "Camera actor mesh not found.";
                state.loadAttempted = true;
                state.loadFailed = true;
                return false;
            }

            if (state.loadAttempted && state.resolvedPath == cameraMeshPath)
            {
                if (state.loadFailed)
                {
                    outError = "Camera actor mesh failed to load.";
                    return false;
                }
                return !state.meshes.empty();
            }

            state = {};
            state.loadAttempted = true;
            state.resolvedPath = cameraMeshPath;

            if (!Assets::LoadMeshSceneParts(cameraMeshPath, state.parts, outError) || state.parts.empty())
            {
                state.loadFailed = true;
                if (outError.empty())
                {
                    outError = "Camera actor mesh contained no geometry.";
                }
                return false;
            }

            state.meshes.reserve(state.parts.size());
            for (const Assets::MeshScenePart& part : state.parts)
            {
                state.meshes.push_back(PrimitiveMeshFactory::BuildMeshDesc(part.mesh));
            }

            return !state.meshes.empty();
        }

        MaterialRenderProxy BuildCameraActorMaterial()
        {
            MaterialRenderProxy material;
            material.name = "Editor.CameraActor";
            material.baseColor = { 0.80f, 0.90f, 1.0f, 1.0f };
            material.emissiveColor = { 0.12f, 0.18f, 0.24f, 1.0f };
            material.emissiveIntensity = 1.0f;
            material.metallic = 0.0f;
            material.roughness = 0.55f;
            material.specular = 0.35f;
            material.featureFlags = MaterialFeature_TwoSided;
            return material;
        }

    }

    TriangleLayer::TriangleLayer()
        : Layer("TriangleLayer"),
          m_SelectedEntity(m_SelectionState.PrimaryRef()),
          m_SelectedEntities(m_SelectionState.EntitiesRef())
    {
    }

    TriangleLayer::~TriangleLayer() = default;

    void TriangleLayer::OnAttach()
    {
        const std::filesystem::path startupScenePath = GetDefaultScenePath();
        Editor::SceneStartupHostContext startupContext {};
        startupContext.scene = &m_Scene;
        startupContext.sceneDocument = &m_SceneDocument;
        startupContext.contentStatus = &m_EditorStatus.Content();
        startupContext.selectedContentEntry = &m_SelectedContentEntry;
        startupContext.projectLoaded = Project::IsLoaded();
        startupContext.loadSceneFromPath = [this](const std::filesystem::path& scenePath) -> bool
        {
            return LoadSceneFromPath(scenePath);
        };
        startupContext.seedDefaultSceneEntities = [this]()
        {
            SeedDefaultSceneEntities();
        };
        startupContext.refreshWindowTitle = [this]()
        {
            RefreshWindowTitle();
        };
        startupContext.updateWorldTransforms = [this]()
        {
            m_Scene.UpdateWorldTransforms();
        };
        m_SceneStartupHostService.Bootstrap(startupContext, startupScenePath);

        m_GameplayInputBindingService.ConfigureEditorGameplayDefaults();
        m_PhysicsSettings = PhysicsSettings {};
        m_PhysicsSettings.backend = PhysicsBackendType::PhysX;
        m_PhysicsSettings.fixedTimeStep = 1.0f / 60.0f;
        m_PhysicsSettings.maxSubSteps = 4;
        m_PhysicsSimulationEnabled = true;
        m_PlayState = EditorPlayState::Stopped;
        m_PlaySceneSnapshot.clear();
        m_PlaySelectedEntityUuids.clear();
        m_PlayPrimarySelectedEntityUuid = 0;
        m_PlaySelectedContentEntry.clear();
        m_PlayGameCameraEntity = entt::null;
        if (!m_PhysicsSystem.Initialize(m_PhysicsSettings))
        {
            LUMA_LOG_WARN("Physics", "Physics API initialized without an active backend.");
        }
        m_PhysicsSystem.SetEnabled(IsSceneSimulationEnabled());
        InitializeConsoleCommands();
        m_ConsoleEntries.clear();
        m_ConsoleLogSinkHandle = Logger::RegisterSink(
            [this](const LogLevel level, const std::string_view category, const std::string_view message, const std::string_view line)
            {
                AddConsoleLine(level, category, message, line);
            });
        m_EditorStatus.Reset();
        m_ProjectSettingsPanel.Reset();

        m_DockLayoutInitialized = false;
        {
            Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
            m_ContentRoot = m_ContentBrowserHostFacadeService.ResolveInitialContentRoot(contentBrowserContext);
        }
        m_ContentRootWatchService.Reset();
        m_ContentBrowserCache.SetThumbnailBudgetMsPerFrame(1.5);
        m_ContentBrowserCache.SetThumbnailConcurrency(1);
        {
            std::string streamingInitError;
            const std::filesystem::path streamingProjectRoot =
                Project::IsLoaded() ? Project::GetProjectRoot() : std::filesystem::current_path();
            if (!m_ResourceStreamingService.Initialize(streamingProjectRoot, streamingInitError))
            {
                m_EditorStatus.Content() = "Resource streaming initialization failed: " + streamingInitError;
            }
            else
            {
                m_ResourceStreamingService.RegisterProvider(std::make_shared<Assets::FileResourceStreamProvider>());
                m_ResourceStreamingService.SetBudget(Assets::StreamingBudget {});
                m_ResourceStreamingService.SetEventCallback(
                    [this](const Assets::StreamEvent& event)
                    {
                        HandleStreamingEvent(event);
                    });
            }
        }

        m_AssetRegistry = Assets::AssetRegistry {};
        m_ImporterRegistry = Assets::ImporterRegistry {};
        Assets::RegisterBuiltInImporters(m_ImporterRegistry);
        m_ImportPipeline = std::make_unique<Assets::ImportPipeline>(m_AssetRegistry, m_ImporterRegistry);

        std::string importInitError;
        const std::filesystem::path importProjectRoot =
            Project::IsLoaded() ? Project::GetProjectRoot() : std::filesystem::current_path();
        m_AssetPipelineInitialized = m_ImportPipeline->Initialize(importProjectRoot, importInitError);
        if (!m_AssetPipelineInitialized)
        {
            m_EditorStatus.Content() = "Asset pipeline initialization failed: " + importInitError;
        }

        {
            m_PackageManagerHostFacadeService.Initialize({
                &m_PackageManagerHostService,
                &m_PackageManagerPanel,
                Project::IsLoaded(),
                Project::IsLoaded() ? &Project::GetProjectRoot() : nullptr,
                &m_ShowPackageManagerPanel,
                [this]()
                {
                    Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
                    m_ContentBrowserHostFacadeService.RefreshRoots(contentBrowserContext);
                },
                [this]()
                {
                    Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
                    m_ContentBrowserHostFacadeService.RefreshContentEntries(contentBrowserContext);
                }
            });
        }

        LUMA_LOG_INFO("Editor", "Console initialized. Open Window > Console for palette/console/tasks.");
    }

    void TriangleLayer::OnDetach()
    {
        m_LuaScriptRuntime.Stop();
        m_ResourceStreamingService.SetEventCallback({});
        m_ResourceStreamingService.Shutdown();

        if (m_ConsoleLogSinkHandle != 0)
        {
            Logger::UnregisterSink(m_ConsoleLogSinkHandle);
            m_ConsoleLogSinkHandle = 0;
        }

        if (m_ActivePipeline && m_LastRenderer != nullptr)
        {
            m_ActivePipeline->Shutdown(*m_LastRenderer, m_GPUResourceManager);
        }
        m_ActivePipeline.reset();
        m_GPUResourceManager.Shutdown();
        Editor::SkyPreviewTextureHostContext skyPreviewContext = BuildSkyPreviewTextureHostContext();
        m_SkyPreviewTextureHostService.Release(skyPreviewContext);
        ReleaseGizmoToolbarIcons();
        m_ContentBrowserCache.Shutdown(m_LastRenderer);
        if (m_StreamingTaskActive && m_StreamingTask != 0)
        {
            EditorTaskManager::EndTask(m_StreamingTask);
        }
        m_ContentImportService.Reset();
        m_StreamingTaskActive = false;
        m_StreamingTask = 0;
        m_StreamingActiveHandles.clear();
        m_StreamedMeshAssets.clear();
        m_StreamingTaskBatchSize = 0;
        m_StreamingTaskLastMessage.clear();
        m_ViewportController.ClearViewportInteraction();
        m_MaterialRenderProxyCacheService.Clear();
        m_ImportPipeline.reset();
        m_AssetPipelineInitialized = false;
        m_PackageManagerHostFacadeService.Shutdown({
            &m_PackageManagerHostService,
            &m_PackageManagerPanel,
            Project::IsLoaded(),
            Project::IsLoaded() ? &Project::GetProjectRoot() : nullptr,
            &m_ShowPackageManagerPanel
        });
        m_PhysicsSystem.Shutdown();
        m_LastRenderer = nullptr;
        m_DockLayoutInitialized = false;
        m_EditorStatus.Reset();
        m_ProjectSettingsPanel.Reset();
        m_ContentRootWatchService.Reset();
        m_PlayState = EditorPlayState::Stopped;
        m_PlaySceneSnapshot.clear();
        m_PlaySelectedEntityUuids.clear();
        m_PlayPrimarySelectedEntityUuid = 0;
        m_PlaySelectedContentEntry.clear();
        m_PlayGameCameraEntity = entt::null;
    }

    void TriangleLayer::OnUpdate(const float deltaTimeSeconds)
    {
        if (IsPlayModeActive() && !IsPlayModePaused())
        {
            m_LuaScriptRuntime.Update(m_Scene, deltaTimeSeconds);
            UpdateSceneAudioRuntime();
        }

        Editor::EditorTickCoordinatorContext tickContext {};
        tickContext.deltaTimeSeconds = deltaTimeSeconds;
        tickContext.timeSeconds = &m_Time;
        tickContext.lastDeltaTimeSeconds = &m_LastDeltaTimeSeconds;
        tickContext.renderer = m_LastRenderer;
        tickContext.showContentBrowserPanel = m_ShowContentBrowserPanel;
        tickContext.contentBrowserCache = &m_ContentBrowserCache;
        tickContext.lastContentBrowserTickMs = &m_LastContentBrowserTickMs;
        tickContext.resourceStreamingService = &m_ResourceStreamingService;
        tickContext.lastStreamingTickMs = &m_LastStreamingTickMs;
        tickContext.physicsSystem = &m_PhysicsSystem;
        tickContext.physicsSimulationEnabled = IsSceneSimulationEnabled();
        tickContext.scene = &m_Scene;
        tickContext.sceneDocument = &m_SceneDocument;
        tickContext.pruneEntitySelection = [this]()
        {
            m_SceneEntityUtilityService.PruneEntitySelection(m_Scene, m_SelectionState);
        };
        tickContext.tickPackageManager = [this]()
        {
            m_PackageManagerHostFacadeService.Tick({
                &m_PackageManagerHostService,
                &m_PackageManagerPanel,
                Project::IsLoaded(),
                Project::IsLoaded() ? &Project::GetProjectRoot() : nullptr,
                &m_ShowPackageManagerPanel,
                [this]()
                {
                    Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
                    m_ContentBrowserHostFacadeService.RefreshRoots(contentBrowserContext);
                },
                [this]()
                {
                    Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
                    m_ContentBrowserHostFacadeService.RefreshContentEntries(contentBrowserContext);
                }
            });
        };
        tickContext.pumpContentFolderTreeRebuild = [this]()
        {
            Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
            m_ContentBrowserHostFacadeService.PumpContentFolderTreeRebuild(contentBrowserContext);
        };
        tickContext.pumpContentEntriesRefresh = [this]()
        {
            Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
            m_ContentBrowserHostFacadeService.PumpContentEntriesRefresh(contentBrowserContext);
        };
        tickContext.tickContentImportQueue = [this]()
        {
            TickContentImportQueue();
        };
        tickContext.updateStreamingTaskState = [this]()
        {
            UpdateStreamingTaskState();
        };
        tickContext.updateConsoleTasks = [this](const float tickDeltaTimeSeconds)
        {
            UpdateConsoleTasks(tickDeltaTimeSeconds);
        };
        tickContext.markSceneRenderCacheDirty = [this]()
        {
            MarkSceneRenderCacheDirty(Editor::SceneRenderCacheDirtyFlags::Geometry);
        };
        tickContext.updateSceneDirtyState = [this]()
        {
            UpdateSceneDirtyState();
        };

        m_EditorTickCoordinatorService.Tick(tickContext);

        if (Project::IsLoaded() && m_ContentRootWatchService.Poll(m_ContentRoots, deltaTimeSeconds))
        {
            Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
            m_ContentBrowserHostFacadeService.RefreshRoots(contentBrowserContext);
            m_ContentBrowserHostFacadeService.InvalidateFolderTreeCache(contentBrowserContext);
            m_ContentBrowserHostFacadeService.RefreshContentEntries(contentBrowserContext);
            m_EditorStatus.Content() = "Detected external content changes. Refreshed Content Browser.";
        }

        if (IsPlayModeActive() && !IsPlayModePaused())
        {
            std::vector<PhysicsEvent> physicsEvents;
            m_PhysicsSystem.ConsumeEvents(physicsEvents);
            if (!physicsEvents.empty())
            {
                m_LuaScriptRuntime.DispatchPhysicsCallbacks(m_Scene, physicsEvents);
            }

            const std::uint32_t fixedStepCount = m_PhysicsSystem.ConsumeSimulatedStepCount();
            if (fixedStepCount > 0)
            {
                m_LuaScriptRuntime.RunFixedUpdates(
                    m_Scene,
                    fixedStepCount,
                    m_PhysicsSettings.fixedTimeStep);
            }
        }
    }

    void TriangleLayer::TickContentImportQueue()
    {
        m_ContentImportService.Tick({
            m_AssetPipelineInitialized,
            m_ImportPipeline.get(),
            [this]()
            {
                Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
                m_ContentBrowserHostFacadeService.InvalidateFolderTreeCache(contentBrowserContext);
            },
            [this]()
            {
                Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
                m_ContentBrowserHostFacadeService.RefreshContentEntries(contentBrowserContext);
            },
            [this](std::string status)
            {
                m_EditorStatus.Content() = std::move(status);
            }
        });
    }

    bool TriangleLayer::EnsureGizmoToolbarIconsLoaded()
    {
        return m_EditorIconService.EnsureLoaded(m_LastRenderer);
    }

    void TriangleLayer::ReleaseGizmoToolbarIcons()
    {
        m_EditorIconService.Release(m_LastRenderer);
    }

    void TriangleLayer::MarkSceneRenderCacheDirty(const Editor::SceneRenderCacheDirtyFlags flags)
    {
        m_RenderSceneCacheDirtyFlags |= flags;
    }

    void TriangleLayer::OnRender(IRenderBackend& renderer)
    {
        Editor::RenderFrameCoordinatorContext renderContext {};
        renderContext.renderer = &renderer;
        renderContext.lastRenderer = &m_LastRenderer;
        renderContext.gpuResourceManager = &m_GPUResourceManager;
        renderContext.activePipeline = &m_ActivePipeline;
        renderContext.activeProfile = &m_ActiveProfile;
        renderContext.desiredProfile =
            Project::IsLoaded() ? Project::GetConfig().pipeline : RenderPipelineProfile::CoreLite;
        renderContext.streamingService = &m_ResourceStreamingService;
        renderContext.scene = &m_Scene;
        renderContext.viewportController = &m_ViewportController;
        renderContext.activeCameraEntity = IsPlayModeActive() ? EnsurePlayModeGameCameraEntity() : entt::null;
        renderContext.selectedEntity = m_SelectedEntity;
        renderContext.timeSeconds = m_Time;
        renderContext.skyMesh = &m_SkyPrimitiveMeshDesc;
        renderContext.skyMeshRevision = m_SkyPrimitiveMeshRevision;
        renderContext.hasSkyMesh = m_HasSkyPrimitiveMesh;
        renderContext.gridMesh = &m_ScenePrimitiveMeshDesc;
        renderContext.gridMeshRevision = m_ScenePrimitiveMeshRevision;
        renderContext.hasGridMesh = m_HasScenePrimitiveMesh;
        renderContext.renderItems = &m_SceneRenderItems;
        renderContext.renderItemsRevision = m_SceneRenderItemsRevision;
        renderContext.skyAverageColor = m_SkyAverageColor;
        renderContext.renderSceneCacheDirtyFlags = m_RenderSceneCacheDirtyFlags;
        renderContext.lastViewportGridEnabled = m_LastViewportGridEnabled;
        renderContext.hasScenePrimitiveMesh = m_HasScenePrimitiveMesh;
        renderContext.hasSkyPrimitiveMesh = m_HasSkyPrimitiveMesh;
        renderContext.lastSkyMeshSignature = &m_LastSkyMeshSignature;
        renderContext.skyboxSourcePath = &m_SkyboxSourcePath;
        renderContext.lastSceneRebuildMs = &m_LastSceneRebuildMs;
        renderContext.lastSceneViewBuildMs = &m_LastSceneViewBuildMs;
        renderContext.lastRenderFrameMs = &m_LastRenderFrameMs;
        renderContext.onRendererChanged = [this]()
        {
            ReleaseGizmoToolbarIcons();
            m_ContentBrowserCache.Shutdown(m_LastRenderer);
        };
        renderContext.syncSkyEnvironmentResources = [this]()
        {
            Editor::SkyPreviewTextureHostContext skyPreviewContext = BuildSkyPreviewTextureHostContext();
            m_SkyPreviewTextureHostService.Sync(skyPreviewContext);
        };
        renderContext.findPrimarySkyEntity = [this]()
        {
            return FindPrimarySkyEntity();
        };
        renderContext.buildSkyMeshSignature = [this](const EntityID)
        {
            Editor::SceneRenderCacheStateContext cacheStateContext = BuildSceneRenderCacheStateContext();
            return m_SceneRenderCacheStateService.BuildActiveSkyMeshSignature(cacheStateContext);
        };
        renderContext.rebuildScenePrimitiveMesh = [this]()
        {
            RebuildScenePrimitiveMesh();
        };
        renderContext.resolveSkyAssetPath = [this](const std::string& assetPath)
        {
            return ResolveSkyAssetPath(assetPath);
        };
        renderContext.setLensSourceEntity = [this](const EntityID lensSourceEntity)
        {
            m_ViewportController.SetLensSourceEntity(lensSourceEntity);
        };
        renderContext.clearSkyRebuildRequested = [this](const EntityID skyEntity)
        {
            auto& registry = m_Scene.GetRegistry();
            if (skyEntity == entt::null ||
                !registry.valid(skyEntity) ||
                !registry.all_of<SkyLightComponent>(skyEntity))
            {
                return;
            }

            auto& skyLight = registry.get<SkyLightComponent>(skyEntity);
            if (skyLight.active)
            {
                skyLight.rebuildIBLRequested = false;
            }
        };
        renderContext.buildBlendedPostProcessView =
            [this](const std::array<float, 3>& cameraWorldPosition, ScenePostProcessView& outPostProcess)
        {
                BuildBlendedPostProcessView(cameraWorldPosition, outPostProcess);
            };

        m_RenderFrameCoordinatorService.Render(renderContext);
    }

    bool TriangleLayer::IsAssetPipelineInitialized() const
    {
        return m_AssetPipelineInitialized;
    }

    bool TriangleLayer::IsPackageRegistryLoaded() const
    {
        return m_PackageManagerPanel.IsRegistryLoaded();
    }

    bool TriangleLayer::HasContentRoots() const
    {
        return !m_ContentRoots.empty();
    }

    bool TriangleLayer::HasRendererBinding() const
    {
        return m_LastRenderer != nullptr;
    }

    bool TriangleLayer::HasRenderPipeline() const
    {
        return m_ActivePipeline != nullptr;
    }

    bool TriangleLayer::IsDockLayoutInitialized() const
    {
        return m_DockLayoutInitialized;
    }

    bool TriangleLayer::HasSceneEntities() const
    {
        const auto view = m_Scene.GetRegistry().view<IDComponent>();
        return view.begin() != view.end();
    }

    std::size_t TriangleLayer::GetAssetRegistryCount() const
    {
        return m_AssetRegistry.GetAllAssets().size();
    }

    std::size_t TriangleLayer::GetContentRootCount() const
    {
        return m_ContentRoots.size();
    }

    std::size_t TriangleLayer::GetContentEntryCount() const
    {
        return m_ContentBrowserCache.GetContentEntries().size();
    }

    void TriangleLayer::OnImGuiRender()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.04f, 0.04f, 0.04f, 1.0f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.08f, 0.08f, 0.08f, 1.0f);

        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
        {
            SaveActiveScene();
        }

        ImGuizmo::BeginFrame();
        DrawDockspace();

        if (m_ShowHierarchyPanel)
        {
            EnsureGizmoToolbarIconsLoaded();
            const auto& icons = m_EditorIconService.Icons();
            Editor::HierarchyPanelContext hierarchyPanelContext {};
            hierarchyPanelContext.scene = &m_Scene;
            hierarchyPanelContext.panelIconTexture = icons.hierarchyPanel;
            hierarchyPanelContext.createIconTexture = icons.hierarchyCreate;
            hierarchyPanelContext.cameraIconTexture = icons.hierarchyCamera;
            hierarchyPanelContext.cubeIconTexture = icons.hierarchyCube;
            hierarchyPanelContext.planeIconTexture = icons.hierarchyPlane;
            hierarchyPanelContext.sphereIconTexture = icons.hierarchySphere;
            hierarchyPanelContext.cylinderIconTexture = icons.hierarchyCylinder;
            hierarchyPanelContext.drawEntityCreationMenu = [this](const EntityID parentEntity)
            {
                m_EntityCreationMenu.Draw({
                    parentEntity,
                    [this](const Editor::EntityTemplateKind templateKind, const EntityID requestedParentEntity)
                    {
                        CreateEntityFromTemplate(templateKind, requestedParentEntity);
                    }
                });
            };
            hierarchyPanelContext.isEntitySelected = [this](const EntityID entity) -> bool
            {
                return m_SceneEntityUtilityService.IsEntitySelected(m_SelectionState, entity);
            };
            hierarchyPanelContext.isEntityHidden = [this](const EntityID entity) -> bool
            {
                const auto& registry = m_Scene.GetRegistry();
                return entity != entt::null &&
                    registry.valid(entity) &&
                    registry.all_of<EditorRuntimeOnlyComponent>(entity);
            };
            hierarchyPanelContext.selectSingleEntity = [this](const EntityID entity)
            {
                Editor::SceneEntitySelectionContext selectionContext { &m_Scene, &m_SelectionState, &m_SelectedContentEntry };
                m_SceneEntityUtilityService.SelectSingleEntity(selectionContext, entity);
            };
            hierarchyPanelContext.toggleEntitySelection = [this](const EntityID entity)
            {
                Editor::SceneEntitySelectionContext selectionContext { &m_Scene, &m_SelectionState, &m_SelectedContentEntry };
                m_SceneEntityUtilityService.ToggleEntitySelection(selectionContext, entity);
            };
            hierarchyPanelContext.pruneSelection = [this]()
            {
                m_SceneEntityUtilityService.PruneEntitySelection(m_Scene, m_SelectionState);
            };
            hierarchyPanelContext.markSceneRenderCacheDirty = [this]()
            {
                MarkSceneRenderCacheDirty(Editor::SceneRenderCacheDirtyFlags::Geometry);
            };
            hierarchyPanelContext.createEntityFromMeshAsset =
                [this](const std::filesystem::path& assetPath, const EntityID parentEntity) -> EntityID
            {
                return CreateEntityFromMeshAsset(assetPath, parentEntity);
            };
            hierarchyPanelContext.createPrefabFromEntity = [this](const EntityID entity) -> bool
            {
                return CreatePrefabFromEntity(entity);
            };
            hierarchyPanelContext.isMeshAssetPathCandidate = [](const std::filesystem::path& assetPath) -> bool
            {
                return IsMeshAssetPathCandidate(assetPath);
            };
            hierarchyPanelContext.setContentStatus = [this](std::string status)
            {
                m_EditorStatus.Content() = std::move(status);
            };
            m_HierarchyPanel.Draw(&m_ShowHierarchyPanel, hierarchyPanelContext);
        }
        if (m_ShowViewportPanel)
        {
            DrawViewportPanel();
        }
        else
        {
            m_ViewportController.ClearViewportInteraction();
            if (Input::GetCursorMode() == CursorMode::Disabled)
            {
                Input::SetCursorMode(CursorMode::Normal);
            }
        }
        if (m_ShowInspectorPanel)
        {
            DrawInspectorPanel();
        }
        if (m_ShowContentBrowserPanel)
        {
            DrawContentBrowserPanel();
        }
        if (m_ShowConsolePanel)
        {
            DrawConsolePanel();
        }
        if (m_ShowProjectSettingsPanel)
        {
            Editor::ProjectSettingsPanelContext projectSettingsContext {};
            projectSettingsContext.resetGameplayInputBindings = [this]()
            {
                m_GameplayInputBindingService.ConfigureEditorGameplayDefaults();
            };
            projectSettingsContext.onProjectConfigSaved = [this]()
            {
                RefreshWindowTitle();
            };
            projectSettingsContext.setProjectInputStatus = [this](std::string status)
            {
                m_EditorStatus.SetProjectInput(std::move(status));
            };
            projectSettingsContext.getProjectInputStatus = [this]() -> std::string_view
            {
                return m_EditorStatus.GetProjectInput();
            };
            projectSettingsContext.setProjectConfigStatus = [this](std::string status)
            {
                m_EditorStatus.SetProjectConfig(std::move(status));
            };
            projectSettingsContext.getProjectConfigStatus = [this]() -> std::string_view
            {
                return m_EditorStatus.GetProjectConfig();
            };
            m_ProjectSettingsPanel.Draw(&m_ShowProjectSettingsPanel, projectSettingsContext);
        }
        if (m_ShowPreferencesPanel)
        {
            m_PreferencesPanel.Draw(&m_ShowPreferencesPanel, m_ViewportController);
        }
        if (!Editor::HasBuiltInPlugins())
        {
            m_ShowPluginsPanel = false;
        }
        if (m_ShowPluginsPanel)
        {
            Editor::PluginsPanelContext pluginsPanelContext {};
            pluginsPanelContext.invalidateProjectSettingsDraft = [this]()
            {
                m_ProjectSettingsPanel.InvalidateDraft();
            };
            pluginsPanelContext.setProjectConfigStatus = [this](std::string status)
            {
                m_EditorStatus.SetProjectConfig(std::move(status));
            };
            pluginsPanelContext.getProjectConfigStatus = [this]() -> std::string_view
            {
                return m_EditorStatus.GetProjectConfig();
            };
            m_PluginsPanel.Draw(&m_ShowPluginsPanel, pluginsPanelContext);
        }
        {
            m_PackageManagerHostFacadeService.Draw({
                &m_PackageManagerHostService,
                &m_PackageManagerPanel,
                Project::IsLoaded(),
                Project::IsLoaded() ? &Project::GetProjectRoot() : nullptr,
                &m_ShowPackageManagerPanel,
                [this]()
                {
                    Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
                    m_ContentBrowserHostFacadeService.RefreshRoots(contentBrowserContext);
                },
                [this]()
                {
                    Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
                    m_ContentBrowserHostFacadeService.RefreshContentEntries(contentBrowserContext);
                }
            });
        }
        if (m_ShowGPUResourcesPanel)
        {
            DrawGPUResourcesPanel();
        }
        if (!m_ShowProjectSettingsPanel)
        {
            m_ProjectSettingsPanel.CancelInputCapture();
        }

        DrawUnsavedScenePrompt();
    }

    bool TriangleLayer::OnCloseRequested()
    {
        if (IsPlayModeActive() && !StopPlayMode())
        {
            return false;
        }

        if (!IsSceneDirty())
        {
            return true;
        }

        RequestPendingSceneActionClose();
        return false;
    }

    void TriangleLayer::DrawEditorToolbar()
    {
        EnsureGizmoToolbarIconsLoaded();
        const auto& icons = m_EditorIconService.Icons();
        m_EditorToolbarPanel.Draw({
            icons.toolbarSelectionDetails,
            icons.toolbarPause,
            icons.toolbarStop,
            m_PlayState == EditorPlayState::Stopped,
            m_PlayState != EditorPlayState::Stopped,
            m_PlayState != EditorPlayState::Stopped,
            m_PlayState == EditorPlayState::Playing,
            m_PlayState == EditorPlayState::Paused,
            [this]()
            {
                EnterPlayMode();
            },
            [this]()
            {
                TogglePausePlayMode();
            },
            [this]()
            {
                StopPlayMode();
            }
        });
    }

    void TriangleLayer::EnterPlayMode()
    {
        if (IsPlayModeActive())
        {
            return;
        }

        m_PlayGameCameraEntity = EnsurePlayModeGameCameraEntity();
        if (m_PlayGameCameraEntity == entt::null)
        {
            bool hasAnySceneCamera = false;
            const auto cameraView = m_Scene.GetRegistry().view<CameraComponent>();
            for (const EntityID entity : cameraView)
            {
                if (m_Scene.GetRegistry().all_of<EditorRuntimeOnlyComponent>(entity))
                {
                    continue;
                }

                hasAnySceneCamera = true;
                break;
            }

            m_EditorStatus.Content() =
                hasAnySceneCamera
                    ? "No Primary Camera Is Selected."
                    : "Play failed: add a scene Camera and mark it Primary first.";
            return;
        }

        std::string snapshotError;
        if (!CaptureSceneSnapshot(m_PlaySceneSnapshot, snapshotError))
        {
            m_EditorStatus.Content() = "Play failed: " + snapshotError;
            m_PlayGameCameraEntity = entt::null;
            return;
        }

        m_PlaySelectedEntityUuids = CaptureSelectedEntityUuids();
        m_PlayPrimarySelectedEntityUuid = 0;
        if (m_SelectedEntity != entt::null &&
            m_Scene.GetRegistry().valid(m_SelectedEntity) &&
            m_Scene.GetRegistry().all_of<IDComponent>(m_SelectedEntity))
        {
            m_PlayPrimarySelectedEntityUuid = m_Scene.GetRegistry().get<IDComponent>(m_SelectedEntity).id;
        }
        m_PlaySelectedContentEntry = m_SelectedContentEntry;
        m_PlayState = EditorPlayState::Playing;
        m_LuaScriptRuntime.Start(m_Scene);
        StartSceneAudioPlayback();
        m_PhysicsSystem.SetEnabled(IsSceneSimulationEnabled());
        m_EditorStatus.Content() = "Entered Play mode using the highest-priority primary Camera.";
    }

    void TriangleLayer::TogglePausePlayMode()
    {
        if (!IsPlayModeActive())
        {
            m_EditorStatus.Content() = "Pause is only available while playing.";
            return;
        }

        m_PlayState = IsPlayModePaused() ? EditorPlayState::Playing : EditorPlayState::Paused;
        m_PhysicsSystem.SetEnabled(IsSceneSimulationEnabled());
        m_EditorStatus.Content() = IsPlayModePaused() ? "Play mode paused." : "Play mode resumed.";
    }

    bool TriangleLayer::StopPlayMode()
    {
        if (!IsPlayModeActive())
        {
            return true;
        }
        m_LuaScriptRuntime.Stop();
        StopSceneAudioPlayback();
        Audio::AudioSystem::StopAll();
        if (m_PlaySceneSnapshot.empty())
        {
            m_PlayState = EditorPlayState::Stopped;
            m_PhysicsSystem.SetEnabled(false);
            m_EditorStatus.Content() = "Stopped Play mode.";
            return true;
        }

        Editor::SceneDocumentHostContext context {};
        context.scene = &m_Scene;
        context.sceneDocument = &m_SceneDocument;
        context.contentStatus = &m_EditorStatus.Content();
        context.selectedContentEntry = &m_SelectedContentEntry;
        context.afterLoad = [this]()
        {
            RestoreSelectedEntityUuids(m_PlaySelectedEntityUuids, m_PlayPrimarySelectedEntityUuid);
            m_SelectedContentEntry = m_PlaySelectedContentEntry;
            m_ViewportController.ClearViewportInteraction();
        };

        if (!m_SceneDocumentHostService.RestoreSceneSnapshot(context, m_PlaySceneSnapshot))
        {
            return false;
        }

        m_PlayState = EditorPlayState::Stopped;
        m_PlaySceneSnapshot.clear();
        m_PlaySelectedEntityUuids.clear();
        m_PlayPrimarySelectedEntityUuid = 0;
        m_PlaySelectedContentEntry.clear();
        m_PlayGameCameraEntity = entt::null;
        m_PhysicsSystem.SetEnabled(false);
        MarkSceneRenderCacheDirty(Editor::SceneRenderCacheDirtyFlags::All);
        UpdateSceneDirtyState();
        m_EditorStatus.Content() = "Stopped Play mode and restored the editor scene.";
        return true;
    }

    void TriangleLayer::StartSceneAudioPlayback()
    {
        auto& registry = m_Scene.GetRegistry();
        if (!HasEnabledAudioListener(registry))
        {
            Audio::AudioSystem::SetMasterVolume(0.0f);
            const auto silentView = registry.view<AudioSourceComponent>();
            for (const EntityID entity : silentView)
            {
                silentView.get<AudioSourceComponent>(entity).runtimeHandle = 0;
            }
            return;
        }

        const auto view = registry.view<AudioSourceComponent>();
        for (const EntityID entity : view)
        {
            auto& source = view.get<AudioSourceComponent>(entity);
            if (!source.playOnAwake || source.clipAsset.empty() || source.mute)
            {
                source.runtimeHandle = 0;
                continue;
            }

            const auto clip = Audio::AudioSystem::LoadClip(source.clipAsset);
            if (!clip)
            {
                source.runtimeHandle = 0;
                continue;
            }

            Audio::PlaySettings settings {};
            settings.volume = source.volume;
            settings.pitch = source.pitch;
            settings.looping = source.looping;
            settings.spatialized = source.spatialized;
            settings.minDistance = source.minDistance;
            settings.maxDistance = source.maxDistance;

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
        }
    }

    void TriangleLayer::StopSceneAudioPlayback()
    {
        auto& registry = m_Scene.GetRegistry();
        const auto view = registry.view<AudioSourceComponent>();
        for (const EntityID entity : view)
        {
            auto& source = view.get<AudioSourceComponent>(entity);
            if (source.runtimeHandle != 0)
            {
                Audio::AudioSystem::Stop(source.runtimeHandle);
                source.runtimeHandle = 0;
            }
        }
        Audio::AudioSystem::StopAll();
    }

    void TriangleLayer::UpdateSceneAudioRuntime()
    {
        auto& registry = m_Scene.GetRegistry();

        EntityID activeListener = entt::null;
        const auto listenerView = registry.view<AudioListenerComponent, TransformComponent>();
        for (const EntityID entity : listenerView)
        {
            const auto& listener = listenerView.get<AudioListenerComponent>(entity);
            if (!listener.enabled)
            {
                continue;
            }

            activeListener = entity;
            const auto& transform = listenerView.get<TransformComponent>(entity);
            const std::array<float, 3> forward = NormalizeOrFallback(
                RotateByEulerDegrees(std::array<float, 3> { 0.0f, 0.0f, -1.0f }, transform.worldRotation),
                std::array<float, 3> { 0.0f, 0.0f, -1.0f });
            const std::array<float, 3> up = NormalizeOrFallback(
                RotateByEulerDegrees(std::array<float, 3> { 0.0f, 1.0f, 0.0f }, transform.worldRotation),
                std::array<float, 3> { 0.0f, 1.0f, 0.0f });
            Audio::AudioSystem::SetListenerTransform(transform.worldPosition, forward, up);
            Audio::AudioSystem::SetMasterVolume(listener.volume);
            break;
        }

        if (activeListener == entt::null)
        {
            Audio::AudioSystem::SetMasterVolume(0.0f);
        }

        const auto sourceView = registry.view<AudioSourceComponent>();
        for (const EntityID entity : sourceView)
        {
            auto& source = sourceView.get<AudioSourceComponent>(entity);

            if (source.runtimeHandle != 0 && !Audio::AudioSystem::IsPlaying(source.runtimeHandle))
            {
                source.runtimeHandle = 0;
            }

            if (source.clipAsset.empty() || source.mute)
            {
                if (source.runtimeHandle != 0)
                {
                    Audio::AudioSystem::Stop(source.runtimeHandle);
                    source.runtimeHandle = 0;
                }
                continue;
            }

            if (source.runtimeHandle == 0 && source.playOnAwake)
            {
                const auto clip = Audio::AudioSystem::LoadClip(source.clipAsset);
                if (clip)
                {
                    Audio::PlaySettings settings {};
                    settings.volume = source.volume;
                    settings.pitch = source.pitch;
                    settings.looping = source.looping;
                    settings.spatialized = source.spatialized;
                    settings.minDistance = source.minDistance;
                    settings.maxDistance = source.maxDistance;

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
                }
            }

            if (source.runtimeHandle == 0)
            {
                continue;
            }

            Audio::AudioSystem::SetVolume(source.runtimeHandle, source.volume);
            Audio::AudioSystem::SetPitch(source.runtimeHandle, source.pitch);
            if (source.spatialized && registry.all_of<TransformComponent>(entity))
            {
                Audio::AudioSystem::SetPosition(source.runtimeHandle, registry.get<TransformComponent>(entity).worldPosition);
            }
        }
    }

    EntityID TriangleLayer::EnsurePlayModeGameCameraEntity()
    {
        m_PlayGameCameraEntity = Editor::FindHighestPriorityPrimaryCameraEntity(m_Scene);
        return m_PlayGameCameraEntity;
    }

    void TriangleLayer::DrawDockspace()
    {
        DrawFooter();

        m_EditorDockspaceService.Update(m_DockLayoutInitialized);

        const bool canSaveScene = m_SceneDocument.HasCurrentScenePath() || Project::IsLoaded();
        const std::string projectLabel =
            Project::IsLoaded() ? ("Project: " + Project::GetConfig().name) : "Project: None";
        const std::string sceneLabel =
            "Scene: " + GetActiveSceneDisplayName() + (m_SceneDocument.IsDirtyFlag() ? "*" : "");

        m_EditorMenuBarPanel.Draw({
            canSaveScene,
            Project::IsLoaded(),
            m_SceneDocument.HasCurrentScenePath(),
            Project::IsLoaded() && m_SceneDocument.HasCurrentScenePath(),
            &m_ShowProjectSettingsPanel,
            &m_ShowPreferencesPanel,
            &m_ShowPluginsPanel,
            &m_ViewportController.ShowCameraDebugOverlay(),
            &m_ViewportController.ShowGrid(),
            &m_ViewportController.PreviewSceneCameraLens(),
            &m_ShowHierarchyPanel,
            &m_ShowViewportPanel,
            &m_ShowInspectorPanel,
            &m_ShowContentBrowserPanel,
            &m_ShowConsolePanel,
            &m_ShowPackageManagerPanel,
            &m_ShowFooter,
            &m_ShowGPUResourcesPanel,
            sceneLabel,
            projectLabel,
            [this]() { RequestNewScene(); },
            [this]() { SaveActiveScene(); },
            [this]() { OpenSaveSceneAsPrompt(); },
            [this]() { RequestReloadScene(); },
            [this]() { SetProjectStartScene(m_SceneDocument.GetCurrentScenePath()); }
        });

        DrawEditorToolbar();
    }

    void TriangleLayer::DrawFooter()
    {
        if (!m_ShowFooter)
        {
            return;
        }

        const std::string importStatus = m_ContentImportService.BuildFooterStatus();
        std::string footerMessage = "Ready.";
        LogLevel footerMessageLevel = LogLevel::Info;
        {
            std::scoped_lock lock(m_ConsoleMutex);
            if (!m_ConsoleEntries.empty())
            {
                const ConsoleEntry& latestEntry = m_ConsoleEntries.back();
                footerMessageLevel = latestEntry.level;
                if (!latestEntry.category.empty())
                {
                    footerMessage = "[" + latestEntry.category + "] " + latestEntry.message;
                }
                else if (!latestEntry.message.empty())
                {
                    footerMessage = latestEntry.message;
                }
                else if (!latestEntry.line.empty())
                {
                    footerMessage = latestEntry.line;
                }
            }
            else if (!importStatus.empty())
            {
                footerMessage = importStatus;
            }
        }

        const float frameDelta = std::max(m_LastDeltaTimeSeconds, 1.0e-4f);
        const float frameMs = frameDelta * 1000.0f;
        const float fps = 1.0f / frameDelta;
        const char* apiLabel = m_LastRenderer != nullptr ? ToString(m_LastRenderer->GetAPI()).data() : "Unknown";
        const GPUResourceManager::Stats gpuStats = m_GPUResourceManager.GetStats();
        char metricsBuffer[512] = {};
        std::snprintf(
            metricsBuffer,
            sizeof(metricsBuffer),
            "Pipeline: %.*s | API: %s | %.0f FPS | %.2f ms | CPU CB:%.2f ST:%.2f RB:%.2f SV:%.2f RF:%.2f | GPU RP:%llu PS:%llu M:%llu T:%llu RT:%llu FB:%llu DS:%llu Q:%llu",
            static_cast<int>(ToString(m_ActiveProfile).size()),
            ToString(m_ActiveProfile).data(),
            apiLabel,
            std::round(fps),
            frameMs,
            m_LastContentBrowserTickMs,
            m_LastStreamingTickMs,
            m_LastSceneRebuildMs,
            m_LastSceneViewBuildMs,
            m_LastRenderFrameMs,
            static_cast<unsigned long long>(gpuStats.renderPassCount),
            static_cast<unsigned long long>(gpuStats.pipelineCount),
            static_cast<unsigned long long>(gpuStats.meshCount),
            static_cast<unsigned long long>(gpuStats.textureCount),
            static_cast<unsigned long long>(gpuStats.renderTargetCount),
            static_cast<unsigned long long>(gpuStats.framebufferCount),
            static_cast<unsigned long long>(gpuStats.descriptorSetCount),
            static_cast<unsigned long long>(gpuStats.pendingDestroyCount));
        const std::string metricsText(metricsBuffer);

        m_FooterBarPanel.Draw({
            footerMessage,
            footerMessageLevel,
            metricsText
        });
    }

    void TriangleLayer::DrawGPUResourcesPanel()
    {
        m_GPUResourcesPanel.Draw(
            &m_ShowGPUResourcesPanel,
            {
                m_GPUResourceManager.GetStats(),
                m_GPUResourceManager.GetDebugEntries(),
                m_ResourceStreamingService.GetStats(),
                m_ResourceStreamingService.GetRecords(),
                m_ContentBrowserCache.GetThumbnailStats(),
                m_LastSceneRebuildMs,
                m_LastSceneViewBuildMs,
                m_LastRenderFrameMs
            });
    }

    void TriangleLayer::InitializeConsoleCommands()
    {
        m_ConsoleCommandHostService.Initialize(BuildConsoleCommandHostContext());
    }

    void TriangleLayer::AddConsoleLine(
        const LogLevel level,
        const std::string_view category,
        const std::string_view message,
        const std::string_view line)
    {
        ConsoleEntry entry {
            level,
            std::string(category),
            std::string(message),
            std::string(line)
        };

        std::scoped_lock lock(m_ConsoleMutex);
        m_ConsoleEntries.push_back(std::move(entry));
        if (m_ConsoleEntries.size() > m_ConsoleMaxEntries)
        {
            const std::size_t overflow = m_ConsoleEntries.size() - m_ConsoleMaxEntries;
            m_ConsoleEntries.erase(m_ConsoleEntries.begin(), m_ConsoleEntries.begin() + static_cast<std::ptrdiff_t>(overflow));
        }
        m_ConsoleScrollToBottom = true;
    }

    void TriangleLayer::UpdateConsoleTasks(const float deltaTimeSeconds)
    {
        for (ConsoleTaskState& task : m_ConsoleTasks)
        {
            if (!task.running)
            {
                continue;
            }

            task.progress = std::clamp(task.progress + deltaTimeSeconds * task.speed, 0.0f, 1.0f);
            if (task.progress >= 1.0f)
            {
                task.running = false;
                LUMA_LOG_INFO("Task", task.name + " completed.");
            }
        }
    }

    void TriangleLayer::ExecuteConsoleCommand(const std::string_view commandLine, const bool addToHistory)
    {
        m_ConsoleCommandHostService.Execute(BuildConsoleCommandHostContext(), commandLine, addToHistory);
    }

    Editor::ConsoleCommandHostContext TriangleLayer::BuildConsoleCommandHostContext()
    {
        Editor::ConsoleCommandHostContext context {};
        context.commandService = &m_ConsoleCommandService;
        context.packageManagerHostFacadeService = &m_PackageManagerHostFacadeService;
        context.commands = &m_ConsoleCommands;
        context.favoriteCommands = &m_ConsoleFavoriteCommands;
        context.cvars = &m_ConsoleCvars;
        context.tasks = &m_ConsoleTasks;
        context.commandHistory = &m_ConsoleCommandHistory;
        context.recentCommands = &m_ConsoleRecentCommands;
        context.historyCursor = &m_ConsoleHistoryCursor;
        context.vsyncEnabled = Project::IsLoaded() && Project::GetConfig().vsync;
        context.viewportGridEnabled = m_ViewportController.ShowGrid();
        context.cameraDebugOverlayEnabled = m_ViewportController.ShowCameraDebugOverlay();
        context.showHierarchyPanel = &m_ShowHierarchyPanel;
        context.showViewportPanel = &m_ShowViewportPanel;
        context.showInspectorPanel = &m_ShowInspectorPanel;
        context.showContentBrowserPanel = &m_ShowContentBrowserPanel;
        context.showConsolePanel = &m_ShowConsolePanel;
        context.showPackageManagerPanel = &m_ShowPackageManagerPanel;
        context.showFooter = &m_ShowFooter;
        context.showGpuResourcesPanel = &m_ShowGPUResourcesPanel;
        context.showProjectSettingsPanel = &m_ShowProjectSettingsPanel;
        context.showPreferencesPanel = &m_ShowPreferencesPanel;
        context.viewportGridEnabledState = &m_ViewportController.ShowGrid();
        context.cameraDebugOverlayEnabledState = &m_ViewportController.ShowCameraDebugOverlay();
        context.activeProfile = &m_ActiveProfile;
        context.lastDeltaTimeSeconds = m_LastDeltaTimeSeconds;
        context.primitiveMeshLod = &m_PrimitiveMeshLod;
        context.resourceStreamingService = &m_ResourceStreamingService;
        context.scene = &m_Scene;
        context.packageManagerHostContext.hostService = &m_PackageManagerHostService;
        context.packageManagerHostContext.panel = &m_PackageManagerPanel;
        context.packageManagerHostContext.projectLoaded = Project::IsLoaded();
        if (context.packageManagerHostContext.projectLoaded)
        {
            context.packageManagerHostContext.projectRoot = &Project::GetProjectRoot();
        }
        context.packageManagerHostContext.showPackageManagerPanel = &m_ShowPackageManagerPanel;
        context.packageManagerHostContext.refreshContentRoots = [this]()
        {
            Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
            m_ContentBrowserHostFacadeService.RefreshRoots(contentBrowserContext);
        };
        context.packageManagerHostContext.refreshContentEntries = [this]()
        {
            Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
            m_ContentBrowserHostFacadeService.RefreshContentEntries(contentBrowserContext);
        };
        context.selectSingleEntity = [this](const EntityID entity)
        {
            Editor::SceneEntitySelectionContext selectionContext { &m_Scene, &m_SelectionState, &m_SelectedContentEntry };
            m_SceneEntityUtilityService.SelectSingleEntity(selectionContext, entity);
        };
        context.refreshContentBrowser = [this]()
        {
            Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
            m_ContentBrowserHostFacadeService.InvalidateFolderTreeCache(contentBrowserContext);
            m_ContentBrowserHostFacadeService.RefreshContentEntries(contentBrowserContext);
        };
        context.openProjectSettings = [this]()
        {
            m_ShowProjectSettingsPanel = true;
        };
        context.openPreferences = [this]()
        {
            m_ShowPreferencesPanel = true;
        };
        context.markSceneRenderCacheDirty = [this]()
        {
            MarkSceneRenderCacheDirty(Editor::SceneRenderCacheDirtyFlags::Geometry);
        };
        context.clearConsoleOutput = [this]()
        {
            {
                std::scoped_lock lock(m_ConsoleMutex);
                m_ConsoleEntries.clear();
            }
            m_ConsoleScrollToBottom = true;
        };
        return context;
    }

    Editor::ContentBrowserHostFacadeContext TriangleLayer::BuildContentBrowserHostFacadeContext()
    {
        Editor::ContentBrowserHostFacadeContext context {};
        context.open = &m_ShowContentBrowserPanel;
        context.contentRoot = &m_ContentRoot;
        context.currentDirectory = &m_CurrentContentDirectory;
        context.selectedEntry = &m_SelectedContentEntry;
        context.roots = &m_ContentRoots;
        context.activeRootIndex = &m_ActiveContentRootIndex;
        context.status = &m_EditorStatus.Content();
        context.cache = &m_ContentBrowserCache;
        Editor::ContentImportState& contentImportState = m_ContentImportService.State();
        context.pendingImports = &contentImportState.pendingImports;
        context.contentImportCursor = &contentImportState.cursor;
        context.importedCount = &contentImportState.importedCount;
        context.skippedCount = &contentImportState.skippedCount;
        context.failedCount = &contentImportState.failedCount;
        context.firstError = &contentImportState.firstError;
        context.contentImportTask = &contentImportState.task;
        context.contentImportActive = &contentImportState.active;
        context.assetPipelineInitialized = m_AssetPipelineInitialized;
        context.hasImportPipeline = (m_ImportPipeline != nullptr);
        context.projectLoaded = Project::IsLoaded();
        context.projectAssetsPath = Project::IsLoaded() ? Project::GetAssetsPath() : std::filesystem::path {};
        context.thumbnailRenderer = m_LastRenderer;
        context.getMountRoots = [this]()
        {
            return m_PackageManagerPanel.GetMountRoots();
        };
        context.requestLoadScene = [this](const std::filesystem::path& scenePath)
        {
            RequestLoadScene(scenePath);
        };
        context.openAsset = [this](const std::filesystem::path& assetPath, const std::string& entryName)
        {
            std::string extension = assetPath.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](const unsigned char ch)
            {
                return static_cast<char>(std::tolower(ch));
            });
            if (extension == ".lumaprefab")
            {
                InstantiatePrefabAsset(assetPath);
                return;
            }
            m_EditorStatus.Content() = "Open asset: " + entryName;
        };
        return context;
    }

    void TriangleLayer::DrawConsolePanel()
    {
        m_ConsolePanelHostService.Draw({
            &m_ConsolePanel,
            &m_ShowConsolePanel,
            &m_CommandPaletteQuery,
            &m_ConsoleCommands,
            &m_ConsoleRecentCommands,
            &m_ConsoleFavoriteCommands,
            &m_ConsoleCommandInput,
            [this](const std::string_view commandLine, const bool addToHistory)
            {
                ExecuteConsoleCommand(commandLine, addToHistory);
            },
            &m_ConsoleEntries,
            &m_ConsoleMutex,
            &m_ConsoleSearchQuery,
            &m_ConsoleCommandHistory,
            &m_ConsoleHistoryCursor,
            &m_ConsoleFilterTrace,
            &m_ConsoleFilterInfo,
            &m_ConsoleFilterWarn,
            &m_ConsoleFilterError,
            &m_ConsoleFilterFatal,
            &m_ConsoleAutoScroll,
            &m_ConsoleScrollToBottom,
            &m_ConsoleTasks,
            [this](const std::size_t index)
            {
                if (index >= m_ConsoleTasks.size())
                {
                    return;
                }

                ConsoleTaskState& task = m_ConsoleTasks[index];
                task.running = true;
                task.progress = 0.0f;
                LUMA_LOG_INFO("Task", "Started task: " + task.name);
            },
            [this](const std::size_t index)
            {
                if (index >= m_ConsoleTasks.size())
                {
                    return;
                }

                ConsoleTaskState& task = m_ConsoleTasks[index];
                task.running = false;
                task.progress = 0.0f;
                LUMA_LOG_WARN("Task", "Canceled task: " + task.name);
            }
        });
    }

    void TriangleLayer::DrawInspectorPanel()
    {
        m_InspectorHostService.Draw({
            &m_ShowInspectorPanel,
            &m_Scene,
            &m_SelectedContentEntry,
            m_SelectedEntity,
            &m_LuaScriptRuntime,
            IsPlayModeActive(),
            m_PhysicsSystem.GetBackendName(),
            &m_PhysicsSimulationEnabled,
            &m_ContentRoots,
            &m_ImportedSceneParts,
            Project::IsLoaded() ? &Project::GetConfig().tags : nullptr,
            &m_MaterialTextureAssetPickerService,
            &m_MaterialTextureAssetPickerPanel,
            &m_InspectorPanel,
            &m_InspectorEntityPanel,
            &m_InspectorMeshRendererPanel,
            &m_InspectorAudioPanel,
            &m_InspectorCameraLightingPanel,
            &m_InspectorScriptPanel,
            &m_InspectorPhysicsPanel,
            &m_InspectorDestructionPanel,
            &m_InspectorJointPanel,
            &m_InspectorAdvancedPhysicsPanel,
            &m_InspectorVehiclePhysicsPanel,
            &m_InspectorFieldBuoyancyPanel,
            &m_InspectorPhysicsEventsPanel,
            &m_InspectorEnvironmentEffectsPanel,
            &m_InspectorMaterialPanel,
            &m_InspectorAddComponentPanel,
            [this](const EntityID entity)
            {
                return CreatePrefabFromEntity(entity);
            },
            [this](const EntityID entity)
            {
                return ApplyPrefabInstance(entity);
            },
            [this](const EntityID entity)
            {
                return RevertPrefabInstance(entity);
            },
            [this](const EntityID entity)
            {
                return GetPrefabInstanceStatus(entity);
            },
            [this](const EntityID entity)
            {
                return GetPrefabOverridePaths(entity);
            },
            [this](const EntityID entity, const std::string_view componentPath)
            {
                return RevertPrefabComponent(entity, componentPath);
            },
            [this](const EntityID entity, const std::string_view overridePath)
            {
                return RevertPrefabOverridePath(entity, overridePath);
            },
            [this](const EntityID entity)
            {
                SelectPrefabAsset(entity);
            },
            [this](const EntityID entity, const PrimitiveType primitive)
            {
                EnsurePrimitiveColliderForEntity(m_Scene.GetRegistry(), entity, primitive);
            },
            [this]()
            {
                MarkSceneRenderCacheDirty();
            },
            [this]()
            {
                MarkSceneRenderCacheDirty(Editor::SceneRenderCacheDirtyFlags::Materials);
            },
            [this]()
            {
                MarkSceneRenderCacheDirty(Editor::SceneRenderCacheDirtyFlags::Environment);
            },
            [this]()
            {
                MarkSceneRenderCacheDirty(Editor::SceneRenderCacheDirtyFlags::Geometry);
            },
            [this](const std::string& assetPath)
            {
                return ResolveSkyAssetPath(assetPath);
            },
            [this](const std::filesystem::path& assetPath, const bool isDirectory) -> void*
            {
                return m_ContentThumbnailHostService.GetOrCreateThumbnail(
                    m_ContentBrowserCache,
                    m_LastRenderer,
                    assetPath,
                    isDirectory);
            },
            [](MaterialComponent& material)
            {
                InitializeDefaultMaterialComponent(material);
            },
            [this](const std::filesystem::path& assetPath, MaterialComponent& outMaterial, std::string& outError) -> bool
            {
                return LoadMaterialComponentFromJsonAsset(assetPath, outMaterial, outError);
            },
            [this](const std::string_view message)
            {
                AddConsoleLine(LogLevel::Warn, "Material", message, message);
            },
            [this](SkyLightComponent& skyLight)
            {
                m_SceneEntityUtilityService.InitializeSkyLightDefaults(skyLight);
            },
            [this](PostProcessComponent& postProcess)
            {
                m_SceneEntityUtilityService.InitializePostProcessDefaults(postProcess);
            },
            [this]()
            {
                return IsSelectionValid();
            },
            [this](std::string status)
            {
                m_EditorStatus.Content() = std::move(status);
            }
        });
    }

    EntityID TriangleLayer::CreateEntityFromTemplate(const Editor::EntityTemplateKind templateKind, const EntityID parentEntity)
    {
        return m_EntityTemplateCreationService.CreateEntityFromTemplate(
            {
                &m_Scene,
                [this](const std::string& baseName)
                {
                    return m_SceneEntityUtilityService.GenerateUniqueEntityName(m_Scene, baseName);
                },
                [this](const EntityID entity, const PrimitiveType primitive)
                {
                    EnsurePrimitiveColliderForEntity(m_Scene.GetRegistry(), entity, primitive);
                },
                [](MaterialComponent& material)
                {
                    InitializeDefaultMaterialComponent(material);
                },
                [this](SkyLightComponent& skyLight)
                {
                    m_SceneEntityUtilityService.InitializeSkyLightDefaults(skyLight);
                },
                [this](const EntityID entity)
                {
                    Editor::SceneEntitySelectionContext selectionContext { &m_Scene, &m_SelectionState, &m_SelectedContentEntry };
                    m_SceneEntityUtilityService.SelectSingleEntity(selectionContext, entity);
                }
            },
            templateKind,
            parentEntity);
    }

    EntityID TriangleLayer::CreateEntityFromMeshAsset(
        const std::filesystem::path& assetPath,
        const EntityID parentEntity,
        const std::array<float, 3>* worldPosition)
    {
        if (!IsMeshAssetPathCandidate(assetPath))
        {
            return entt::null;
        }

        std::filesystem::path activeContentRoot;
        Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
        if (const ContentBrowserRoot* activeRoot = m_ContentBrowserHostFacadeService.GetActiveRoot(contentBrowserContext);
            activeRoot != nullptr)
        {
            activeContentRoot = activeRoot->path;
        }

        return m_MeshEntityImportService.CreateEntityFromMeshAsset(
            {
                &m_Scene,
                activeContentRoot,
                Project::IsLoaded(),
                Project::IsLoaded() ? Project::GetAssetsPath() : std::filesystem::path {},
                Project::IsLoaded() ? Project::GetProjectRoot() : std::filesystem::path {},
                [this](const std::string& baseName)
                {
                    return m_SceneEntityUtilityService.GenerateUniqueEntityName(m_Scene, baseName);
                },
                [this](const EntityID entity)
                {
                    Editor::SceneEntitySelectionContext selectionContext { &m_Scene, &m_SelectionState, &m_SelectedContentEntry };
                    m_SceneEntityUtilityService.SelectSingleEntity(selectionContext, entity);
                },
                [this](const std::string_view message)
                {
                    AddConsoleLine(LogLevel::Warn, "Import", message, message);
                },
                [](MaterialComponent& material)
                {
                    InitializeDefaultMaterialComponent(material);
                },
                [this](
                    const std::string& cacheKey,
                    const std::filesystem::path& resolvedPath,
                    const std::vector<Assets::MeshScenePart>& parts)
                {
                    ImportedScenePartsState& importedSceneState = m_ImportedSceneParts[cacheKey];
                    importedSceneState.resolvedPath = resolvedPath;
                    importedSceneState.parts = parts;
                    importedSceneState.loadAttempted = true;
                    importedSceneState.loadFailed = false;
                }
            },
            assetPath,
            parentEntity,
            worldPosition);
    }

    std::filesystem::path TriangleLayer::BuildUniquePrefabAssetPath(const std::string_view baseName) const
    {
        if (!Project::IsLoaded())
        {
            return {};
        }

        std::filesystem::path targetDirectory = Project::GetAssetsPath() / "Prefabs";
        if (!m_CurrentContentDirectory.empty() && PathIsWithinRoot(m_CurrentContentDirectory, Project::GetAssetsPath()))
        {
            targetDirectory = m_CurrentContentDirectory;
        }

        const std::string fileStem = SanitizeFileStem(std::string(baseName));
        std::error_code ec;
        std::filesystem::create_directories(targetDirectory, ec);
        if (ec)
        {
            return {};
        }

        for (int suffixIndex = 0; suffixIndex < 1000; ++suffixIndex)
        {
            const std::string suffix = suffixIndex == 0 ? "" : " " + std::to_string(suffixIndex);
            const std::filesystem::path candidate = targetDirectory / (fileStem + suffix + ".lumaprefab");
            if (!std::filesystem::exists(candidate, ec))
            {
                return candidate;
            }
        }

        return {};
    }

    EntityID TriangleLayer::FindPrefabInstanceRoot(const EntityID entity) const
    {
        const auto& registry = m_Scene.GetRegistry();
        EntityID current = entity;
        while (current != entt::null && registry.valid(current))
        {
            if (const auto* prefabInstance = registry.try_get<PrefabInstanceComponent>(current);
                prefabInstance != nullptr && prefabInstance->isRoot)
            {
                return current;
            }

            const auto* relationship = registry.try_get<RelationshipComponent>(current);
            if (relationship == nullptr)
            {
                break;
            }
            current = relationship->parent;
        }

        return entt::null;
    }

    void TriangleLayer::MarkPrefabInstanceHierarchy(const EntityID rootEntity, const std::string& prefabAsset)
    {
        auto& registry = m_Scene.GetRegistry();
        if (!registry.valid(rootEntity))
        {
            return;
        }

        std::function<void(EntityID, bool)> markRecursive;
        markRecursive = [&](const EntityID entity, const bool isRoot)
        {
            if (!registry.valid(entity) || !registry.all_of<IDComponent>(entity))
            {
                return;
            }

            PrefabInstanceComponent prefabInstance {};
            prefabInstance.prefabAsset = prefabAsset;
            prefabInstance.sourceEntityId = registry.get<IDComponent>(entity).id;
            prefabInstance.isRoot = isRoot;
            registry.emplace_or_replace<PrefabInstanceComponent>(entity, std::move(prefabInstance));

            const auto* relationship = registry.try_get<RelationshipComponent>(entity);
            if (relationship == nullptr)
            {
                return;
            }

            for (const EntityID child : relationship->children)
            {
                markRecursive(child, false);
            }
        };

        markRecursive(rootEntity, true);
    }

    bool TriangleLayer::CreatePrefabFromEntity(const EntityID rootEntity)
    {
        if (IsPlayModeActive())
        {
            m_EditorStatus.Content() = "Prefab creation is unavailable during play mode.";
            return false;
        }
        if (!Project::IsLoaded())
        {
            m_EditorStatus.Content() = "Load a project before creating prefabs.";
            return false;
        }

        const auto& registry = m_Scene.GetRegistry();
        if (!registry.valid(rootEntity) || !registry.all_of<TagComponent>(rootEntity))
        {
            m_EditorStatus.Content() = "Select a valid entity to create a prefab.";
            return false;
        }

        const auto& tag = registry.get<TagComponent>(rootEntity);
        const std::filesystem::path prefabPath = BuildUniquePrefabAssetPath(tag.name.empty() ? "NewPrefab" : tag.name);
        if (prefabPath.empty())
        {
            m_EditorStatus.Content() = "Failed to allocate a prefab asset path.";
            return false;
        }

        m_Scene.UpdateWorldTransforms();
        std::string error;
        if (!PrefabSerializer::SerializePrefab(m_Scene, rootEntity, prefabPath, error))
        {
            m_EditorStatus.Content() = "Failed to create prefab: " + error;
            return false;
        }

        MarkPrefabInstanceHierarchy(rootEntity, prefabPath.lexically_normal().generic_string());
        m_PrefabStatusCacheRootEntity = entt::null;
        m_PrefabOverrideCacheEntity = entt::null;
        Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
        m_ContentBrowserHostFacadeService.InvalidateFolderTreeCache(contentBrowserContext);
        m_ContentBrowserHostFacadeService.RefreshContentEntries(contentBrowserContext);
        m_EditorStatus.Content() = "Created prefab: " + prefabPath.filename().string();
        return true;
    }

    EntityID TriangleLayer::InstantiatePrefabAsset(const std::filesystem::path& prefabPath, const EntityID parentEntity)
    {
        if (IsPlayModeActive())
        {
            m_EditorStatus.Content() = "Prefab instancing is unavailable during play mode.";
            return entt::null;
        }

        std::string error;
        EntityID instantiatedRoot = entt::null;
        if (!PrefabSerializer::InstantiatePrefab(m_Scene, prefabPath, &instantiatedRoot, error) || instantiatedRoot == entt::null)
        {
            m_EditorStatus.Content() = "Failed to instantiate prefab: " + error;
            return entt::null;
        }

        if (parentEntity != entt::null && m_Scene.GetRegistry().valid(parentEntity))
        {
            m_Scene.SetParent(instantiatedRoot, parentEntity);
        }
        m_Scene.UpdateWorldTransforms();

        Editor::SceneEntitySelectionContext selectionContext { &m_Scene, &m_SelectionState, &m_SelectedContentEntry };
        m_SceneEntityUtilityService.SelectSingleEntity(selectionContext, instantiatedRoot);
        m_PrefabStatusCacheRootEntity = entt::null;
        m_PrefabOverrideCacheEntity = entt::null;
        MarkSceneRenderCacheDirty(Editor::SceneRenderCacheDirtyFlags::All);
        m_EditorStatus.Content() = "Instantiated prefab: " + prefabPath.filename().string();
        return instantiatedRoot;
    }

    bool TriangleLayer::ApplyPrefabInstance(const EntityID entity)
    {
        if (IsPlayModeActive())
        {
            m_EditorStatus.Content() = "Prefab apply is unavailable during play mode.";
            return false;
        }

        const EntityID rootEntity = FindPrefabInstanceRoot(entity);
        auto& registry = m_Scene.GetRegistry();
        const auto* prefabInstance = rootEntity != entt::null ? registry.try_get<PrefabInstanceComponent>(rootEntity) : nullptr;
        if (prefabInstance == nullptr)
        {
            m_EditorStatus.Content() = "Selected entity is not part of a prefab instance.";
            return false;
        }

        std::filesystem::path prefabPath(prefabInstance->prefabAsset);
        if (!prefabPath.is_absolute() && Project::IsLoaded())
        {
            prefabPath = (Project::GetProjectRoot() / prefabPath).lexically_normal();
        }

        m_Scene.UpdateWorldTransforms();
        std::string error;
        if (!PrefabSerializer::SerializePrefab(m_Scene, rootEntity, prefabPath, error))
        {
            m_EditorStatus.Content() = "Failed to apply prefab: " + error;
            return false;
        }

        MarkPrefabInstanceHierarchy(rootEntity, prefabPath.lexically_normal().generic_string());
        m_PrefabStatusCacheRootEntity = entt::null;
        m_PrefabOverrideCacheEntity = entt::null;
        Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
        m_ContentBrowserHostFacadeService.InvalidateFolderTreeCache(contentBrowserContext);
        m_ContentBrowserHostFacadeService.RefreshContentEntries(contentBrowserContext);
        m_EditorStatus.Content() = "Applied prefab: " + prefabPath.filename().string();
        return true;
    }

    bool TriangleLayer::RevertPrefabInstance(const EntityID entity)
    {
        if (IsPlayModeActive())
        {
            m_EditorStatus.Content() = "Prefab revert is unavailable during play mode.";
            return false;
        }

        const EntityID rootEntity = FindPrefabInstanceRoot(entity);
        auto& registry = m_Scene.GetRegistry();
        const auto* prefabInstance = rootEntity != entt::null ? registry.try_get<PrefabInstanceComponent>(rootEntity) : nullptr;
        const auto* relationship = rootEntity != entt::null ? registry.try_get<RelationshipComponent>(rootEntity) : nullptr;
        if (prefabInstance == nullptr)
        {
            m_EditorStatus.Content() = "Selected entity is not part of a prefab instance.";
            return false;
        }

        std::filesystem::path prefabPath(prefabInstance->prefabAsset);
        if (!prefabPath.is_absolute() && Project::IsLoaded())
        {
            prefabPath = (Project::GetProjectRoot() / prefabPath).lexically_normal();
        }

        const EntityID parentEntity = relationship != nullptr ? relationship->parent : entt::null;
        EntityID newRootEntity = InstantiatePrefabAsset(prefabPath, entt::null);
        if (newRootEntity == entt::null)
        {
            return false;
        }

        if (parentEntity != entt::null && registry.valid(parentEntity))
        {
            m_Scene.SetParent(newRootEntity, parentEntity);
        }

        m_Scene.DestroyEntity(rootEntity);
        m_Scene.UpdateWorldTransforms();

        Editor::SceneEntitySelectionContext selectionContext { &m_Scene, &m_SelectionState, &m_SelectedContentEntry };
        m_SceneEntityUtilityService.SelectSingleEntity(selectionContext, newRootEntity);
        m_PrefabStatusCacheRootEntity = entt::null;
        m_PrefabOverrideCacheEntity = entt::null;
        MarkSceneRenderCacheDirty(Editor::SceneRenderCacheDirtyFlags::All);
        m_EditorStatus.Content() = "Reverted prefab: " + prefabPath.filename().string();
        return true;
    }

    std::string TriangleLayer::GetPrefabInstanceStatus(const EntityID entity)
    {
        const EntityID rootEntity = FindPrefabInstanceRoot(entity);
        auto& registry = m_Scene.GetRegistry();
        const auto* prefabInstance = rootEntity != entt::null ? registry.try_get<PrefabInstanceComponent>(rootEntity) : nullptr;
        if (prefabInstance == nullptr)
        {
            return {};
        }

        const bool sceneDirty = IsSceneDirty();
        if (rootEntity == m_PrefabStatusCacheRootEntity &&
            prefabInstance->prefabAsset == m_PrefabStatusCachePrefabAsset &&
            sceneDirty == m_PrefabStatusCacheSceneDirty &&
            (m_Time - m_PrefabStatusCacheTimeSeconds) < 0.5f)
        {
            return m_PrefabStatusCacheValue;
        }

        std::filesystem::path prefabPath(prefabInstance->prefabAsset);
        if (!prefabPath.is_absolute() && Project::IsLoaded())
        {
            prefabPath = (Project::GetProjectRoot() / prefabPath).lexically_normal();
        }

        std::error_code ec;
        if (!std::filesystem::exists(prefabPath, ec))
        {
            m_PrefabStatusCacheRootEntity = rootEntity;
            m_PrefabStatusCachePrefabAsset = prefabInstance->prefabAsset;
            m_PrefabStatusCacheSceneDirty = sceneDirty;
            m_PrefabStatusCacheTimeSeconds = m_Time;
            m_PrefabStatusCacheValue = "Source Missing";
            return m_PrefabStatusCacheValue;
        }

        std::filesystem::path tempPath =
            std::filesystem::temp_directory_path(ec) /
            ("luma_prefab_compare_" + std::to_string(static_cast<std::uint32_t>(entt::to_integral(rootEntity))) + ".lumaprefab");
        std::string error;
        std::string statusValue = "Unknown";
        if (!ec && PrefabSerializer::SerializePrefab(m_Scene, rootEntity, tempPath, error))
        {
            std::ifstream sourceInput(prefabPath, std::ios::binary);
            std::ifstream tempInput(tempPath, std::ios::binary);
            if (sourceInput.is_open() && tempInput.is_open())
            {
                const std::string sourceContents((std::istreambuf_iterator<char>(sourceInput)), std::istreambuf_iterator<char>());
                const std::string tempContents((std::istreambuf_iterator<char>(tempInput)), std::istreambuf_iterator<char>());
                statusValue = sourceContents == tempContents ? "Up to Date" : "Modified";
            }
            else
            {
                statusValue = "Compare Failed";
            }
        }
        else
        {
            statusValue = "Compare Failed";
        }

        if (!tempPath.empty())
        {
            std::filesystem::remove(tempPath, ec);
        }

        m_PrefabStatusCacheRootEntity = rootEntity;
        m_PrefabStatusCachePrefabAsset = prefabInstance->prefabAsset;
        m_PrefabStatusCacheSceneDirty = sceneDirty;
        m_PrefabStatusCacheTimeSeconds = m_Time;
        m_PrefabStatusCacheValue = std::move(statusValue);
        return m_PrefabStatusCacheValue;
    }

    std::vector<std::string> TriangleLayer::GetPrefabOverridePaths(const EntityID entity)
    {
        auto& registry = m_Scene.GetRegistry();
        if (!registry.valid(entity))
        {
            return {};
        }

        const auto* prefabInstance = registry.try_get<PrefabInstanceComponent>(entity);
        if (prefabInstance == nullptr)
        {
            return {};
        }

        const bool sceneDirty = IsSceneDirty();
        if (entity == m_PrefabOverrideCacheEntity &&
            prefabInstance->prefabAsset == m_PrefabOverrideCachePrefabAsset &&
            prefabInstance->sourceEntityId == m_PrefabOverrideCacheSourceEntityId &&
            sceneDirty == m_PrefabOverrideCacheSceneDirty &&
            (m_Time - m_PrefabOverrideCacheTimeSeconds) < 0.5f)
        {
            return m_PrefabOverrideCacheValue;
        }

        std::filesystem::path prefabPath(prefabInstance->prefabAsset);
        if (!prefabPath.is_absolute() && Project::IsLoaded())
        {
            prefabPath = (Project::GetProjectRoot() / prefabPath).lexically_normal();
        }

        std::vector<std::string> overridePaths;
        std::error_code ec;
        if (std::filesystem::exists(prefabPath, ec))
        {
            std::ifstream sourceInput(prefabPath, std::ios::binary);
            if (sourceInput.is_open())
            {
                nlohmann::json sourceRoot;
                try
                {
                    sourceInput >> sourceRoot;
                }
                catch (const std::exception&)
                {
                    sourceRoot = nlohmann::json {};
                }

                const std::filesystem::path tempPath =
                    std::filesystem::temp_directory_path(ec) /
                    ("luma_prefab_overrides_" + std::to_string(static_cast<std::uint32_t>(entt::to_integral(entity))) + ".scene");
                std::string error;
                if (!ec && SceneSerializer::Serialize(m_Scene, tempPath, error))
                {
                    std::ifstream currentInput(tempPath, std::ios::binary);
                    if (currentInput.is_open())
                    {
                        nlohmann::json currentRoot;
                        try
                        {
                            currentInput >> currentRoot;
                        }
                        catch (const std::exception&)
                        {
                            currentRoot = nlohmann::json {};
                        }

                        const UUID currentEntityId = registry.get<IDComponent>(entity).id;
                        const nlohmann::json* sourceEntityJson = FindEntityJsonByUuid(sourceRoot, prefabInstance->sourceEntityId);
                        const nlohmann::json* currentEntityJson = FindEntityJsonByUuid(currentRoot, currentEntityId);
                        if (sourceEntityJson != nullptr && currentEntityJson != nullptr)
                        {
                            CollectJsonDifferencePaths(*sourceEntityJson, *currentEntityJson, "", overridePaths);
                            std::sort(overridePaths.begin(), overridePaths.end());
                            overridePaths.erase(std::unique(overridePaths.begin(), overridePaths.end()), overridePaths.end());
                        }
                    }

                    std::filesystem::remove(tempPath, ec);
                }
            }
        }

        m_PrefabOverrideCacheEntity = entity;
        m_PrefabOverrideCachePrefabAsset = prefabInstance->prefabAsset;
        m_PrefabOverrideCacheSourceEntityId = prefabInstance->sourceEntityId;
        m_PrefabOverrideCacheSceneDirty = sceneDirty;
        m_PrefabOverrideCacheTimeSeconds = m_Time;
        m_PrefabOverrideCacheValue = overridePaths;
        return m_PrefabOverrideCacheValue;
    }

    bool TriangleLayer::RevertPrefabComponent(const EntityID entity, const std::string_view componentPath)
    {
        return RevertPrefabOverridePath(entity, componentPath);
    }

    bool TriangleLayer::RevertPrefabOverridePath(const EntityID entity, const std::string_view overridePath)
    {
        if (IsPlayModeActive())
        {
            m_EditorStatus.Content() = "Prefab override revert is unavailable during play mode.";
            return false;
        }

        auto& registry = m_Scene.GetRegistry();
        if (!registry.valid(entity) || !registry.all_of<IDComponent>(entity))
        {
            m_EditorStatus.Content() = "Select a valid prefab instance entity.";
            return false;
        }

        const auto* prefabInstance = registry.try_get<PrefabInstanceComponent>(entity);
        if (prefabInstance == nullptr)
        {
            m_EditorStatus.Content() = "Selected entity is not a prefab instance entity.";
            return false;
        }

        std::filesystem::path prefabPath(prefabInstance->prefabAsset);
        if (!prefabPath.is_absolute() && Project::IsLoaded())
        {
            prefabPath = (Project::GetProjectRoot() / prefabPath).lexically_normal();
        }

        std::ifstream sourceInput(prefabPath, std::ios::binary);
        if (!sourceInput.is_open())
        {
            m_EditorStatus.Content() = "Failed to open prefab source: " + prefabPath.filename().string();
            return false;
        }

        nlohmann::json sourceRoot;
        try
        {
            sourceInput >> sourceRoot;
        }
        catch (const std::exception&)
        {
            m_EditorStatus.Content() = "Failed to parse prefab source: " + prefabPath.filename().string();
            return false;
        }

        std::error_code ec;
        const std::filesystem::path tempScenePath =
            std::filesystem::temp_directory_path(ec) /
            ("luma_prefab_patch_" + std::to_string(static_cast<std::uint32_t>(entt::to_integral(entity))) + ".scene");
        if (ec)
        {
            m_EditorStatus.Content() = "Failed to allocate a temporary scene file for prefab revert.";
            return false;
        }

        std::string error;
        m_Scene.UpdateWorldTransforms();
        if (!SceneSerializer::Serialize(m_Scene, tempScenePath, error))
        {
            m_EditorStatus.Content() = "Failed to snapshot scene for prefab revert: " + error;
            return false;
        }

        std::ifstream currentInput(tempScenePath, std::ios::binary);
        if (!currentInput.is_open())
        {
            std::filesystem::remove(tempScenePath, ec);
            m_EditorStatus.Content() = "Failed to reopen temporary scene snapshot for prefab revert.";
            return false;
        }

        nlohmann::json currentRoot;
        try
        {
            currentInput >> currentRoot;
        }
        catch (const std::exception&)
        {
            std::filesystem::remove(tempScenePath, ec);
            m_EditorStatus.Content() = "Failed to parse temporary scene snapshot for prefab revert.";
            return false;
        }

        const UUID currentEntityId = registry.get<IDComponent>(entity).id;
        const nlohmann::json* sourceEntityJson = FindEntityJsonByUuid(sourceRoot, prefabInstance->sourceEntityId);
        nlohmann::json* currentEntityJson = const_cast<nlohmann::json*>(FindEntityJsonByUuid(currentRoot, currentEntityId));
        if (sourceEntityJson == nullptr || currentEntityJson == nullptr)
        {
            std::filesystem::remove(tempScenePath, ec);
            m_EditorStatus.Content() = "Failed to resolve matching prefab entity data for revert.";
            return false;
        }

        const std::string normalizedPath = StripPrefabOverridePrefix(overridePath);
        const std::vector<std::string> tokens = ParseOverridePathTokens(normalizedPath);
        if (tokens.empty())
        {
            std::filesystem::remove(tempScenePath, ec);
            m_EditorStatus.Content() = "Invalid prefab override path.";
            return false;
        }

        if (tokens.size() == 1)
        {
            const std::string& componentKey = tokens.front();
            const auto sourceIt = sourceEntityJson->find(componentKey);
            if (sourceIt != sourceEntityJson->end())
            {
                (*currentEntityJson)[componentKey] = *sourceIt;
            }
            else
            {
                currentEntityJson->erase(componentKey);
            }
        }
        else
        {
            const nlohmann::json::json_pointer pointer = BuildJsonPointer(tokens);
            if (JsonPointerExists(*sourceEntityJson, pointer))
            {
                (*currentEntityJson)[pointer] = sourceEntityJson->at(pointer);
            }
            else
            {
                EraseJsonPath(*currentEntityJson, tokens);
            }
        }

        {
            std::ofstream output(tempScenePath, std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                std::filesystem::remove(tempScenePath, ec);
                m_EditorStatus.Content() = "Failed to write patched scene snapshot for prefab revert.";
                return false;
            }
            output << currentRoot.dump(2);
        }

        Scene restoredScene;
        if (!SceneSerializer::Deserialize(tempScenePath, restoredScene, error))
        {
            std::filesystem::remove(tempScenePath, ec);
            m_EditorStatus.Content() = "Failed to restore scene after prefab revert: " + error;
            return false;
        }

        const std::vector<UUID> selectedEntityUuids = CaptureSelectedEntityUuids();
        UUID primarySelectedUuid = 0;
        if (m_SelectedEntity != entt::null && registry.valid(m_SelectedEntity) && registry.all_of<IDComponent>(m_SelectedEntity))
        {
            primarySelectedUuid = registry.get<IDComponent>(m_SelectedEntity).id;
        }

        m_Scene.Swap(restoredScene);
        m_Scene.UpdateWorldTransforms();
        RestoreSelectedEntityUuids(selectedEntityUuids, primarySelectedUuid);
        m_PrefabStatusCacheRootEntity = entt::null;
        m_PrefabOverrideCacheEntity = entt::null;
        MarkSceneRenderCacheDirty(Editor::SceneRenderCacheDirtyFlags::All);
        UpdateSceneDirtyState();
        std::filesystem::remove(tempScenePath, ec);

        m_EditorStatus.Content() = "Reverted prefab override: " + normalizedPath;
        return true;
    }

    void TriangleLayer::SelectPrefabAsset(const EntityID entity)
    {
        const EntityID rootEntity = FindPrefabInstanceRoot(entity);
        const auto& registry = m_Scene.GetRegistry();
        const auto* prefabInstance = rootEntity != entt::null ? registry.try_get<PrefabInstanceComponent>(rootEntity) : nullptr;
        if (prefabInstance == nullptr)
        {
            return;
        }

        std::filesystem::path prefabPath(prefabInstance->prefabAsset);
        if (!prefabPath.is_absolute() && Project::IsLoaded())
        {
            prefabPath = (Project::GetProjectRoot() / prefabPath).lexically_normal();
        }

        m_SelectedContentEntry = prefabPath;
        m_EditorStatus.Content() = "Selected prefab asset: " + prefabPath.filename().string();
    }

    void TriangleLayer::DrawViewportPanel()
    {
        EnsureGizmoToolbarIconsLoaded();
        const auto& icons = m_EditorIconService.Icons();
        m_ViewportPanel.Draw({
            &m_ShowViewportPanel,
            icons.viewportPanel,
            &m_ViewportController,
            m_LastRenderer,
            m_SkyboxPreviewTexture,
            m_ShowColliderDebug,
            IsPlayModeActive(),
            m_LastDeltaTimeSeconds,
            [this]()
            {
                if (!IsPlayModeActive())
                {
                    return 60.0f;
                }

                float fovDegrees = 60.0f;
                const auto& registry = m_Scene.GetRegistry();
                if (registry.valid(m_ViewportController.LensSourceEntity()) &&
                    registry.all_of<CameraComponent>(m_ViewportController.LensSourceEntity()))
                {
                    const auto& camera = registry.get<CameraComponent>(m_ViewportController.LensSourceEntity());
                    fovDegrees = camera.fovDegrees;
                }
                return fovDegrees;
            },
            [](const ImVec2& rectMin, const ImVec2& rectMax)
            {
                return IsPopupBlockingViewportRect(rectMin, rectMax);
            },
            [this]()
            {
                m_ViewportAssetDropService.Handle({
                    [this]()
                    {
                        return m_ViewportController.ComputeDropSpawnPosition();
                    },
                    [this](const std::filesystem::path& assetPath, const EntityID parentEntity, const std::array<float, 3>* worldPosition)
                    {
                        return CreateEntityFromMeshAsset(assetPath, parentEntity, worldPosition);
                    },
                    [](const std::filesystem::path& assetPath)
                    {
                        return IsMeshAssetPathCandidate(assetPath);
                    },
                    [this](std::string status)
                    {
                        m_EditorStatus.Content() = std::move(status);
                    }
                });
            },
            [this](ImDrawList* drawList, const ImVec2& origin)
            {
                EnsureGizmoToolbarIconsLoaded();
                const auto& icons = m_EditorIconService.Icons();
                m_ViewportToolbarPanel.Draw({
                    &m_ViewportController,
                    drawList,
                    origin,
                    icons.gizmoSelect,
                    icons.gizmoTranslate,
                    icons.gizmoRotate,
                    icons.gizmoScale,
                    icons.gizmoSnap,
                    icons.gizmoGrid
                });
            },
            [this](ImDrawList* drawList, const ImVec2& origin, const ImVec2& renderAreaSize, const EntityID lensSourceEntity)
            {
                m_ViewportDebugOverlay.Draw({
                    &m_Scene,
                    &m_SelectionState,
                    &m_ViewportController.Camera(),
                    drawList,
                    origin,
                    renderAreaSize,
                    lensSourceEntity
                });
            },
            [this](
                ImDrawList* drawList,
                const ImVec2& rectMin,
                const ImVec2& rectMax,
                const ImVec2& renderAreaSize,
                const bool viewportInputBlockedByPopup,
                const EntityID lensSourceEntity)
            {
                m_ViewportInteraction.Handle({
                    &m_Scene,
                    &m_ViewportController,
                    &m_SelectionState,
                    &m_ViewportController.Camera(),
                    drawList,
                    rectMin,
                    rectMax,
                    renderAreaSize,
                    viewportInputBlockedByPopup,
                    true,
                    lensSourceEntity,
                    [this]()
                    {
                        m_SelectedContentEntry.clear();
                    }
                });
            }
        });
    }

    void TriangleLayer::DrawContentBrowserPanel()
    {
        Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
        m_ContentBrowserHostFacadeService.Draw(contentBrowserContext);
    }

    EntityID TriangleLayer::FindEditorCameraEntity() const
    {
        return Editor::FindEditorCameraEntity(m_Scene, m_SelectedEntity);
    }

    std::uint32_t TriangleLayer::ComputeRequestedMeshLod(
        const TransformComponent& transform,
        const MeshRendererComponent& meshRenderer) const
    {
        Editor::MeshStreamingGeometryContext context {};
        context.cameraPosition = m_ViewportController.Camera().position;
        return m_MeshStreamingGeometryService.ComputeRequestedMeshLod(context, transform, meshRenderer);
    }

    const PrimitiveMeshData* TriangleLayer::ResolveMeshRendererGeometry(
        const TransformComponent& transform,
        const MeshRendererComponent& meshRenderer)
    {
        Editor::MeshStreamingGeometryContext context {};
        context.primitiveMeshLod = m_PrimitiveMeshLod;
        context.cameraPosition = m_ViewportController.Camera().position;
        context.projectLoaded = Project::IsLoaded();
        context.projectAssetsPath = Project::GetAssetsPath();
        context.projectRoot = Project::GetProjectRoot();
        context.streamingService = &m_ResourceStreamingService;
        context.importedSceneParts = &m_ImportedSceneParts;
        context.streamedMeshAssets = &m_StreamedMeshAssets;
        context.logImportError =
            [this](const std::string_view message)
            {
                AddConsoleLine(LogLevel::Error, "Import", message, message);
            };
        context.logStreamingError =
            [this](const std::string_view message)
            {
                AddConsoleLine(LogLevel::Error, "Streaming", message, message);
            };
        context.logStreamingWarn =
            [this](const std::string_view message)
            {
                AddConsoleLine(LogLevel::Warn, "Streaming", message, message);
            };
        return m_MeshStreamingGeometryService.ResolveMeshRendererGeometry(context, transform, meshRenderer);
    }

    void TriangleLayer::RebuildScenePrimitiveMesh()
    {
        const Editor::SceneRenderCacheDirtyFlags dirtyFlags = m_RenderSceneCacheDirtyFlags;
        const bool geometryDirty =
            Editor::HasAnySceneRenderCacheDirtyFlags(dirtyFlags, Editor::SceneRenderCacheDirtyFlags::Geometry);
        const bool materialsDirty =
            Editor::HasAnySceneRenderCacheDirtyFlags(dirtyFlags, Editor::SceneRenderCacheDirtyFlags::Materials);
        const bool environmentDirty =
            Editor::HasAnySceneRenderCacheDirtyFlags(dirtyFlags, Editor::SceneRenderCacheDirtyFlags::Environment);

        Editor::SceneRenderCacheStateContext cacheStateContext = BuildSceneRenderCacheStateContext();
        const std::string currentSkyMeshSignature = m_SceneRenderCacheStateService.BuildActiveSkyMeshSignature(cacheStateContext);
        const bool viewportGridChanged = m_ViewportController.ShowGrid() != m_LastViewportGridEnabled;
        const bool sceneGridMissing =
            m_ViewportController.ShowGrid() ? !m_HasScenePrimitiveMesh : m_HasScenePrimitiveMesh;
        const bool skyMeshChanged = currentSkyMeshSignature != m_LastSkyMeshSignature;
        const bool skyMeshMissing =
            currentSkyMeshSignature.empty() ? m_HasSkyPrimitiveMesh : !m_HasSkyPrimitiveMesh;
        const bool buildScenePrimitiveMesh = viewportGridChanged || sceneGridMissing;
        const bool buildSkyPrimitiveMesh = environmentDirty || skyMeshChanged || skyMeshMissing;
        const bool collectRenderSources = geometryDirty || materialsDirty;

        auto& registry = m_Scene.GetRegistry();
        Editor::SceneRenderCacheBuildContext buildContext {};
        buildContext.scene = &m_Scene;
        buildContext.showGrid = m_ViewportController.ShowGrid();
        buildContext.skyEnvironmentLinearPixels = &m_SkyEnvironmentLinearPixels;
        buildContext.skyEnvironmentWidth = m_SkyEnvironmentWidth;
        buildContext.skyEnvironmentHeight = m_SkyEnvironmentHeight;
        buildContext.streamedMeshAssets = &m_StreamedMeshAssets;
        buildContext.collectRenderSources = collectRenderSources;
        buildContext.buildScenePrimitiveMesh = buildScenePrimitiveMesh;
        buildContext.buildSkyPrimitiveMesh = buildSkyPrimitiveMesh;
        buildContext.findPrimarySkyEntity =
            [this]()
            {
                return FindPrimarySkyEntity();
            };
        buildContext.resolveImportedSceneParts =
            [this](const std::string& sourcePath) -> ImportedScenePartsState*
            {
                Editor::MeshStreamingGeometryContext context {};
                context.projectLoaded = Project::IsLoaded();
                context.projectAssetsPath = Project::GetAssetsPath();
                context.projectRoot = Project::GetProjectRoot();
                context.importedSceneParts = &m_ImportedSceneParts;
                context.logImportError =
                    [this](const std::string_view message)
                    {
                        AddConsoleLine(LogLevel::Error, "Import", message, message);
                    };
                return m_MeshStreamingGeometryService.ResolveImportedSceneParts(context, sourcePath);
            };
        buildContext.resolveMeshRendererGeometry =
            [this](const TransformComponent& transform, const MeshRendererComponent& meshRenderer)
            {
                return ResolveMeshRendererGeometry(transform, meshRenderer);
            };
        buildContext.computeRequestedMeshLod =
            [this](const TransformComponent& transform, const MeshRendererComponent& meshRenderer)
            {
                return ComputeRequestedMeshLod(transform, meshRenderer);
            };
        buildContext.computeSkyColor =
            [this](const std::array<float, 3>& direction, const SkyLightComponent& skyLight, const bool hasEnvironment)
            {
                const Vec3 linearSkyColor = ComputeSkyColorLikeLuma(
                    { direction[0], direction[1], direction[2] },
                    skyLight,
                    hasEnvironment,
                    m_SkyEnvironmentLinearPixels,
                    m_SkyEnvironmentWidth,
                    m_SkyEnvironmentHeight);
                return std::array<float, 3> { linearSkyColor.x, linearSkyColor.y, linearSkyColor.z };
            };

        Editor::SceneRenderCacheBuildResult buildResult = m_SceneRenderCacheBuilder.Build(buildContext);
        using PendingSceneRenderSource = Editor::PendingSceneRenderSource;
        auto& vertices = buildResult.vertices;
        auto& indices = buildResult.indices;
        auto& skyVertices = buildResult.skyVertices;
        auto& skyIndices = buildResult.skyIndices;
        auto& pendingRenderSources = buildResult.pendingRenderSources;
        auto& indexOverflow = buildResult.indexOverflow;
        auto& skyIndexOverflow = buildResult.skyIndexOverflow;
        auto& renderItemsStateHash = buildResult.renderItemsStateHash;
        std::size_t cameraRenderItemCount = 0;
        bool hasCameraActorMeshes = false;
        if (!IsPlayModeActive())
        {
            std::string cameraActorMeshError;
            hasCameraActorMeshes = EnsureCameraActorMeshLoaded(m_CameraActorMeshState, cameraActorMeshError);
            if (hasCameraActorMeshes)
            {
                const auto cameraView = registry.view<TransformComponent, CameraComponent>();
                for (const EntityID entity : cameraView)
                {
                    if (registry.all_of<EditorRuntimeOnlyComponent>(entity))
                    {
                        continue;
                    }

                    const auto& transform = cameraView.get<TransformComponent>(entity);
                    renderItemsStateHash = HashBytes(&entity, sizeof(entity), renderItemsStateHash);
                    renderItemsStateHash =
                        HashBytes(transform.worldPosition.data(), sizeof(transform.worldPosition), renderItemsStateHash);
                    renderItemsStateHash =
                        HashBytes(transform.worldRotation.data(), sizeof(transform.worldRotation), renderItemsStateHash);
                    renderItemsStateHash =
                        HashBytes(transform.worldScale.data(), sizeof(transform.worldScale), renderItemsStateHash);
                    cameraRenderItemCount += m_CameraActorMeshState.meshes.size();
                }
            }
        }
        if (buildSkyPrimitiveMesh && (skyIndexOverflow || skyVertices.empty() || skyIndices.empty()))
        {
            if (m_HasSkyPrimitiveMesh || m_SkyPrimitiveMeshHash != 0 || !m_SkyPrimitiveMeshDesc.vertexData.empty())
            {
                m_HasSkyPrimitiveMesh = false;
                m_SkyPrimitiveMeshHash = 0;
                m_SkyPrimitiveMeshDesc = {};
                ++m_SkyPrimitiveMeshRevision;
            }
        }
        else if (buildSkyPrimitiveMesh)
        {
            std::uint64_t skyHash = 1469598103934665603ull;
            skyHash = HashBytes(skyVertices.data(), skyVertices.size() * sizeof(PrimitiveVertex), skyHash);
            skyHash = HashBytes(skyIndices.data(), skyIndices.size() * sizeof(std::uint32_t), skyHash);

            if (!m_HasSkyPrimitiveMesh || skyHash != m_SkyPrimitiveMeshHash)
            {
                PrimitiveMeshData skyMeshData;
                skyMeshData.vertices = std::move(skyVertices);
                skyMeshData.indices = std::move(skyIndices);

                m_SkyPrimitiveMeshDesc = PrimitiveMeshFactory::BuildMeshDesc(skyMeshData);
                m_SkyPrimitiveMeshHash = skyHash;
                m_HasSkyPrimitiveMesh = true;
                ++m_SkyPrimitiveMeshRevision;
            }
        }

        if (buildScenePrimitiveMesh && (indexOverflow || vertices.empty() || indices.empty()))
        {
            if (m_HasScenePrimitiveMesh || m_ScenePrimitiveMeshHash != 0 || !m_ScenePrimitiveMeshDesc.vertexData.empty())
            {
                m_HasScenePrimitiveMesh = false;
                m_ScenePrimitiveMeshHash = 0;
                m_ScenePrimitiveMeshDesc = {};
                ++m_ScenePrimitiveMeshRevision;
            }
        }
        else if (buildScenePrimitiveMesh)
        {
            std::uint64_t hash = 1469598103934665603ull;
            hash = HashBytes(vertices.data(), vertices.size() * sizeof(PrimitiveVertex), hash);
            hash = HashBytes(indices.data(), indices.size() * sizeof(std::uint32_t), hash);

            if (!m_HasScenePrimitiveMesh || hash != m_ScenePrimitiveMeshHash)
            {
                PrimitiveMeshData sceneMeshData;
                sceneMeshData.vertices = std::move(vertices);
                sceneMeshData.indices = std::move(indices);

                m_ScenePrimitiveMeshDesc = PrimitiveMeshFactory::BuildMeshDesc(sceneMeshData);
                m_ScenePrimitiveMeshHash = hash;
                m_HasScenePrimitiveMesh = true;
                ++m_ScenePrimitiveMeshRevision;
            }
        }

        if (!collectRenderSources)
        {
            m_SceneRenderCacheStateService.FinalizeBuild(cacheStateContext);
            return;
        }

        const std::size_t expectedRenderItemCount = pendingRenderSources.size() + cameraRenderItemCount;
        const bool rebuildRenderItems =
            materialsDirty ||
            expectedRenderItemCount != m_SceneRenderItems.size() ||
            renderItemsStateHash != m_SceneRenderItemsHash;

        if (expectedRenderItemCount == 0)
        {
            if (!m_SceneRenderItems.empty() || m_SceneRenderItemsHash != 0)
            {
                m_SceneRenderItems.clear();
                m_SceneRenderItemsHash = 0;
                ++m_SceneRenderItemsRevision;
            }
            m_SceneRenderCacheStateService.FinalizeBuild(cacheStateContext);
            return;
        }

        if (!rebuildRenderItems)
        {
            m_SceneRenderCacheStateService.FinalizeBuild(cacheStateContext);
            return;
        }

        Editor::SceneRenderItemAssemblyContext assemblyContext {};
        assemblyContext.pendingRenderSources = &pendingRenderSources;
        assemblyContext.renderItemsStateHash = renderItemsStateHash;
        assemblyContext.buildImportedMaterialProxy =
            [this](const Assets::MeshMaterialInfo& sourceMaterial, const std::filesystem::path& sourceMaterialPath)
            {
                return BuildImportedMaterialRenderProxy(sourceMaterial, sourceMaterialPath);
            };
        assemblyContext.tryBuildSlotMaterialOverride =
            [this](const MeshRendererComponent& meshRenderer, const std::size_t materialSlotIndex, MaterialRenderProxy& outProxy)
            {
                const std::string* slotOverridePath =
                    ResolveMeshRendererMaterialOverride(meshRenderer, materialSlotIndex);
                if (slotOverridePath == nullptr || slotOverridePath->empty())
                {
                    return false;
                }

                const std::filesystem::path assetPath = ResolveSkyAssetPath(*slotOverridePath);
                return m_MaterialRenderProxyCacheService.TryGetProxy(
                    {
                        [](MaterialComponent& material)
                        {
                            InitializeDefaultMaterialComponent(material);
                        },
                        [this](const std::filesystem::path& path, MaterialComponent& material, std::string& outError) -> bool
                        {
                            return LoadMaterialComponentFromJsonAsset(path, material, outError);
                        },
                        [](const MaterialComponent& material, const std::filesystem::path& path) -> MaterialRenderProxy
                        {
                            return BuildMaterialRenderProxyFromComponent(material, path);
                        }
                    },
                    assetPath,
                    outProxy);
            };
        assemblyContext.tryBuildEntityMaterialOverride =
            [this](const EntityID entity, const Assets::MeshMaterialInfo* sourceMaterial, MaterialRenderProxy& outProxy)
            {
                const MaterialComponent* materialComponent = m_Scene.GetRegistry().try_get<MaterialComponent>(entity);
                if (materialComponent == nullptr)
                {
                    return false;
                }

                MaterialComponent defaultMaterial {};
                InitializeDefaultMaterialComponent(defaultMaterial);
                const bool shouldOverrideSourceMaterial =
                    sourceMaterial == nullptr ||
                    !MaterialPropertiesEqual(*materialComponent, defaultMaterial) ||
                    (!TrimCopy(materialComponent->sharedMaterial).empty() &&
                     materialComponent->sharedMaterial != std::string(kDefaultGridMaterialAsset));
                if (!shouldOverrideSourceMaterial)
                {
                    return false;
                }

                outProxy = BuildMaterialRenderProxyFromComponent(
                    *materialComponent,
                    ResolveSkyAssetPath(materialComponent->sharedMaterial));
                return true;
            };

        m_SceneRenderItemAssemblyService.BuildInto(
            assemblyContext,
            m_SceneRenderItemAssemblyScratch,
            m_SceneRenderItems);

        if (hasCameraActorMeshes)
        {
            const auto cameraView = registry.view<TransformComponent, CameraComponent>();
            MaterialRenderProxy cameraActorMaterial = BuildCameraActorMaterial();
            cameraActorMaterial.sourcePath = m_CameraActorMeshState.resolvedPath;
            for (const EntityID entity : cameraView)
            {
                if (registry.all_of<EditorRuntimeOnlyComponent>(entity))
                {
                    continue;
                }

                const auto& transform = cameraView.get<TransformComponent>(entity);
                std::array<float, 3> markerRotation = transform.worldRotation;
                markerRotation[1] -= 90.0f;
                std::array<float, 3> markerScale = transform.worldScale;
                markerScale[0] *= 0.18f;
                markerScale[1] *= 0.18f;
                markerScale[2] *= 0.18f;
                const auto worldTransform =
                    BuildTransformMatrix(transform.worldPosition, markerRotation, markerScale).elements;

                for (std::size_t partIndex = 0; partIndex < m_CameraActorMeshState.meshes.size(); ++partIndex)
                {
                    SceneRenderItem renderItem;
                    renderItem.key = "editor.camera:" + std::to_string(static_cast<std::uint32_t>(entity)) + ":" +
                        std::to_string(partIndex);
                    renderItem.meshKey = m_CameraActorMeshState.resolvedPath.generic_string() + "#part:" +
                        std::to_string(partIndex);
                    renderItem.mesh = m_CameraActorMeshState.meshes[partIndex];
                    renderItem.revision = renderItemsStateHash == 0 ? 1 : renderItemsStateHash;
                    renderItem.meshRevision = 1;
                    renderItem.worldPosition = transform.worldPosition;
                    renderItem.worldTransform = worldTransform;
                    renderItem.material = BuildImportedMaterialRenderProxy(
                        m_CameraActorMeshState.parts[partIndex].material,
                        m_CameraActorMeshState.resolvedPath);
                    if (renderItem.material.name.empty())
                    {
                        renderItem.material = cameraActorMaterial;
                    }
                    m_SceneRenderItems.push_back(std::move(renderItem));
                }
            }
        }

        m_SceneRenderItemsHash = renderItemsStateHash;
        ++m_SceneRenderItemsRevision;

        m_SceneRenderCacheStateService.FinalizeBuild(cacheStateContext);
    }

    void TriangleLayer::HandleStreamingEvent(const Assets::StreamEvent& event)
    {
        const bool affectsSceneRenderCache =
            m_MeshStreamingGeometryService.InvalidateFromStreamingEvent(event, m_StreamedMeshAssets);

        switch (event.type)
        {
        case Assets::StreamEventType::Queued:
        case Assets::StreamEventType::Started:
        case Assets::StreamEventType::Retargeted:
            if (event.handle != 0)
            {
                m_StreamingActiveHandles.insert(event.handle);
                m_StreamingTaskBatchSize = std::max<std::uint32_t>(
                    m_StreamingTaskBatchSize,
                    static_cast<std::uint32_t>(m_StreamingActiveHandles.size()));
            }
            break;
        case Assets::StreamEventType::Completed:
        case Assets::StreamEventType::Failed:
        case Assets::StreamEventType::Cancelled:
        case Assets::StreamEventType::Released:
            if (event.handle != 0)
            {
                m_StreamingActiveHandles.erase(event.handle);
            }
            break;
        case Assets::StreamEventType::Evicted:
        case Assets::StreamEventType::BudgetUpdated:
            break;
        }

        if (!event.message.empty())
        {
            m_StreamingTaskLastMessage = event.message;
        }

        if (event.type == Assets::StreamEventType::Failed)
        {
            AddConsoleLine(LogLevel::Error, "Streaming", event.message, event.message);
        }
        else if (event.type == Assets::StreamEventType::Evicted)
        {
            AddConsoleLine(LogLevel::Warn, "Streaming", event.message, event.message);
        }

        if (affectsSceneRenderCache)
        {
            MarkSceneRenderCacheDirty(Editor::SceneRenderCacheDirtyFlags::Geometry);
        }
    }

    void TriangleLayer::UpdateStreamingTaskState()
    {
        const Assets::StreamingStats stats = m_ResourceStreamingService.GetStats();
        const std::uint32_t pendingCount = stats.queuedCount + stats.inFlightCount;
        const std::uint32_t activeCount = pendingCount;

        if (activeCount == 0)
        {
            if (m_StreamingTaskActive && m_StreamingTask != 0)
            {
                EditorTaskManager::SetSubtask(m_StreamingTask, "Streaming queue is idle.");
                EditorTaskManager::SetProgress(m_StreamingTask, 1.0f);
                EditorTaskManager::EndTask(m_StreamingTask);
            }
            m_StreamingTask = 0;
            m_StreamingTaskActive = false;
            m_StreamingTaskBatchSize = 0;
            m_StreamingTaskLastMessage.clear();
            m_StreamingActiveHandles.clear();
            return;
        }

        // A single background request is expected while streamed scene content remains live.
        // Do not keep a persistent HUD card open for that steady-state case.
        if (stats.queuedCount == 0 && stats.inFlightCount <= 1)
        {
            if (m_StreamingTaskActive && m_StreamingTask != 0)
            {
                EditorTaskManager::SetSubtask(m_StreamingTask, "Background streaming continuing.");
                EditorTaskManager::SetProgress(m_StreamingTask, 1.0f);
                EditorTaskManager::EndTask(m_StreamingTask);
            }
            m_StreamingTask = 0;
            m_StreamingTaskActive = false;
            m_StreamingTaskBatchSize = 0;
            return;
        }

        m_StreamingTaskBatchSize = std::max(m_StreamingTaskBatchSize, activeCount);
        if (!m_StreamingTaskActive)
        {
            EditorTaskDesc taskDesc;
            taskDesc.title = "Streaming Resources...";
            taskDesc.subtask = "Preparing streamed assets.";
            taskDesc.blocking = false;
            taskDesc.cancellable = false;
            m_StreamingTask = EditorTaskManager::BeginTask(taskDesc);
            m_StreamingTaskActive = (m_StreamingTask != 0);
        }

        if (m_StreamingTask == 0)
        {
            return;
        }

        const float progress = std::clamp(
            1.0f - (static_cast<float>(activeCount) / static_cast<float>(std::max(1u, m_StreamingTaskBatchSize))),
            0.01f,
            0.99f);

        std::string subtask = m_StreamingTaskLastMessage;
        if (subtask.empty())
        {
            subtask = "Streaming assets in the background.";
        }
        subtask += "  Queued: " + std::to_string(stats.queuedCount);
        subtask += " | In Flight: " + std::to_string(stats.inFlightCount);
        subtask += " | Resident: " + std::to_string(stats.residentCount);

        EditorTaskManager::SetSubtask(m_StreamingTask, subtask);
        EditorTaskManager::SetProgress(m_StreamingTask, progress);
    }

    EntityID TriangleLayer::FindPrimarySkyEntity() const
    {
        return Editor::FindPrimarySkyEntity(m_Scene);
    }

    void TriangleLayer::BuildBlendedPostProcessView(
        const std::array<float, 3>& cameraWorldPosition,
        ScenePostProcessView& outPostProcess) const
    {
        m_PostProcessBlendService.BuildBlendedView(m_Scene, cameraWorldPosition, outPostProcess);
    }

    std::filesystem::path TriangleLayer::ResolveSkyAssetPath(const std::string& path) const
    {
        if (path.empty())
        {
            return {};
        }

        std::filesystem::path inputPath(path);
        std::error_code ec;
        if (inputPath.is_absolute())
        {
            const std::filesystem::path absolute = std::filesystem::weakly_canonical(inputPath, ec);
            return ec ? inputPath.lexically_normal() : absolute;
        }

        for (const Editor::ContentBrowserRootState& root : m_ContentRoots)
        {
            if (root.path.empty())
            {
                continue;
            }

            const std::filesystem::path candidate = root.path / inputPath;
            if (std::filesystem::exists(candidate, ec) && !ec)
            {
                const std::filesystem::path resolved = std::filesystem::weakly_canonical(candidate, ec);
                return ec ? candidate.lexically_normal() : resolved;
            }
        }

        return m_SkyEnvironmentService.ResolveAssetPath(
            path,
            Project::IsLoaded(),
            Project::GetProjectRoot(),
            Project::GetAssetsPath());
    }

    Editor::SceneRenderCacheStateContext TriangleLayer::BuildSceneRenderCacheStateContext() const
    {
        Editor::SceneRenderCacheStateContext context {};
        context.scene = &m_Scene;
        context.viewportGridEnabled = m_ViewportController.ShowGrid();
        context.skyboxSourcePath = &m_SkyboxSourcePath;
        context.renderSceneCacheDirtyFlags =
            const_cast<Editor::SceneRenderCacheDirtyFlags*>(&m_RenderSceneCacheDirtyFlags);
        context.lastViewportGridEnabled = const_cast<bool*>(&m_LastViewportGridEnabled);
        context.lastSkyMeshSignature = const_cast<std::string*>(&m_LastSkyMeshSignature);
        context.findPrimarySkyEntity = [this]()
        {
            return FindPrimarySkyEntity();
        };
        return context;
    }

    Editor::SkyPreviewTextureHostContext TriangleLayer::BuildSkyPreviewTextureHostContext()
    {
        Editor::SkyPreviewTextureHostContext context {};
        context.renderer = m_LastRenderer;
        context.scene = &m_Scene;
        context.skyEnvironmentService = &m_SkyEnvironmentService;
        context.projectLoaded = Project::IsLoaded();
        context.projectRoot = Project::IsLoaded() ? Project::GetProjectRoot() : std::filesystem::path {};
        context.assetsPath = Project::IsLoaded() ? Project::GetAssetsPath() : std::filesystem::path {};
        context.cachePath = Project::IsLoaded() ? Project::GetCachePath() : std::filesystem::path {};
        context.skyStatus = &m_EditorStatus.Sky();
        context.skyboxPreviewTexture = &m_SkyboxPreviewTexture;
        context.skyboxPreviewWidth = &m_SkyboxPreviewWidth;
        context.skyboxPreviewHeight = &m_SkyboxPreviewHeight;
        context.skyboxSourcePath = &m_SkyboxSourcePath;
        context.skyboxSignature = &m_SkyboxSignature;
        context.skyAverageColor = &m_SkyAverageColor;
        context.skyEnvironmentLinearPixels = &m_SkyEnvironmentLinearPixels;
        context.skyEnvironmentWidth = &m_SkyEnvironmentWidth;
        context.skyEnvironmentHeight = &m_SkyEnvironmentHeight;
        context.findPrimarySkyEntity = [this]()
        {
            return FindPrimarySkyEntity();
        };
        context.resolveAssetPath = [this](const std::string& assetPath)
        {
            return ResolveSkyAssetPath(assetPath);
        };
        context.evaluateSkyColor = [this](const Editor::SkyColorEvalContext& evalContext)
        {
            static const std::vector<float> kEmptyPixels;
            const std::vector<float>& linearPixels =
                evalContext.linearPixels != nullptr ? *evalContext.linearPixels : kEmptyPixels;
            const Vec3 skyColor = ComputeSkyColorLikeLuma(
                { evalContext.direction[0], evalContext.direction[1], evalContext.direction[2] },
                *evalContext.skyLight,
                evalContext.hasEnvironment,
                linearPixels,
                evalContext.sourceWidth,
                evalContext.sourceHeight);
            return std::array<float, 3> { skyColor.x, skyColor.y, skyColor.z };
        };
        return context;
    }

    bool TriangleLayer::LoadSceneFromPath(const std::filesystem::path& scenePath)
    {
        Editor::SceneDocumentHostContext context {};
        context.scene = &m_Scene;
        context.sceneDocument = &m_SceneDocument;
        context.contentStatus = &m_EditorStatus.Content();
        context.selectedContentEntry = &m_SelectedContentEntry;
        context.afterLoad = [this]()
        {
            m_SceneEntityUtilityService.ClearEntitySelection(m_SelectionState);
            m_ViewportController.ResetCameraToDefault();

            const auto roots = m_Scene.GetRootEntities();
            if (!roots.empty())
            {
                Editor::SceneEntitySelectionContext selectionContext { &m_Scene, &m_SelectionState, &m_SelectedContentEntry };
                m_SceneEntityUtilityService.SelectSingleEntity(selectionContext, roots.front());
            }
        };

        const bool loaded = m_SceneDocumentHostService.LoadSceneFromPath(context, scenePath);
        if (loaded)
        {
            MarkSceneRenderCacheDirty(Editor::SceneRenderCacheDirtyFlags::All);
        }
        return loaded;
    }

    bool TriangleLayer::SaveSceneToPath(const std::filesystem::path& scenePath)
    {
        Editor::SceneDocumentHostContext context {};
        context.scene = &m_Scene;
        context.sceneDocument = &m_SceneDocument;
        context.contentStatus = &m_EditorStatus.Content();
        context.selectedContentEntry = &m_SelectedContentEntry;
        context.beforeSave = [this]()
        {
            m_Scene.UpdateWorldTransforms();
        };
        context.afterSave = [this]()
        {
            Editor::ContentBrowserHostFacadeContext contentBrowserContext = BuildContentBrowserHostFacadeContext();
            m_ContentBrowserHostFacadeService.InvalidateFolderTreeCache(contentBrowserContext);
            m_ContentBrowserHostFacadeService.RefreshContentEntries(contentBrowserContext);
        };

        return m_SceneDocumentHostService.SaveSceneToPath(context, scenePath);
    }

    bool TriangleLayer::SetProjectStartScene(const std::filesystem::path& scenePath)
    {
        return m_SceneFileService.SetProjectStartScene(
            scenePath,
            m_EditorStatus.ProjectConfig(),
            [this]()
            {
                m_ProjectSettingsPanel.InvalidateDraft();
            });
    }

    bool TriangleLayer::CaptureSceneSnapshot(std::string& outSnapshot, std::string& outError) const
    {
        Editor::SceneDocumentHostContext context {};
        context.scene = const_cast<Scene*>(&m_Scene);
        context.sceneDocument = const_cast<Editor::SceneDocument*>(&m_SceneDocument);
        return m_SceneDocumentHostService.CaptureSceneSnapshot(context, outSnapshot, outError);
    }

    bool TriangleLayer::IsSceneDirty()
    {
        Editor::SceneDocumentHostContext context {};
        context.scene = &m_Scene;
        context.sceneDocument = &m_SceneDocument;
        return m_SceneDocumentHostService.IsSceneDirty(context);
    }

    void TriangleLayer::UpdateSceneDirtyState()
    {
        Editor::SceneDocumentHostContext context {};
        context.scene = &m_Scene;
        context.sceneDocument = &m_SceneDocument;
        m_SceneDocumentHostService.UpdateSceneDirtyState(context);
    }

    void TriangleLayer::CreateNewScene()
    {
        Editor::SceneDocumentHostContext context {};
        context.scene = &m_Scene;
        context.sceneDocument = &m_SceneDocument;
        context.contentStatus = &m_EditorStatus.Content();
        context.selectedContentEntry = &m_SelectedContentEntry;
        context.afterNewScene = [this]()
        {
            m_SelectedContentEntry.clear();
            SeedDefaultSceneEntities();
        };

        m_SceneDocumentHostService.CreateNewScene(context);
    }

    void TriangleLayer::RequestPendingSceneActionClose()
    {
        Editor::SceneActionHostContext context {};
        context.actionService = &m_SceneActionService;
        m_SceneActionHostService.RequestPendingClose(context);
    }

    void TriangleLayer::RequestNewScene()
    {
        if (IsPlayModeActive() && !StopPlayMode())
        {
            return;
        }

        Editor::SceneActionHostContext context {};
        context.actionService = &m_SceneActionService;
        context.isSceneDirty = [this]() -> bool
        {
            return IsSceneDirty();
        };
        context.createNewScene = [this]()
        {
            CreateNewScene();
        };
        m_SceneActionHostService.RequestNewScene(context);
    }

    void TriangleLayer::RequestLoadScene(const std::filesystem::path& scenePath)
    {
        if (IsPlayModeActive() && !StopPlayMode())
        {
            return;
        }

        Editor::SceneActionHostContext context {};
        context.actionService = &m_SceneActionService;
        context.isSceneDirty = [this]() -> bool
        {
            return IsSceneDirty();
        };
        context.loadScene = [this](const std::filesystem::path& requestedScenePath)
        {
            LoadSceneFromPath(requestedScenePath);
        };
        m_SceneActionHostService.RequestLoadScene(context, scenePath);
    }

    void TriangleLayer::RequestReloadScene()
    {
        if (IsPlayModeActive() && !StopPlayMode())
        {
            return;
        }

        Editor::SceneActionHostContext context {};
        context.actionService = &m_SceneActionService;
        context.sceneDocument = &m_SceneDocument;
        context.isSceneDirty = [this]() -> bool
        {
            return IsSceneDirty();
        };
        context.loadScene = [this](const std::filesystem::path& requestedScenePath)
        {
            LoadSceneFromPath(requestedScenePath);
        };
        m_SceneActionHostService.RequestReloadScene(context);
    }

    bool TriangleLayer::SaveActiveScene()
    {
        if (IsPlayModeActive() && !StopPlayMode())
        {
            return false;
        }

        Editor::SceneActionHostContext context {};
        context.sceneFileService = &m_SceneFileService;
        context.sceneDocument = &m_SceneDocument;
        context.contentStatus = &m_EditorStatus.Content();
        context.saveSceneToPath = [this](const std::filesystem::path& scenePath) -> bool
        {
            return SaveSceneToPath(scenePath);
        };
        return m_SceneActionHostService.SaveActiveScene(context);
    }

    bool TriangleLayer::OpenSaveSceneAsPrompt(std::string_view suggestedName)
    {
        if (IsPlayModeActive() && !StopPlayMode())
        {
            return false;
        }

        Editor::SceneActionHostContext context {};
        context.sceneFileService = &m_SceneFileService;
        context.sceneDocument = &m_SceneDocument;
        context.contentStatus = &m_EditorStatus.Content();
        context.saveSceneToPath = [this](const std::filesystem::path& scenePath) -> bool
        {
            return SaveSceneToPath(scenePath);
        };
        return m_SceneActionHostService.OpenSaveSceneAsPrompt(context, suggestedName);
    }

    void TriangleLayer::DrawUnsavedScenePrompt()
    {
        Editor::SceneActionHostContext context {};
        context.actionService = &m_SceneActionService;
        context.sceneFileService = &m_SceneFileService;
        context.sceneDocument = &m_SceneDocument;
        context.contentStatus = &m_EditorStatus.Content();
        context.createNewScene = [this]()
        {
            CreateNewScene();
        };
        context.loadScene = [this](const std::filesystem::path& scenePath)
        {
            LoadSceneFromPath(scenePath);
        };
        context.saveSceneToPath = [this](const std::filesystem::path& scenePath) -> bool
        {
            return SaveSceneToPath(scenePath);
        };
        m_SceneActionHostService.DrawUnsavedScenePrompt(context);
    }

    std::filesystem::path TriangleLayer::ResolveScenePath(const std::string& scenePath) const
    {
        return m_SceneFileService.ResolveScenePath(m_SceneDocument, scenePath);
    }

    std::filesystem::path TriangleLayer::GetDefaultScenePath() const
    {
        return m_SceneFileService.GetDefaultScenePath(m_SceneDocument);
    }

    std::filesystem::path TriangleLayer::BuildScenePathFromName(const std::string_view sceneName) const
    {
        return m_SceneFileService.BuildScenePathFromName(m_SceneDocument, sceneName);
    }

    std::string TriangleLayer::GetActiveSceneDisplayName() const
    {
        return m_SceneFileService.GetActiveSceneDisplayName(m_SceneDocument);
    }

    void TriangleLayer::RefreshWindowTitle()
    {
        Editor::SceneDocumentHostContext context {};
        context.sceneDocument = &m_SceneDocument;
        m_SceneDocumentHostService.RefreshWindowTitle(context);
    }

    void TriangleLayer::SeedDefaultSceneEntities()
    {
        Editor::SceneBootstrapContext context {};
        context.scene = &m_Scene;
        context.selectedEntity = m_SelectedEntity;
        context.isEntitySelected = [this](const EntityID entity) -> bool
        {
            return m_SceneEntityUtilityService.IsEntitySelected(m_SelectionState, entity);
        };
        context.clearEntitySelection = [this]()
        {
            m_SceneEntityUtilityService.ClearEntitySelection(m_SelectionState);
        };
        context.selectSingleEntity = [this](const EntityID entity)
        {
            Editor::SceneEntitySelectionContext selectionContext { &m_Scene, &m_SelectionState, &m_SelectedContentEntry };
            m_SceneEntityUtilityService.SelectSingleEntity(selectionContext, entity);
        };
        context.initializeSkyLightDefaults = [this](SkyLightComponent& skyLight)
        {
            m_SceneEntityUtilityService.InitializeSkyLightDefaults(skyLight);
        };
        context.resetViewportCamera = [this]()
        {
            m_ViewportController.ResetCameraToDefault();
        };

        m_SceneBootstrapService.SeedDefaultSceneEntities(context);
    }

    bool TriangleLayer::IsSelectionValid() const
    {
        Editor::SceneBootstrapContext context {};
        context.scene = const_cast<Scene*>(&m_Scene);
        context.selectedEntity = m_SelectedEntity;
        context.isEntitySelected = [this](const EntityID entity) -> bool
        {
            return m_SceneEntityUtilityService.IsEntitySelected(m_SelectionState, entity);
        };
        return m_SceneBootstrapService.IsSelectionValid(context);
    }

    bool TriangleLayer::IsPlayModeActive() const
    {
        return m_PlayState != EditorPlayState::Stopped;
    }

    bool TriangleLayer::IsPlayModePaused() const
    {
        return m_PlayState == EditorPlayState::Paused;
    }

    bool TriangleLayer::IsSceneSimulationEnabled() const
    {
        return m_PlayState == EditorPlayState::Playing && m_PhysicsSimulationEnabled;
    }

    std::vector<UUID> TriangleLayer::CaptureSelectedEntityUuids() const
    {
        std::vector<UUID> selectionUuids;
        selectionUuids.reserve(m_SelectedEntities.size());

        const auto& registry = m_Scene.GetRegistry();
        for (const EntityID entity : m_SelectedEntities)
        {
            if (entity == entt::null || !registry.valid(entity) || !registry.all_of<IDComponent>(entity))
            {
                continue;
            }

            selectionUuids.push_back(registry.get<IDComponent>(entity).id);
        }

        return selectionUuids;
    }

    void TriangleLayer::RestoreSelectedEntityUuids(
        const std::vector<UUID>& selectionUuids,
        const UUID primarySelectionUuid)
    {
        m_SceneEntityUtilityService.ClearEntitySelection(m_SelectionState);

        for (const UUID selectionUuid : selectionUuids)
        {
            const EntityID entity = m_Scene.FindByUUID(selectionUuid);
            if (entity != entt::null)
            {
                m_SelectionState.Append(entity);
            }
        }

        if (primarySelectionUuid != 0)
        {
            const EntityID primaryEntity = m_Scene.FindByUUID(primarySelectionUuid);
            if (primaryEntity != entt::null)
            {
                m_SelectionState.Append(primaryEntity);
            }
        }

        if (m_SelectedEntity == entt::null)
        {
            const auto roots = m_Scene.GetRootEntities();
            if (!roots.empty())
            {
                Editor::SceneEntitySelectionContext selectionContext { &m_Scene, &m_SelectionState, &m_SelectedContentEntry };
                m_SceneEntityUtilityService.SelectSingleEntity(selectionContext, roots.front());
            }
        }
    }
}


