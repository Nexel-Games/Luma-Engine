#include "Luma/Editor/Assets/MeshEntityImportService.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include "Luma/Asset/Core/MeshAssetIO.h"
#include "Luma/Renderer/PrimitiveMeshFactory.h"
#include "Luma/Scene/MaterialComponent.h"
#include "Luma/Scene/MeshRendererComponent.h"
#include "Luma/Scene/TransformComponent.h"

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
                [](const unsigned char character)
                {
                    return static_cast<char>(std::tolower(character));
                });
            return value;
        }

        bool PathIsWithinRoot(const std::filesystem::path& path, const std::filesystem::path& root)
        {
            if (path.empty() || root.empty())
            {
                return false;
            }

            const std::string normalizedPath = path.lexically_normal().generic_string();
            std::string normalizedRoot = root.lexically_normal().generic_string();
            if (normalizedRoot.empty())
            {
                return false;
            }

            if (!normalizedRoot.empty() && normalizedRoot.back() == '/')
            {
                normalizedRoot.pop_back();
            }

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

        bool IsRawSceneMeshPath(const std::filesystem::path& path)
        {
            const std::string extension = ToLowerString(path.extension().string());
            return extension == ".obj" ||
                extension == ".fbx" ||
                extension == ".gltf" ||
                extension == ".glb";
        }

        std::string BuildSceneAssetReferencePath(
            const MeshEntityImportContext& context,
            const std::filesystem::path& assetPath)
        {
            if (assetPath.empty())
            {
                return {};
            }

            std::error_code ec;
            const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(assetPath, ec);
            const std::filesystem::path finalPath = ec ? assetPath.lexically_normal() : canonicalPath.lexically_normal();

            if (context.projectLoaded)
            {
                const std::filesystem::path assetsRoot = context.projectAssetsPath.lexically_normal();
                if (!assetsRoot.empty() && PathIsWithinRoot(finalPath, assetsRoot))
                {
                    std::error_code relEc;
                    const std::filesystem::path relativePath = std::filesystem::relative(finalPath, assetsRoot, relEc);
                    if (!relEc && !relativePath.empty())
                    {
                        return relativePath.generic_string();
                    }
                }

                const std::filesystem::path projectRoot = context.projectRoot.lexically_normal();
                if (!projectRoot.empty() && PathIsWithinRoot(finalPath, projectRoot))
                {
                    std::error_code relEc;
                    const std::filesystem::path relativePath = std::filesystem::relative(finalPath, projectRoot, relEc);
                    if (!relEc && !relativePath.empty())
                    {
                        return relativePath.generic_string();
                    }
                }
            }

            return finalPath.generic_string();
        }

        std::string BuildSceneMeshSourcePath(
            const MeshEntityImportContext& context,
            const std::filesystem::path& assetPath)
        {
            std::error_code ec;
            const std::filesystem::path canonicalPath = std::filesystem::weakly_canonical(assetPath, ec);
            std::filesystem::path finalPath = ec ? assetPath.lexically_normal() : canonicalPath.lexically_normal();

            const std::string extension = ToLowerString(finalPath.extension().string());
            if (extension == ".obj" || extension == ".fbx" || extension == ".gltf" || extension == ".glb")
            {
                std::error_code cookedEc;
                const std::filesystem::path cookedPath = finalPath.parent_path() / (finalPath.stem().string() + ".lumamesh");
                if (std::filesystem::exists(cookedPath, cookedEc))
                {
                    finalPath = cookedPath.lexically_normal();
                }
            }

            return BuildSceneAssetReferencePath(context, finalPath);
        }

        void ApplySpawnTransform(
            TransformComponent& transform,
            const EntityID parentEntity,
            const std::array<float, 3>* worldPosition)
        {
            if (worldPosition != nullptr && parentEntity == entt::null)
            {
                transform.position = *worldPosition;
            }
            else
            {
                transform.position = { 0.0f, 0.0f, 0.0f };
            }
            transform.rotation = { 0.0f, 0.0f, 0.0f };
            transform.scale = { 1.0f, 1.0f, 1.0f };
            transform.dirty = true;
        }
    }

    EntityID MeshEntityImportService::CreateEntityFromMeshAsset(
        const MeshEntityImportContext& context,
        const std::filesystem::path& assetPath,
        const EntityID parentEntity,
        const std::array<float, 3>* worldPosition) const
    {
        if (context.scene == nullptr)
        {
            return entt::null;
        }

        std::filesystem::path resolvedPath;
        std::filesystem::path rawSceneSourcePath;
        if (IsRawSceneMeshPath(assetPath))
        {
            auto tryAppendRawCandidate = [&](const std::filesystem::path& candidate)
            {
                if (candidate.empty())
                {
                    return;
                }

                std::error_code ec;
                if (!std::filesystem::exists(candidate, ec))
                {
                    return;
                }

                const std::filesystem::path normalizedCandidate =
                    std::filesystem::weakly_canonical(candidate, ec);
                const std::filesystem::path finalCandidate =
                    ec ? candidate.lexically_normal() : normalizedCandidate.lexically_normal();
                if (!IsRawSceneMeshPath(finalCandidate))
                {
                    return;
                }
                if (rawSceneSourcePath.empty())
                {
                    rawSceneSourcePath = finalCandidate;
                }
            };

            tryAppendRawCandidate(assetPath);
            if (!assetPath.is_absolute())
            {
                if (!context.activeContentRoot.empty())
                {
                    tryAppendRawCandidate(context.activeContentRoot / assetPath);
                }
                if (context.projectLoaded)
                {
                    tryAppendRawCandidate(context.projectAssetsPath / assetPath);
                    tryAppendRawCandidate(context.projectRoot / assetPath);
                }
            }

            resolvedPath = !rawSceneSourcePath.empty()
                ? rawSceneSourcePath
                : Assets::ResolveMeshAssetPath(BuildSceneAssetReferencePath(context, assetPath));
            if ((resolvedPath.empty() || ToLowerString(resolvedPath.extension().string()) == ".lumamesh") &&
                rawSceneSourcePath.empty())
            {
                std::error_code ec;
                std::filesystem::path absoluteCandidate = assetPath;
                if (!assetPath.is_absolute() && context.projectLoaded)
                {
                    absoluteCandidate = context.projectAssetsPath / assetPath;
                }

                const std::filesystem::path canonicalCandidate = std::filesystem::weakly_canonical(absoluteCandidate, ec);
                resolvedPath = ec ? absoluteCandidate.lexically_normal() : canonicalCandidate.lexically_normal();
            }
            if (rawSceneSourcePath.empty() && !resolvedPath.empty() && IsRawSceneMeshPath(resolvedPath))
            {
                rawSceneSourcePath = resolvedPath;
            }
        }
        else
        {
            resolvedPath = Assets::ResolveMeshAssetPath(assetPath.generic_string());
            if (!resolvedPath.empty() && ToLowerString(resolvedPath.extension().string()) == ".lumamesh")
            {
                static const std::array<std::string_view, 4> kRawSceneExtensions {
                    ".obj",
                    ".fbx",
                    ".gltf",
                    ".glb"
                };
                for (const std::string_view extension : kRawSceneExtensions)
                {
                    std::error_code ec;
                    const std::filesystem::path candidate =
                        resolvedPath.parent_path() / (resolvedPath.stem().string() + std::string(extension));
                    if (std::filesystem::exists(candidate, ec))
                    {
                        const std::filesystem::path normalizedCandidate =
                            std::filesystem::weakly_canonical(candidate, ec);
                        rawSceneSourcePath = ec ? candidate.lexically_normal() : normalizedCandidate.lexically_normal();
                        break;
                    }
                }
            }
        }

        if (resolvedPath.empty())
        {
            return entt::null;
        }

        const std::string singleMeshSource = BuildSceneMeshSourcePath(context, resolvedPath);
        std::filesystem::path singleMeshResolvedPath;
        if (!singleMeshSource.empty())
        {
            singleMeshResolvedPath = Assets::ResolveMeshAssetPath(singleMeshSource);
        }

        const bool hasCookedMeshAsset =
            !singleMeshResolvedPath.empty() &&
            ToLowerString(singleMeshResolvedPath.extension().string()) == ".lumamesh";
        const bool useScenePartsImport =
            (IsRawSceneMeshPath(assetPath) || !rawSceneSourcePath.empty()) &&
            !hasCookedMeshAsset;
        auto& registry = context.scene->GetRegistry();

        if (useScenePartsImport)
        {
            std::vector<Assets::MeshScenePart> sceneParts;
            std::string sceneError;
            const std::filesystem::path sceneSourcePath = !rawSceneSourcePath.empty() ? rawSceneSourcePath : resolvedPath;
            if (Assets::LoadMeshSceneParts(sceneSourcePath, sceneParts, sceneError) && !sceneParts.empty())
            {
                const std::string rawMeshSource = BuildSceneAssetReferencePath(context, sceneSourcePath);
                if (!rawMeshSource.empty())
                {
                    if (context.cacheImportedSceneParts)
                    {
                        context.cacheImportedSceneParts(sceneSourcePath.generic_string(), sceneSourcePath, sceneParts);
                    }

                    const std::string baseName = assetPath.stem().string().empty() ? "Mesh" : assetPath.stem().string();
                    const std::string parentName =
                        context.generateUniqueEntityName ? context.generateUniqueEntityName(baseName) : baseName;
                    Entity parent = context.scene->CreateEntity(parentName);

                    auto& parentTransform = parent.GetComponent<TransformComponent>();
                    ApplySpawnTransform(parentTransform, parentEntity, worldPosition);

                    if (parentEntity != entt::null && registry.valid(parentEntity))
                    {
                        context.scene->SetParent(parent.GetHandle(), parentEntity);
                    }

                    int createdChildren = 0;
                    for (std::size_t partIndex = 0; partIndex < sceneParts.size(); ++partIndex)
                    {
                        const Assets::MeshScenePart& part = sceneParts[partIndex];
                        const std::string childBaseName =
                            part.name.empty() ? (baseName + "_" + std::to_string(partIndex)) : part.name;
                        const std::string childName =
                            context.generateUniqueEntityName ? context.generateUniqueEntityName(childBaseName) : childBaseName;
                        Entity child = context.scene->CreateEntity(childName);
                        auto& childTransform = child.GetComponent<TransformComponent>();
                        childTransform.position = { 0.0f, 0.0f, 0.0f };
                        childTransform.rotation = { 0.0f, 0.0f, 0.0f };
                        childTransform.scale = { 1.0f, 1.0f, 1.0f };
                        childTransform.dirty = true;

                        auto& meshRenderer = child.AddComponent<MeshRendererComponent>();
                        meshRenderer.visible = true;
                        meshRenderer.usePrimitive = false;
                        meshRenderer.meshSource = rawMeshSource;
                        meshRenderer.meshPartIndex = static_cast<std::uint32_t>(partIndex);
                        meshRenderer.meshLod = 0;
                        meshRenderer.autoStreamLod = false;
                        meshRenderer.streamSectionsByDistance = false;
                        meshRenderer.color = { 1.0f, 1.0f, 1.0f, 1.0f };
                        auto& material = child.AddComponent<MaterialComponent>();
                        if (context.initializeDefaultMaterial)
                        {
                            context.initializeDefaultMaterial(material);
                        }

                        context.scene->SetParent(child.GetHandle(), parent.GetHandle());
                        ++createdChildren;
                    }

                    if (createdChildren > 0)
                    {
                        if (context.selectSingleEntity)
                        {
                            context.selectSingleEntity(parent.GetHandle());
                        }
                        return parent.GetHandle();
                    }
                }
            }
            else if (!sceneError.empty() && context.logImportWarning)
            {
                context.logImportWarning(sceneError);
            }
        }

        const std::filesystem::path sourceMaterialScenePath =
            !rawSceneSourcePath.empty()
                ? rawSceneSourcePath
                : (IsRawSceneMeshPath(resolvedPath) ? resolvedPath : std::filesystem::path {});
        if (singleMeshSource.empty())
        {
            return entt::null;
        }

        const std::string baseName = assetPath.stem().string().empty() ? "Mesh" : assetPath.stem().string();
        const std::string entityName =
            context.generateUniqueEntityName ? context.generateUniqueEntityName(baseName) : baseName;
        Entity entity = context.scene->CreateEntity(entityName);

        auto& transform = entity.GetComponent<TransformComponent>();
        ApplySpawnTransform(transform, parentEntity, worldPosition);

        auto& meshRenderer = entity.AddComponent<MeshRendererComponent>();
        meshRenderer.visible = true;
        meshRenderer.usePrimitive = false;
        meshRenderer.meshSource = singleMeshSource;
        if (!sourceMaterialScenePath.empty())
        {
            meshRenderer.importedSceneSource = BuildSceneAssetReferencePath(context, sourceMaterialScenePath);
        }
        meshRenderer.meshLod = 0;
        meshRenderer.autoStreamLod = true;
        meshRenderer.color = { 1.0f, 1.0f, 1.0f, 1.0f };
        auto& material = entity.AddComponent<MaterialComponent>();
        if (context.initializeDefaultMaterial)
        {
            context.initializeDefaultMaterial(material);
        }

        if (parentEntity != entt::null && registry.valid(parentEntity))
        {
            context.scene->SetParent(entity.GetHandle(), parentEntity);
        }

        if (context.selectSingleEntity)
        {
            context.selectSingleEntity(entity.GetHandle());
        }
        return entity.GetHandle();
    }
}
