#include "Luma/Asset/Import/BuiltInImporters.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "Luma/Asset/Core/MeshAssetIO.h"

namespace Luma::Assets
{
    namespace
    {
        std::string ToLower(std::string value)
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

        bool IsTruthySettingValue(const std::string_view value)
        {
            std::string normalized(value);
            normalized = ToLower(std::move(normalized));
            return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
        }

        bool ReadFileBinary(const std::filesystem::path& filePath, std::vector<std::uint8_t>& outData, std::string& outError)
        {
            std::ifstream input(filePath, std::ios::binary);
            if (!input.is_open())
            {
                outError = "Failed to open source file: " + filePath.string();
                return false;
            }

            input.seekg(0, std::ios::end);
            const std::streamsize size = input.tellg();
            if (size < 0)
            {
                outError = "Failed to read source size: " + filePath.string();
                return false;
            }
            input.seekg(0, std::ios::beg);

            outData.resize(static_cast<std::size_t>(size));
            if (size > 0)
            {
                input.read(reinterpret_cast<char*>(outData.data()), size);
            }
            if (!input.good() && !input.eof())
            {
                outError = "Failed to read source data: " + filePath.string();
                return false;
            }
            return true;
        }

        std::vector<std::uint8_t> BuildIntermediatePayload(
            const std::filesystem::path& sourcePath,
            const AssetType type,
            const ImportSettingsMap& settings,
            const std::vector<std::uint8_t>& sourceData)
        {
            std::vector<std::pair<std::string, std::string>> sortedSettings;
            sortedSettings.reserve(settings.size());
            for (const auto& [key, value] : settings)
            {
                sortedSettings.emplace_back(key, value);
            }
            std::sort(
                sortedSettings.begin(),
                sortedSettings.end(),
                [](const auto& lhs, const auto& rhs)
                {
                    return lhs.first < rhs.first;
                });

            std::ostringstream header;
            header << "LUMA_INTERMEDIATE_V1\n";
            header << "Type=" << ToString(type) << "\n";
            header << "Source=" << sourcePath.filename().string() << "\n";
            for (const auto& [key, value] : sortedSettings)
            {
                header << "Setting." << key << '=' << value << "\n";
            }
            header << "\n";
            const std::string headerText = header.str();

            std::vector<std::uint8_t> payload;
            payload.reserve(headerText.size() + sourceData.size());
            payload.insert(payload.end(), headerText.begin(), headerText.end());
            payload.insert(payload.end(), sourceData.begin(), sourceData.end());
            return payload;
        }

        ImportSettingsSchema BuildModelSchema();

        ImportSettingsSchema BuildLuaScriptSchema()
        {
            ImportSettingsSchema schema;
            return schema;
        }

        class ExtensionImporter final : public IAssetImporter
        {
        public:
            ExtensionImporter(
                std::string importerId,
                const AssetType outputType,
                const int priority,
                std::unordered_set<std::string> extensions,
                ImportSettingsSchema schema)
                : m_ImporterId(std::move(importerId))
                , m_OutputType(outputType)
                , m_Priority(priority)
                , m_Extensions(std::move(extensions))
                , m_Schema(std::move(schema))
            {
                m_Schema.importerID = m_ImporterId;
            }

            std::string_view GetImporterID() const override
            {
                return m_ImporterId;
            }

            std::uint32_t GetVersion() const override
            {
                return 1;
            }

            int GetPriority() const override
            {
                return m_Priority;
            }

            bool CanHandle(const ImportRequest& request) const override
            {
                if (request.sourcePaths.empty())
                {
                    return false;
                }

                const std::string extension = ToLower(request.sourcePaths.front().extension().string());
                return m_Extensions.contains(extension);
            }

            AssetType GetOutputType(const ImportRequest& request) const override
            {
                (void)request;
                return m_OutputType;
            }

            ImportSettingsSchema BuildSettingsSchema() const override
            {
                return m_Schema;
            }

            bool Import(
                const ImportRequest& request,
                const ImportContext& context,
                const ImportSettingsMap& resolvedSettings,
                ImportOutput& outOutput,
                std::string& outError) const override
            {
                (void)context;
                if (request.sourcePaths.empty())
                {
                    outError = "Importer request missing source path.";
                    return false;
                }

                const std::filesystem::path& sourcePath = request.sourcePaths.front();
                std::vector<std::uint8_t> sourceData;
                if (!ReadFileBinary(sourcePath, sourceData, outError))
                {
                    return false;
                }

                outOutput.meta.name = sourcePath.stem().string();
                outOutput.intermediate.type = m_OutputType;
                outOutput.intermediate.tags = { "imported", std::string(ToString(m_OutputType)) };
                outOutput.intermediate.payload =
                    BuildIntermediatePayload(sourcePath, m_OutputType, resolvedSettings, sourceData);
                return true;
            }

        private:
            std::string m_ImporterId;
            AssetType m_OutputType = AssetType::Unknown;
            int m_Priority = 0;
            std::unordered_set<std::string> m_Extensions;
            ImportSettingsSchema m_Schema;
        };

        class ModelImporter final : public IAssetImporter
        {
        public:
            std::string_view GetImporterID() const override
            {
                return "builtin.model";
            }

            std::uint32_t GetVersion() const override
            {
                return 2;
            }

            int GetPriority() const override
            {
                return 105;
            }

            bool CanHandle(const ImportRequest& request) const override
            {
                if (request.sourcePaths.empty())
                {
                    return false;
                }

                const std::string extension = ToLower(request.sourcePaths.front().extension().string());
                return extension == ".gltf" || extension == ".glb" || extension == ".obj" || extension == ".fbx";
            }

            AssetType GetOutputType(const ImportRequest& request) const override
            {
                (void)request;
                return AssetType::StaticMesh;
            }

            ImportSettingsSchema BuildSettingsSchema() const override
            {
                ImportSettingsSchema schema = BuildModelSchema();
                schema.importerID = std::string(GetImporterID());
                return schema;
            }

            bool Import(
                const ImportRequest& request,
                const ImportContext& context,
                const ImportSettingsMap& resolvedSettings,
                ImportOutput& outOutput,
                std::string& outError) const override
            {
                if (request.sourcePaths.empty())
                {
                    outError = "Importer request missing source path.";
                    return false;
                }

                const std::filesystem::path& sourcePath = request.sourcePaths.front();
                MeshAssetData meshAsset;
                if (!LoadMeshAssetData(sourcePath, meshAsset, outError))
                {
                    return false;
                }

                outOutput.meta.name = sourcePath.stem().string();
                outOutput.intermediate.type = AssetType::StaticMesh;
                outOutput.intermediate.tags = { "imported", "StaticMesh", "cooked" };
                std::vector<MeshAssetChunkPayload> chunks;
                const std::filesystem::path chunkDirectory =
                    std::filesystem::path(outOutput.meta.name + ".meshchunks");
                if (!BuildCookedMeshAssetPayload(
                        meshAsset,
                        sourcePath.filename().string(),
                        chunkDirectory,
                        outOutput.intermediate.payload,
                        chunks,
                        outError))
                {
                    return false;
                }
                outOutput.intermediate.sidecarFiles.clear();
                outOutput.intermediate.sidecarFiles.reserve(chunks.size());
                for (auto& chunk : chunks)
                {
                    outOutput.intermediate.sidecarFiles.push_back({
                        .relativePath = std::move(chunk.relativePath),
                        .payload = std::move(chunk.payload)
                    });
                }

                return true;
            }
        };

        ImportSettingsSchema BuildTextureSchema()
        {
            ImportSettingsSchema schema;
            schema.fields = {
                ImportSettingField { "colorSpace", "Color Space", "Texture", ImportSettingType::Enum, "sRGB", {}, {}, { "sRGB", "Linear" }, "Controls gamma conversion." },
                ImportSettingField { "generateMipmaps", "Generate Mipmaps", "Texture", ImportSettingType::Bool, "true", {}, {}, {}, "Build mip chain for runtime streaming." },
                ImportSettingField { "compression", "Compression", "Texture", ImportSettingType::Enum, "Auto", {}, {}, { "Auto", "None", "BC7", "BC5", "ASTC" }, "Preferred cooker compression target." }
            };
            return schema;
        }

        ImportSettingsSchema BuildHDRISchema()
        {
            ImportSettingsSchema schema;
            schema.fields = {
                ImportSettingField { "mapping", "Mapping", "HDRI", ImportSettingType::Enum, "Equirectangular", {}, {}, { "Equirectangular", "Cubemap" }, "Source projection model." },
                ImportSettingField { "generateIbl", "Generate IBL", "HDRI", ImportSettingType::Bool, "true", {}, {}, {}, "Generate irradiance and reflection resources on cook." },
                ImportSettingField { "exposure", "Exposure", "HDRI", ImportSettingType::Float, "1.0", "0.0", "8.0", {}, "Exposure multiplier for preview." }
            };
            return schema;
        }

        ImportSettingsSchema BuildModelSchema()
        {
            ImportSettingsSchema schema;
            schema.fields = {
                ImportSettingField { "generateTangents", "Generate Tangents", "Mesh", ImportSettingType::Bool, "true", {}, {}, {}, "Computes tangents if source omits them." },
                ImportSettingField { "importMaterials", "Import Materials", "Mesh", ImportSettingType::Bool, "true", {}, {}, {}, "Auto-create PBR material assets." },
                ImportSettingField { "generateCollision", "Generate Collision", "Mesh", ImportSettingType::Bool, "true", {}, {}, {}, "Generate simple collision mesh." },
                ImportSettingField { "lodPolicy", "LOD Policy", "Mesh", ImportSettingType::Enum, "FromSource", {}, {}, { "FromSource", "AutoGenerate", "Disabled" }, "LOD import behavior." }
            };
            return schema;
        }

        ImportSettingsSchema BuildAudioSchema()
        {
            ImportSettingsSchema schema;
            schema.fields = {
                ImportSettingField { "streaming", "Streaming", "Audio", ImportSettingType::Bool, "false", {}, {}, {}, "Streams from disk instead of full preload." },
                ImportSettingField { "compression", "Compression", "Audio", ImportSettingType::Enum, "Vorbis", {}, {}, { "PCM", "Vorbis", "ADPCM" }, "Cooker output compression codec." },
                ImportSettingField { "normalize", "Normalize", "Audio", ImportSettingType::Bool, "false", {}, {}, {}, "Normalizes clip loudness during import." }
            };
            return schema;
        }

        ImportSettingsSchema BuildProceduralSchema()
        {
            ImportSettingsSchema schema;
            schema.fields = {
                ImportSettingField { "generatorId", "Generator ID", "Procedural", ImportSettingType::String, "Luma::NoiseRock", {}, {}, {}, "Runtime generator identifier." },
                ImportSettingField { "seed", "Seed", "Procedural", ImportSettingType::Int, "1337", "0", "2147483647", {}, "Deterministic generation seed." },
                ImportSettingField { "bakeOnImport", "Bake Outputs", "Procedural", ImportSettingType::Bool, "false", {}, {}, {}, "Bake generated outputs as static assets." }
            };
            return schema;
        }

        ImportSettingsSchema BuildPackageSchema()
        {
            ImportSettingsSchema schema;
            schema.fields = {
                ImportSettingField { "installMode", "Install Mode", "Package", ImportSettingType::Enum, "Project", {}, {}, { "Project", "GlobalCache" }, "Installation location policy." },
                ImportSettingField { "verifyHash", "Verify Hash", "Package", ImportSettingType::Bool, "true", {}, {}, {}, "Checks package integrity before install." }
            };
            return schema;
        }
    }

    void RegisterBuiltInImporters(ImporterRegistry& registry)
    {
        registry.RegisterImporter(std::make_unique<ExtensionImporter>(
            "builtin.texture",
            AssetType::Texture2D,
            110,
            std::unordered_set<std::string> { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds" },
            BuildTextureSchema()));

        registry.RegisterImporter(std::make_unique<ExtensionImporter>(
            "builtin.hdri",
            AssetType::HDRI,
            120,
            std::unordered_set<std::string> { ".hdr", ".exr" },
            BuildHDRISchema()));

        registry.RegisterImporter(std::make_unique<ModelImporter>());

        registry.RegisterImporter(std::make_unique<ExtensionImporter>(
            "builtin.audio",
            AssetType::AudioClip,
            100,
            std::unordered_set<std::string> { ".wav", ".mp3", ".ogg", ".flac" },
            BuildAudioSchema()));

        registry.RegisterImporter(std::make_unique<ExtensionImporter>(
            "builtin.lua",
            AssetType::LuaScript,
            108,
            std::unordered_set<std::string> { ".lua" },
            BuildLuaScriptSchema()));

        registry.RegisterImporter(std::make_unique<ExtensionImporter>(
            "builtin.procedural",
            AssetType::Procedural,
            95,
            std::unordered_set<std::string> { ".lpro", ".lumaproc", ".procjson" },
            BuildProceduralSchema()));

        registry.RegisterImporter(std::make_unique<ExtensionImporter>(
            "builtin.package",
            AssetType::PackageManifest,
            115,
            std::unordered_set<std::string> { ".lpk", ".lumapkg", ".lpkmanifest" },
            BuildPackageSchema()));
    }
}
