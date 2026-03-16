#include "Luma/Editor/Scene/PrefabWorkflowService.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "Luma/Core/App/Project.h"
#include "Luma/Scene/IDComponent.h"
#include "Luma/Scene/PrefabInstanceComponent.h"
#include "Luma/Scene/PrefabSerializer.h"
#include "Luma/Scene/RelationshipComponent.h"
#include "Luma/Scene/SceneSerializer.h"
#include "Luma/Scene/TagComponent.h"

namespace Luma::Editor
{
    namespace
    {
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
    }

    std::filesystem::path PrefabWorkflowService::BuildUniquePrefabAssetPath(
        const PrefabWorkflowContext& context,
        const std::string_view baseName)
    {
        if (!Project::IsLoaded())
        {
            return {};
        }

        std::filesystem::path targetDirectory = Project::GetAssetsPath() / "Prefabs";
        if (context.currentContentDirectory != nullptr &&
            !context.currentContentDirectory->empty() &&
            PathIsWithinRoot(*context.currentContentDirectory, Project::GetAssetsPath()))
        {
            targetDirectory = *context.currentContentDirectory;
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

    EntityID PrefabWorkflowService::FindPrefabInstanceRoot(
        const PrefabWorkflowContext& context,
        const EntityID entity) const
    {
        if (context.scene == nullptr)
        {
            return entt::null;
        }

        const auto& registry = context.scene->GetRegistry();
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

    void PrefabWorkflowService::MarkPrefabInstanceHierarchy(
        const PrefabWorkflowContext& context,
        const EntityID rootEntity,
        const std::string& prefabAsset) const
    {
        if (context.scene == nullptr)
        {
            return;
        }

        auto& registry = context.scene->GetRegistry();
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

    bool PrefabWorkflowService::CreatePrefabFromEntity(
        PrefabWorkflowContext& context,
        const EntityID rootEntity)
    {
        if (context.playModeActive)
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Prefab creation is unavailable during play mode.";
            }
            return false;
        }
        if (!Project::IsLoaded())
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Load a project before creating prefabs.";
            }
            return false;
        }
        if (context.scene == nullptr)
        {
            return false;
        }

        const auto& registry = context.scene->GetRegistry();
        if (!registry.valid(rootEntity) || !registry.all_of<TagComponent>(rootEntity))
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Select a valid entity to create a prefab.";
            }
            return false;
        }

        const auto& tag = registry.get<TagComponent>(rootEntity);
        const std::filesystem::path prefabPath =
            BuildUniquePrefabAssetPath(context, tag.name.empty() ? "NewPrefab" : tag.name);
        if (prefabPath.empty())
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Failed to allocate a prefab asset path.";
            }
            return false;
        }

        context.scene->UpdateWorldTransforms();
        std::string error;
        if (!PrefabSerializer::SerializePrefab(*context.scene, rootEntity, prefabPath, error))
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Failed to create prefab: " + error;
            }
            return false;
        }

        MarkPrefabInstanceHierarchy(context, rootEntity, prefabPath.lexically_normal().generic_string());
        m_PrefabStatusCacheRootEntity = entt::null;
        m_PrefabOverrideCacheEntity = entt::null;
        if (context.contentBrowserHostFacadeService != nullptr && context.buildContentBrowserContext)
        {
            ContentBrowserHostFacadeContext contentBrowserContext = context.buildContentBrowserContext();
            context.contentBrowserHostFacadeService->InvalidateFolderTreeCache(contentBrowserContext);
            context.contentBrowserHostFacadeService->RefreshContentEntries(contentBrowserContext);
        }
        if (context.editorStatus != nullptr)
        {
            context.editorStatus->Content() = "Created prefab: " + prefabPath.filename().string();
        }
        return true;
    }

    EntityID PrefabWorkflowService::InstantiatePrefabAsset(
        PrefabWorkflowContext& context,
        const std::filesystem::path& prefabPath,
        const EntityID parentEntity)
    {
        if (context.playModeActive)
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Prefab instancing is unavailable during play mode.";
            }
            return entt::null;
        }
        if (context.scene == nullptr)
        {
            return entt::null;
        }

        std::string error;
        EntityID instantiatedRoot = entt::null;
        if (!PrefabSerializer::InstantiatePrefab(*context.scene, prefabPath, &instantiatedRoot, error) ||
            instantiatedRoot == entt::null)
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Failed to instantiate prefab: " + error;
            }
            return entt::null;
        }

        if (parentEntity != entt::null && context.scene->GetRegistry().valid(parentEntity))
        {
            context.scene->SetParent(instantiatedRoot, parentEntity);
        }
        context.scene->UpdateWorldTransforms();

        if (context.selectSingleEntity)
        {
            context.selectSingleEntity(instantiatedRoot);
        }
        m_PrefabStatusCacheRootEntity = entt::null;
        m_PrefabOverrideCacheEntity = entt::null;
        if (context.markSceneRenderCacheDirty)
        {
            context.markSceneRenderCacheDirty(SceneRenderCacheDirtyFlags::All);
        }
        if (context.editorStatus != nullptr)
        {
            context.editorStatus->Content() = "Instantiated prefab: " + prefabPath.filename().string();
        }
        return instantiatedRoot;
    }

    bool PrefabWorkflowService::ApplyPrefabInstance(
        PrefabWorkflowContext& context,
        const EntityID entity)
    {
        if (context.playModeActive)
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Prefab apply is unavailable during play mode.";
            }
            return false;
        }
        if (context.scene == nullptr)
        {
            return false;
        }

        const EntityID rootEntity = FindPrefabInstanceRoot(context, entity);
        auto& registry = context.scene->GetRegistry();
        const auto* prefabInstance = rootEntity != entt::null ? registry.try_get<PrefabInstanceComponent>(rootEntity) : nullptr;
        if (prefabInstance == nullptr)
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Selected entity is not part of a prefab instance.";
            }
            return false;
        }

        std::filesystem::path prefabPath(prefabInstance->prefabAsset);
        if (!prefabPath.is_absolute() && Project::IsLoaded())
        {
            prefabPath = (Project::GetProjectRoot() / prefabPath).lexically_normal();
        }

        context.scene->UpdateWorldTransforms();
        std::string error;
        if (!PrefabSerializer::SerializePrefab(*context.scene, rootEntity, prefabPath, error))
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Failed to apply prefab: " + error;
            }
            return false;
        }

        MarkPrefabInstanceHierarchy(context, rootEntity, prefabPath.lexically_normal().generic_string());
        m_PrefabStatusCacheRootEntity = entt::null;
        m_PrefabOverrideCacheEntity = entt::null;
        if (context.contentBrowserHostFacadeService != nullptr && context.buildContentBrowserContext)
        {
            ContentBrowserHostFacadeContext contentBrowserContext = context.buildContentBrowserContext();
            context.contentBrowserHostFacadeService->InvalidateFolderTreeCache(contentBrowserContext);
            context.contentBrowserHostFacadeService->RefreshContentEntries(contentBrowserContext);
        }
        if (context.editorStatus != nullptr)
        {
            context.editorStatus->Content() = "Applied prefab: " + prefabPath.filename().string();
        }
        return true;
    }

    bool PrefabWorkflowService::RevertPrefabInstance(
        PrefabWorkflowContext& context,
        const EntityID entity)
    {
        if (context.playModeActive)
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Prefab revert is unavailable during play mode.";
            }
            return false;
        }
        if (context.scene == nullptr)
        {
            return false;
        }

        const EntityID rootEntity = FindPrefabInstanceRoot(context, entity);
        auto& registry = context.scene->GetRegistry();
        const auto* prefabInstance = rootEntity != entt::null ? registry.try_get<PrefabInstanceComponent>(rootEntity) : nullptr;
        const auto* relationship = rootEntity != entt::null ? registry.try_get<RelationshipComponent>(rootEntity) : nullptr;
        if (prefabInstance == nullptr)
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Selected entity is not part of a prefab instance.";
            }
            return false;
        }

        std::filesystem::path prefabPath(prefabInstance->prefabAsset);
        if (!prefabPath.is_absolute() && Project::IsLoaded())
        {
            prefabPath = (Project::GetProjectRoot() / prefabPath).lexically_normal();
        }

        const EntityID parentEntity = relationship != nullptr ? relationship->parent : entt::null;
        EntityID newRootEntity = InstantiatePrefabAsset(context, prefabPath, entt::null);
        if (newRootEntity == entt::null)
        {
            return false;
        }

        if (parentEntity != entt::null && registry.valid(parentEntity))
        {
            context.scene->SetParent(newRootEntity, parentEntity);
        }

        context.scene->DestroyEntity(rootEntity);
        context.scene->UpdateWorldTransforms();

        if (context.selectSingleEntity)
        {
            context.selectSingleEntity(newRootEntity);
        }
        m_PrefabStatusCacheRootEntity = entt::null;
        m_PrefabOverrideCacheEntity = entt::null;
        if (context.markSceneRenderCacheDirty)
        {
            context.markSceneRenderCacheDirty(SceneRenderCacheDirtyFlags::All);
        }
        if (context.editorStatus != nullptr)
        {
            context.editorStatus->Content() = "Reverted prefab: " + prefabPath.filename().string();
        }
        return true;
    }

    std::string PrefabWorkflowService::GetPrefabInstanceStatus(
        PrefabWorkflowContext& context,
        const EntityID entity)
    {
        if (context.scene == nullptr)
        {
            return {};
        }

        const EntityID rootEntity = FindPrefabInstanceRoot(context, entity);
        auto& registry = context.scene->GetRegistry();
        const auto* prefabInstance = rootEntity != entt::null ? registry.try_get<PrefabInstanceComponent>(rootEntity) : nullptr;
        if (prefabInstance == nullptr)
        {
            return {};
        }

        const bool sceneDirty = context.sceneDirty;
        if (rootEntity == m_PrefabStatusCacheRootEntity &&
            prefabInstance->prefabAsset == m_PrefabStatusCachePrefabAsset &&
            sceneDirty == m_PrefabStatusCacheSceneDirty &&
            (context.timeSeconds - m_PrefabStatusCacheTimeSeconds) < 0.5f)
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
            m_PrefabStatusCacheTimeSeconds = context.timeSeconds;
            m_PrefabStatusCacheValue = "Source Missing";
            return m_PrefabStatusCacheValue;
        }

        std::filesystem::path tempPath =
            std::filesystem::temp_directory_path(ec) /
            ("luma_prefab_compare_" + std::to_string(static_cast<std::uint32_t>(entt::to_integral(rootEntity))) + ".lumaprefab");
        std::string error;
        std::string statusValue = "Unknown";
        if (!ec && PrefabSerializer::SerializePrefab(*context.scene, rootEntity, tempPath, error))
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
        m_PrefabStatusCacheTimeSeconds = context.timeSeconds;
        m_PrefabStatusCacheValue = std::move(statusValue);
        return m_PrefabStatusCacheValue;
    }

    std::vector<std::string> PrefabWorkflowService::GetPrefabOverridePaths(
        PrefabWorkflowContext& context,
        const EntityID entity)
    {
        if (context.scene == nullptr)
        {
            return {};
        }

        auto& registry = context.scene->GetRegistry();
        if (!registry.valid(entity))
        {
            return {};
        }

        const auto* prefabInstance = registry.try_get<PrefabInstanceComponent>(entity);
        if (prefabInstance == nullptr)
        {
            return {};
        }

        const bool sceneDirty = context.sceneDirty;
        if (entity == m_PrefabOverrideCacheEntity &&
            prefabInstance->prefabAsset == m_PrefabOverrideCachePrefabAsset &&
            prefabInstance->sourceEntityId == m_PrefabOverrideCacheSourceEntityId &&
            sceneDirty == m_PrefabOverrideCacheSceneDirty &&
            (context.timeSeconds - m_PrefabOverrideCacheTimeSeconds) < 0.5f)
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
                if (!ec && SceneSerializer::Serialize(*context.scene, tempPath, error))
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
        m_PrefabOverrideCacheTimeSeconds = context.timeSeconds;
        m_PrefabOverrideCacheValue = overridePaths;
        return m_PrefabOverrideCacheValue;
    }

    bool PrefabWorkflowService::RevertPrefabComponent(
        PrefabWorkflowContext& context,
        const EntityID entity,
        const std::string_view componentPath)
    {
        return RevertPrefabOverridePath(context, entity, componentPath);
    }

    bool PrefabWorkflowService::RevertPrefabOverridePath(
        PrefabWorkflowContext& context,
        const EntityID entity,
        const std::string_view overridePath)
    {
        if (context.playModeActive)
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Prefab override revert is unavailable during play mode.";
            }
            return false;
        }
        if (context.scene == nullptr)
        {
            return false;
        }

        auto& registry = context.scene->GetRegistry();
        if (!registry.valid(entity) || !registry.all_of<IDComponent>(entity))
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Select a valid prefab instance entity.";
            }
            return false;
        }

        const auto* prefabInstance = registry.try_get<PrefabInstanceComponent>(entity);
        if (prefabInstance == nullptr)
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Selected entity is not a prefab instance entity.";
            }
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
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Failed to open prefab source: " + prefabPath.filename().string();
            }
            return false;
        }

        nlohmann::json sourceRoot;
        try
        {
            sourceInput >> sourceRoot;
        }
        catch (const std::exception&)
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Failed to parse prefab source: " + prefabPath.filename().string();
            }
            return false;
        }

        std::error_code ec;
        const std::filesystem::path tempScenePath =
            std::filesystem::temp_directory_path(ec) /
            ("luma_prefab_patch_" + std::to_string(static_cast<std::uint32_t>(entt::to_integral(entity))) + ".scene");
        if (ec)
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Failed to allocate a temporary scene file for prefab revert.";
            }
            return false;
        }

        std::string error;
        context.scene->UpdateWorldTransforms();
        if (!SceneSerializer::Serialize(*context.scene, tempScenePath, error))
        {
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Failed to snapshot scene for prefab revert: " + error;
            }
            return false;
        }

        std::ifstream currentInput(tempScenePath, std::ios::binary);
        if (!currentInput.is_open())
        {
            std::filesystem::remove(tempScenePath, ec);
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Failed to reopen temporary scene snapshot for prefab revert.";
            }
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
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Failed to parse temporary scene snapshot for prefab revert.";
            }
            return false;
        }

        const UUID currentEntityId = registry.get<IDComponent>(entity).id;
        const nlohmann::json* sourceEntityJson = FindEntityJsonByUuid(sourceRoot, prefabInstance->sourceEntityId);
        nlohmann::json* currentEntityJson = const_cast<nlohmann::json*>(FindEntityJsonByUuid(currentRoot, currentEntityId));
        if (sourceEntityJson == nullptr || currentEntityJson == nullptr)
        {
            std::filesystem::remove(tempScenePath, ec);
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Failed to resolve matching prefab entity data for revert.";
            }
            return false;
        }

        const std::string normalizedPath = StripPrefabOverridePrefix(overridePath);
        const std::vector<std::string> tokens = ParseOverridePathTokens(normalizedPath);
        if (tokens.empty())
        {
            std::filesystem::remove(tempScenePath, ec);
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Invalid prefab override path.";
            }
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
                if (context.editorStatus != nullptr)
                {
                    context.editorStatus->Content() = "Failed to write patched scene snapshot for prefab revert.";
                }
                return false;
            }
            output << currentRoot.dump(2);
        }

        Scene restoredScene;
        if (!SceneSerializer::Deserialize(tempScenePath, restoredScene, error))
        {
            std::filesystem::remove(tempScenePath, ec);
            if (context.editorStatus != nullptr)
            {
                context.editorStatus->Content() = "Failed to restore scene after prefab revert: " + error;
            }
            return false;
        }

        const std::vector<UUID> selectedEntityUuids = context.captureSelectedEntityUuids
            ? context.captureSelectedEntityUuids()
            : std::vector<UUID> {};
        UUID primarySelectedUuid = 0;
        if (context.selectionState != nullptr && context.selectionState->PrimaryRef() != entt::null &&
            registry.valid(context.selectionState->PrimaryRef()) &&
            registry.all_of<IDComponent>(context.selectionState->PrimaryRef()))
        {
            primarySelectedUuid = registry.get<IDComponent>(context.selectionState->PrimaryRef()).id;
        }

        context.scene->Swap(restoredScene);
        context.scene->UpdateWorldTransforms();
        if (context.restoreSelectedEntityUuids)
        {
            context.restoreSelectedEntityUuids(selectedEntityUuids, primarySelectedUuid);
        }
        m_PrefabStatusCacheRootEntity = entt::null;
        m_PrefabOverrideCacheEntity = entt::null;
        if (context.markSceneRenderCacheDirty)
        {
            context.markSceneRenderCacheDirty(SceneRenderCacheDirtyFlags::All);
        }
        if (context.updateSceneDirtyState)
        {
            context.updateSceneDirtyState();
        }
        std::filesystem::remove(tempScenePath, ec);

        if (context.editorStatus != nullptr)
        {
            context.editorStatus->Content() = "Reverted prefab override: " + normalizedPath;
        }
        return true;
    }

    void PrefabWorkflowService::SelectPrefabAsset(
        PrefabWorkflowContext& context,
        const EntityID entity)
    {
        const EntityID rootEntity = FindPrefabInstanceRoot(context, entity);
        if (context.scene == nullptr)
        {
            return;
        }

        const auto& registry = context.scene->GetRegistry();
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

        if (context.selectedContentEntry != nullptr)
        {
            *context.selectedContentEntry = prefabPath;
        }
        if (context.editorStatus != nullptr)
        {
            context.editorStatus->Content() = "Selected prefab asset: " + prefabPath.filename().string();
        }
    }
}
