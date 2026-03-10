#include "Luma/Asset/Package/PackageManager.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "Luma/Asset/Package/PackageSemVer.h"
#include "Luma/Core/App/Project.h"
#include "Luma/Core/Foundation/DynamicLibrary.h"
#include "Luma/Core/Foundation/Logging.h"

#include <miniz.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <urlmon.h>
#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Urlmon.lib")
#endif

namespace Luma::Assets
{
    namespace
    {
        using json = nlohmann::json;

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

        std::string Trim(std::string value)
        {
            auto notSpace = [](const unsigned char c)
            {
                return std::isspace(c) == 0;
            };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
            return value;
        }

        bool StartsWithInsensitive(const std::string& value, const std::string& prefix)
        {
            if (prefix.size() > value.size())
            {
                return false;
            }

            for (std::size_t i = 0; i < prefix.size(); ++i)
            {
                const char lhs = static_cast<char>(std::tolower(static_cast<unsigned char>(value[i])));
                const char rhs = static_cast<char>(std::tolower(static_cast<unsigned char>(prefix[i])));
                if (lhs != rhs)
                {
                    return false;
                }
            }
            return true;
        }

        bool IsHttpUrl(const std::string& value)
        {
            return StartsWithInsensitive(value, "http://") || StartsWithInsensitive(value, "https://");
        }

        constexpr std::string_view kCanonicalGitHubRegistryRoot =
            "https://raw.githubusercontent.com/NexelGames71/Luma-Package-Registry/main";

        bool IsLegacyGitHubRegistryLocation(const std::string& location)
        {
            return StartsWithInsensitive(location, "https://raw.githubusercontent.com/nexelgames/Luma-Package-Registry/main") ||
                StartsWithInsensitive(location, "https://raw.githubusercontent.com/NexelGames/Luma-Package-Registry/main");
        }

        bool IsLegacyLocalRegistrySource(const Luma::Assets::PackageRegistrySource& source)
        {
            if (ToLower(source.id) != "local")
            {
                return false;
            }

            if (source.location.empty() || IsHttpUrl(source.location))
            {
                return false;
            }

            std::error_code ec;
            const std::filesystem::path normalized = std::filesystem::path(source.location).lexically_normal();
            const std::string filename = ToLower(normalized.filename().string());
            if (filename == "luma-package-registry")
            {
                return true;
            }

            const std::filesystem::path weakCanonical = std::filesystem::weakly_canonical(normalized, ec);
            if (!ec)
            {
                return ToLower(weakCanonical.filename().string()) == "luma-package-registry";
            }

            return false;
        }

        bool ReadTextFile(const std::filesystem::path& path, std::string& outText, std::string& outError)
        {
            std::ifstream input(path, std::ios::binary);
            if (!input.is_open())
            {
                outError = "Failed to open file: " + path.string();
                return false;
            }

            std::ostringstream stream;
            stream << input.rdbuf();
            if (!input.good() && !input.eof())
            {
                outError = "Failed to read file: " + path.string();
                return false;
            }

            outText = stream.str();
            outError.clear();
            return true;
        }

        bool WriteTextFile(const std::filesystem::path& path, const std::string& text, std::string& outError)
        {
            std::error_code ec;
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec)
            {
                outError = "Failed to create directory: " + path.parent_path().string();
                return false;
            }

            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output.is_open())
            {
                outError = "Failed to open file for writing: " + path.string();
                return false;
            }
            output << text;
            if (!output.good())
            {
                outError = "Failed to write file: " + path.string();
                return false;
            }

            outError.clear();
            return true;
        }

        bool ReadJsonFile(const std::filesystem::path& path, json& outJson, std::string& outError)
        {
            std::string text;
            if (!ReadTextFile(path, text, outError))
            {
                return false;
            }

            try
            {
                outJson = json::parse(text);
                outError.clear();
                return true;
            }
            catch (const std::exception& ex)
            {
                outError = "JSON parse failed for " + path.string() + ": " + ex.what();
                return false;
            }
        }

        bool WriteJsonFile(const std::filesystem::path& path, const json& value, std::string& outError)
        {
            return WriteTextFile(path, value.dump(2), outError);
        }

        std::string JoinUrl(const std::string& base, const std::string& relative)
        {
            if (base.empty())
            {
                return relative;
            }
            if (relative.empty())
            {
                return base;
            }

            const bool baseEndsWithSlash = base.back() == '/';
            const bool relativeStartsWithSlash = relative.front() == '/';
            if (baseEndsWithSlash && relativeStartsWithSlash)
            {
                return base.substr(0, base.size() - 1) + relative;
            }
            if (!baseEndsWithSlash && !relativeStartsWithSlash)
            {
                return base + "/" + relative;
            }
            return base + relative;
        }

        bool DownloadFile(const std::string& url, const std::filesystem::path& destination, std::string& outError)
        {
            if (!IsHttpUrl(url))
            {
                outError = "Unsupported URL protocol: " + url;
                return false;
            }

            std::error_code ec;
            std::filesystem::create_directories(destination.parent_path(), ec);
            if (ec)
            {
                outError = "Failed to create destination folder: " + destination.parent_path().string();
                return false;
            }

#if defined(_WIN32)
            const std::wstring wideUrl(url.begin(), url.end());
            const std::wstring wideDestination = destination.wstring();
            const HRESULT result = URLDownloadToFileW(nullptr, wideUrl.c_str(), wideDestination.c_str(), 0, nullptr);
            if (FAILED(result))
            {
                std::ostringstream stream;
                stream << "Failed to download " << url << " (HRESULT=0x" << std::hex
                       << static_cast<unsigned long>(result) << ")";
                outError = stream.str();
                return false;
            }
            outError.clear();
            return true;
#else
            (void)destination;
            outError = "HTTP download currently supported only on Windows builds.";
            return false;
#endif
        }

        bool ComputeFileSHA256(const std::filesystem::path& filePath, std::string& outHash, std::string& outError)
        {
#if defined(_WIN32)
            std::ifstream input(filePath, std::ios::binary);
            if (!input.is_open())
            {
                outError = "Unable to open file for SHA256: " + filePath.string();
                return false;
            }

            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            std::vector<std::uint8_t> objectBuffer;
            std::vector<std::uint8_t> hashBuffer;

            ULONG objectLength = 0;
            ULONG dataLength = 0;
            ULONG hashLength = 0;

            NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
            if (status < 0)
            {
                outError = "BCryptOpenAlgorithmProvider failed.";
                return false;
            }

            status = BCryptGetProperty(
                algorithm,
                BCRYPT_OBJECT_LENGTH,
                reinterpret_cast<PUCHAR>(&objectLength),
                sizeof(objectLength),
                &dataLength,
                0);
            if (status < 0)
            {
                BCryptCloseAlgorithmProvider(algorithm, 0);
                outError = "BCryptGetProperty(object length) failed.";
                return false;
            }

            status = BCryptGetProperty(
                algorithm,
                BCRYPT_HASH_LENGTH,
                reinterpret_cast<PUCHAR>(&hashLength),
                sizeof(hashLength),
                &dataLength,
                0);
            if (status < 0)
            {
                BCryptCloseAlgorithmProvider(algorithm, 0);
                outError = "BCryptGetProperty(hash length) failed.";
                return false;
            }

            objectBuffer.resize(objectLength);
            hashBuffer.resize(hashLength);

            status = BCryptCreateHash(
                algorithm,
                &hash,
                objectBuffer.data(),
                static_cast<ULONG>(objectBuffer.size()),
                nullptr,
                0,
                0);
            if (status < 0)
            {
                BCryptCloseAlgorithmProvider(algorithm, 0);
                outError = "BCryptCreateHash failed.";
                return false;
            }

            std::vector<char> chunk(64 * 1024);
            while (input.good())
            {
                input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
                const std::streamsize count = input.gcount();
                if (count <= 0)
                {
                    break;
                }

                status = BCryptHashData(
                    hash,
                    reinterpret_cast<PUCHAR>(chunk.data()),
                    static_cast<ULONG>(count),
                    0);
                if (status < 0)
                {
                    BCryptDestroyHash(hash);
                    BCryptCloseAlgorithmProvider(algorithm, 0);
                    outError = "BCryptHashData failed.";
                    return false;
                }
            }

            status = BCryptFinishHash(hash, hashBuffer.data(), static_cast<ULONG>(hashBuffer.size()), 0);
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            if (status < 0)
            {
                outError = "BCryptFinishHash failed.";
                return false;
            }

            std::ostringstream stream;
            stream << std::hex;
            for (const std::uint8_t value : hashBuffer)
            {
                stream.width(2);
                stream.fill('0');
                stream << static_cast<unsigned int>(value);
            }

            outHash = ToLower(stream.str());
            outError.clear();
            return true;
#else
            (void)filePath;
            outError = "SHA256 verification is currently supported only on Windows builds.";
            return false;
#endif
        }

        bool VerifyArchiveIntegrity(
            const std::filesystem::path& archivePath,
            const std::string& expectedSha,
            const std::uint64_t expectedSize,
            std::string& outError)
        {
            if (!std::filesystem::exists(archivePath))
            {
                outError = "Archive missing: " + archivePath.string();
                return false;
            }

            if (expectedSize > 0)
            {
                std::error_code ec;
                const std::uintmax_t actualSize = std::filesystem::file_size(archivePath, ec);
                if (ec)
                {
                    outError = "Failed to read archive size: " + archivePath.string();
                    return false;
                }
                if (actualSize != expectedSize)
                {
                    outError = "Archive size mismatch for " + archivePath.string();
                    return false;
                }
            }

            if (!expectedSha.empty())
            {
                std::string actualSha;
                if (!ComputeFileSHA256(archivePath, actualSha, outError))
                {
                    return false;
                }
                if (ToLower(expectedSha) != ToLower(actualSha))
                {
                    outError = "SHA256 mismatch for " + archivePath.string();
                    return false;
                }
            }

            outError.clear();
            return true;
        }

        bool ExtractZipArchive(const std::filesystem::path& archivePath, const std::filesystem::path& destination, std::string& outError)
        {
            mz_zip_archive archive {};
            if (!mz_zip_reader_init_file(&archive, archivePath.string().c_str(), 0))
            {
                outError = "Failed to open archive: " + archivePath.string();
                return false;
            }

            std::error_code ec;
            std::filesystem::create_directories(destination, ec);
            if (ec)
            {
                mz_zip_reader_end(&archive);
                outError = "Failed to create extraction folder: " + destination.string();
                return false;
            }

            const int fileCount = static_cast<int>(mz_zip_reader_get_num_files(&archive));
            for (int i = 0; i < fileCount; ++i)
            {
                mz_zip_archive_file_stat stat {};
                if (!mz_zip_reader_file_stat(&archive, static_cast<mz_uint>(i), &stat))
                {
                    mz_zip_reader_end(&archive);
                    outError = "Failed to read archive entry metadata.";
                    return false;
                }

                const std::filesystem::path relativePath = stat.m_filename;
                if (relativePath.empty())
                {
                    continue;
                }

                const std::filesystem::path outputPath = destination / relativePath;
                const std::string entryName = stat.m_filename != nullptr ? std::string(stat.m_filename) : std::string {};
                const bool isDirectoryEntry =
                    mz_zip_reader_is_file_a_directory(&archive, static_cast<mz_uint>(i)) ||
                    (!entryName.empty() && (entryName.back() == '/' || entryName.back() == '\\'));
                if (isDirectoryEntry)
                {
                    std::filesystem::create_directories(outputPath, ec);
                    if (ec)
                    {
                        mz_zip_reader_end(&archive);
                        outError = "Failed to create output directory: " + outputPath.string();
                        return false;
                    }
                    continue;
                }

                std::filesystem::create_directories(outputPath.parent_path(), ec);
                if (ec)
                {
                    mz_zip_reader_end(&archive);
                    outError = "Failed to create output directory: " + outputPath.parent_path().string();
                    return false;
                }

                if (!mz_zip_reader_extract_to_file(
                        &archive,
                        static_cast<mz_uint>(i),
                        outputPath.string().c_str(),
                        0))
                {
                    mz_zip_reader_end(&archive);
                    outError = "Failed to extract file: " + outputPath.string();
                    return false;
                }
            }

            mz_zip_reader_end(&archive);
            outError.clear();
            return true;
        }

        bool ReadPackageIdentityFromArchive(
            const std::filesystem::path& archivePath,
            std::string& outPackageId,
            std::string& outVersion,
            std::string& outError)
        {
            mz_zip_archive archive {};
            if (!mz_zip_reader_init_file(&archive, archivePath.string().c_str(), 0))
            {
                outError = "Failed to open package archive: " + archivePath.string();
                return false;
            }

            const int entryIndex = mz_zip_reader_locate_file(&archive, "package.json", nullptr, 0);
            if (entryIndex < 0)
            {
                mz_zip_reader_end(&archive);
                outError = "package.json missing inside archive.";
                return false;
            }

            size_t uncompressedSize = 0;
            void* memory = mz_zip_reader_extract_to_heap(
                &archive,
                static_cast<mz_uint>(entryIndex),
                &uncompressedSize,
                0);
            if (memory == nullptr || uncompressedSize == 0)
            {
                mz_zip_reader_end(&archive);
                outError = "Failed to extract package.json from archive.";
                return false;
            }

            std::string jsonText(reinterpret_cast<const char*>(memory), uncompressedSize);
            mz_free(memory);
            mz_zip_reader_end(&archive);

            try
            {
                const json packageJson = json::parse(jsonText);
                outPackageId = packageJson.value("name", "");
                outVersion = packageJson.value("version", "");
            }
            catch (const std::exception& ex)
            {
                outError = std::string("Invalid package.json in archive: ") + ex.what();
                return false;
            }

            if (outPackageId.empty() || outVersion.empty())
            {
                outError = "Archive package.json requires fields: name, version.";
                return false;
            }

            outError.clear();
            return true;
        }

        bool IsVersionGreater(const std::string& left, const std::string& right)
        {
            SemVer lhs {};
            SemVer rhs {};
            if (!TryParseSemVer(left, lhs))
            {
                return false;
            }
            if (!TryParseSemVer(right, rhs))
            {
                return true;
            }
            return CompareSemVer(lhs, rhs) > 0;
        }

        std::string BuildArchiveFileName(const std::string& packageId, const std::string& version)
        {
            return packageId + "-" + version + ".zip";
        }

        std::optional<std::filesystem::path> AsLocalPathIfAny(const std::string& value)
        {
            if (value.empty() || IsHttpUrl(value))
            {
                return std::nullopt;
            }
            return std::filesystem::path(value);
        }

        std::filesystem::path MakeRelativeToInstallRoot(
            const std::filesystem::path& installRoot,
            const std::filesystem::path& candidate)
        {
            if (candidate.empty())
            {
                return {};
            }
            if (!candidate.is_absolute())
            {
                return candidate.lexically_normal();
            }

            std::error_code ec;
            std::filesystem::path relative = std::filesystem::relative(candidate, installRoot, ec);
            if (!ec && !relative.empty())
            {
                return relative.lexically_normal();
            }

            relative = candidate.lexically_relative(installRoot);
            if (!relative.empty())
            {
                return relative.lexically_normal();
            }

            return candidate.lexically_normal();
        }

        bool InstalledPackageMetadataEquals(
            const InstalledPackageRecord& lhs,
            const InstalledPackageRecord& rhs)
        {
            return lhs.displayName == rhs.displayName &&
                lhs.description == rhs.description &&
                lhs.editorTool.enabled == rhs.editorTool.enabled &&
                lhs.editorTool.displayName == rhs.editorTool.displayName &&
                lhs.editorTool.entryRelativePath.lexically_normal().generic_string() ==
                    rhs.editorTool.entryRelativePath.lexically_normal().generic_string() &&
                lhs.editorTool.workingDirectoryRelativePath.lexically_normal().generic_string() ==
                    rhs.editorTool.workingDirectoryRelativePath.lexically_normal().generic_string();
        }

        void PopulateInstalledPackageMetadataFromPackageJson(
            const json& packageJson,
            InstalledPackageRecord& record,
            bool& outHasExplicitEditorTool)
        {
            outHasExplicitEditorTool = false;

            if (packageJson.contains("displayName") && packageJson["displayName"].is_string())
            {
                record.displayName = packageJson["displayName"].get<std::string>();
            }
            if (packageJson.contains("description") && packageJson["description"].is_string())
            {
                record.description = packageJson["description"].get<std::string>();
            }

            if (!packageJson.contains("editorTool") || !packageJson["editorTool"].is_object())
            {
                return;
            }

            outHasExplicitEditorTool = true;
            const json& editorTool = packageJson["editorTool"];
            record.editorTool.enabled = editorTool.value("enabled", true);

            if (editorTool.contains("displayName") && editorTool["displayName"].is_string())
            {
                record.editorTool.displayName = editorTool["displayName"].get<std::string>();
            }
            if (editorTool.contains("entry") && editorTool["entry"].is_string())
            {
                record.editorTool.entryRelativePath =
                    std::filesystem::path(editorTool["entry"].get<std::string>()).lexically_normal();
            }
            if (editorTool.contains("workingDirectory") && editorTool["workingDirectory"].is_string())
            {
                record.editorTool.workingDirectoryRelativePath =
                    std::filesystem::path(editorTool["workingDirectory"].get<std::string>()).lexically_normal();
            }

            if (record.editorTool.enabled &&
                record.editorTool.workingDirectoryRelativePath.empty() &&
                !record.editorTool.entryRelativePath.empty())
            {
                record.editorTool.workingDirectoryRelativePath =
                    record.editorTool.entryRelativePath.parent_path().lexically_normal();
            }
            if (record.editorTool.enabled && record.editorTool.displayName.empty())
            {
                record.editorTool.displayName =
                    !record.displayName.empty() ? record.displayName : record.package.id;
            }
        }

        bool TryDetectInstalledEditorToolConvention(InstalledPackageRecord& record)
        {
            if (record.installPath.empty())
            {
                return false;
            }

            const std::filesystem::path editorRoot = record.installPath / "Editor";
            std::error_code ec;
            if (!std::filesystem::exists(editorRoot, ec) || ec)
            {
                return false;
            }

            std::vector<std::filesystem::path> executablePaths;
            std::filesystem::recursive_directory_iterator iterator(
                editorRoot,
                std::filesystem::directory_options::skip_permission_denied,
                ec);
            if (ec)
            {
                return false;
            }

            for (const auto& entry : iterator)
            {
                const bool isRegularFile = entry.is_regular_file(ec);
                if (ec || !isRegularFile)
                {
                    ec.clear();
                    continue;
                }

                if (ToLower(entry.path().extension().string()) == ".exe")
                {
                    executablePaths.push_back(entry.path());
                }
            }

            if (executablePaths.empty())
            {
                return false;
            }

            std::sort(executablePaths.begin(), executablePaths.end());
            const std::filesystem::path chosenExecutable = executablePaths.front();

            record.editorTool.enabled = true;
            if (record.editorTool.displayName.empty())
            {
                record.editorTool.displayName =
                    !record.displayName.empty() ? record.displayName : record.package.id;
            }
            record.editorTool.entryRelativePath = MakeRelativeToInstallRoot(record.installPath, chosenExecutable);
            record.editorTool.workingDirectoryRelativePath =
                MakeRelativeToInstallRoot(record.installPath, chosenExecutable.parent_path());
            return !record.editorTool.entryRelativePath.empty();
        }

        bool HydrateInstalledPackageMetadata(InstalledPackageRecord& record, std::string& outWarning)
        {
            outWarning.clear();
            const InstalledPackageRecord before = record;

            bool hasExplicitEditorTool = false;
            if (!record.installPath.empty())
            {
                const std::filesystem::path packageManifestPath = record.installPath / "package.json";
                std::error_code existsEc;
                if (std::filesystem::exists(packageManifestPath, existsEc) && !existsEc)
                {
                    json packageJson;
                    if (std::string error; ReadJsonFile(packageManifestPath, packageJson, error))
                    {
                        PopulateInstalledPackageMetadataFromPackageJson(packageJson, record, hasExplicitEditorTool);
                    }
                    else
                    {
                        outWarning = error;
                    }
                }
            }

            if (!hasExplicitEditorTool &&
                (!record.editorTool.enabled || record.editorTool.entryRelativePath.empty()))
            {
                TryDetectInstalledEditorToolConvention(record);
            }

            if (record.editorTool.enabled)
            {
                if (record.editorTool.displayName.empty())
                {
                    record.editorTool.displayName =
                        !record.displayName.empty() ? record.displayName : record.package.id;
                }
                if (record.editorTool.workingDirectoryRelativePath.empty() &&
                    !record.editorTool.entryRelativePath.empty())
                {
                    record.editorTool.workingDirectoryRelativePath =
                        record.editorTool.entryRelativePath.parent_path().lexically_normal();
                }
            }

            return !InstalledPackageMetadataEquals(before, record);
        }

        class JsonManifestStore final : public IPackageManifestStore
        {
        public:
            bool Load(
                const std::filesystem::path& packagesRoot,
                PackageManifest& outManifest,
                PackageLock& outLock,
                bool& outMigratedLegacyLock,
                std::string& outError) override;

            bool Save(
                const std::filesystem::path& packagesRoot,
                const PackageManifest& manifest,
                const PackageLock& lock,
                std::string& outError) override;
        };

        class CompositeRegistryProvider final : public IPackageRegistryProvider
        {
        public:
            bool FetchIndex(
                const std::vector<PackageRegistrySource>& sources,
                std::vector<PackageCatalogEntry>& outEntries,
                std::string& outError) override;

            bool FetchManifest(
                const std::vector<PackageRegistrySource>& sources,
                const std::string& packageId,
                std::vector<PackageVersionInfo>& outVersions,
                std::string& outError) override;

        private:
            static bool FetchIndexFromSource(
                const PackageRegistrySource& source,
                std::vector<PackageCatalogEntry>& outEntries,
                std::string& outError);
            static bool FetchManifestFromSource(
                const PackageRegistrySource& source,
                const std::string& packageId,
                std::vector<PackageVersionInfo>& outVersions,
                std::string& outError);
            static bool LoadJsonDocument(
                const PackageRegistrySource& source,
                const std::string& relativePath,
                json& outJson,
                std::string& outError);
        };

        class BasicResolver final : public IPackageResolver
        {
        public:
            PackageInstallPlan ResolveInstallPlan(
                const PackageInstallRequest& request,
                const std::vector<InstalledPackageRecord>& installedPackages,
                const std::vector<PackageCatalogEntry>& catalog,
                const std::unordered_map<std::string, std::vector<PackageVersionInfo>>& manifestCache,
                const std::string& engineVersionNormalized) override;

        private:
            static bool IsEngineCompatible(const PackageVersionInfo& version, const std::string& engineVersion);
        };

        class TransactionalInstaller final : public IPackageInstaller
        {
        public:
            PackageOperationResult Install(
                const PackageInstallPlan& plan,
                const std::filesystem::path& globalCacheRoot,
                const std::filesystem::path& projectCacheRoot,
                const std::filesystem::path& projectInstallRoot,
                ProgressCallback progressCallback) override;

            PackageOperationResult Remove(
                const std::string& packageId,
                const std::vector<InstalledPackageRecord>& installed,
                const std::filesystem::path& projectInstallRoot,
                ProgressCallback progressCallback) override;

            PackageOperationResult VerifyInstalled(
                const std::string& packageIdOrEmpty,
                const std::vector<InstalledPackageRecord>& installed,
                ProgressCallback progressCallback) override;

        private:
            static bool AcquireArchive(
                const PackageInstallAction& action,
                const std::filesystem::path& globalCacheRoot,
                const std::filesystem::path& projectCacheRoot,
                std::filesystem::path& outArchivePath,
                std::string& outError);
            static void CleanupStaging(const std::filesystem::path& stagingRoot);
            static void Rollback(const std::vector<std::pair<std::filesystem::path, std::filesystem::path>>& movedPairs);
        };

        class PackageMountService final : public IPackageMountService
        {
        public:
            void Refresh(const std::vector<InstalledPackageRecord>& installed) override;
            std::vector<PackageMountRoot> GetMountRoots() const override;

        private:
            void AppendMount(const InstalledPackageRecord& package, const std::string& kind);

            std::vector<PackageMountRoot> m_MountRoots;
        };

        class ConventionModuleLoader final : public IPackageModuleLoader
        {
        public:
            bool TryHotLoadPackage(
                const std::string& packageId,
                const std::filesystem::path& installRoot,
                std::vector<std::string>& outWarnings) override;

            bool UnloadPackage(
                const std::string& packageId,
                std::vector<std::string>& outWarnings) override;

        private:
            static void CollectModules(const std::filesystem::path& directory, std::vector<std::filesystem::path>& outPaths);

            std::unordered_map<std::string, std::vector<DynamicLibrary>> m_LoadedModules;
        };

        bool JsonManifestStore::Load(
            const std::filesystem::path& packagesRoot,
            PackageManifest& outManifest,
            PackageLock& outLock,
            bool& outMigratedLegacyLock,
            std::string& outError)
        {
            outManifest = PackageManifest {};
            outLock = PackageLock {};
            outMigratedLegacyLock = false;

            std::error_code ec;
            std::filesystem::create_directories(packagesRoot, ec);
            if (ec)
            {
                outError = "Failed to create packages directory: " + packagesRoot.string();
                return false;
            }

            const std::filesystem::path manifestPath = packagesRoot / "manifest.json";
            const std::filesystem::path lockPath = packagesRoot / "lock.json";
            const std::filesystem::path legacyLockPath = packagesRoot / "Packages.lock";

            if (std::filesystem::exists(manifestPath))
            {
                json value;
                if (!ReadJsonFile(manifestPath, value, outError))
                {
                    return false;
                }

                outManifest.schemaVersion = value.value("schemaVersion", 1);
                if (value.contains("sources") && value["sources"].is_array())
                {
                    for (const auto& source : value["sources"])
                    {
                        PackageRegistrySource parsedSource;
                        parsedSource.id = source.value("id", "");
                        parsedSource.location = source.value("location", "");
                        parsedSource.enabled = source.value("enabled", true);
                        parsedSource.preferLocal = source.value("preferLocal", false);
                        if (!parsedSource.id.empty() && !parsedSource.location.empty())
                        {
                            outManifest.sources.push_back(std::move(parsedSource));
                        }
                    }
                }

                if (value.contains("dependencies") && value["dependencies"].is_array())
                {
                    for (const auto& dependency : value["dependencies"])
                    {
                        PackageRequirement requirement;
                        requirement.id = dependency.value("id", "");
                        requirement.versionConstraint = dependency.value("versionConstraint", "");
                        if (!requirement.id.empty())
                        {
                            outManifest.dependencies.push_back(std::move(requirement));
                        }
                    }
                }
            }

            if (std::filesystem::exists(lockPath))
            {
                json value;
                if (!ReadJsonFile(lockPath, value, outError))
                {
                    return false;
                }

                outLock.schemaVersion = value.value("schemaVersion", 1);
                if (value.contains("packages") && value["packages"].is_array())
                {
                    for (const auto& package : value["packages"])
                    {
                        InstalledPackageRecord record;
                        record.package.id = package.value("id", "");
                        record.package.version = package.value("version", "");
                        record.source = package.value("source", "");
                        record.sha256 = package.value("sha256", "");
                        record.sizeBytes = package.value("sizeBytes", static_cast<std::uint64_t>(0));
                        record.installPath = package.value("installPath", "");
                        record.cachePath = package.value("cachePath", "");
                        record.displayName = package.value("displayName", "");
                        record.description = package.value("description", "");
                        record.restartRequired = package.value("restartRequired", false);
                        if (package.contains("editorTool") && package["editorTool"].is_object())
                        {
                            const auto& editorTool = package["editorTool"];
                            record.editorTool.enabled = editorTool.value("enabled", false);
                            record.editorTool.displayName = editorTool.value("displayName", "");
                            record.editorTool.entryRelativePath = editorTool.value("entryRelativePath", "");
                            record.editorTool.workingDirectoryRelativePath =
                                editorTool.value("workingDirectoryRelativePath", "");
                        }
                        if (package.contains("dependencies") && package["dependencies"].is_array())
                        {
                            for (const auto& dep : package["dependencies"])
                            {
                                PackageDependency dependency;
                                dependency.id = dep.value("id", "");
                                dependency.constraint = dep.value("constraint", "");
                                if (!dependency.id.empty())
                                {
                                    record.dependencies.push_back(std::move(dependency));
                                }
                            }
                        }
                        if (!record.package.id.empty() && !record.package.version.empty())
                        {
                            outLock.packages.push_back(std::move(record));
                        }
                    }
                }

                if (value.contains("legacyEntries") && value["legacyEntries"].is_array())
                {
                    for (const auto& entry : value["legacyEntries"])
                    {
                        if (entry.is_string())
                        {
                            outLock.legacyEntries.push_back(entry.get<std::string>());
                        }
                    }
                }
            }
            else if (std::filesystem::exists(legacyLockPath))
            {
                std::ifstream input(legacyLockPath);
                std::string line;
                while (std::getline(input, line))
                {
                    line = Trim(line);
                    if (!line.empty())
                    {
                        outLock.legacyEntries.push_back(line);
                    }
                }
                outMigratedLegacyLock = !outLock.legacyEntries.empty();
            }

            outError.clear();
            return true;
        }

        bool JsonManifestStore::Save(
            const std::filesystem::path& packagesRoot,
            const PackageManifest& manifest,
            const PackageLock& lock,
            std::string& outError)
        {
            const std::filesystem::path manifestPath = packagesRoot / "manifest.json";
            const std::filesystem::path lockPath = packagesRoot / "lock.json";

            json manifestJson;
            manifestJson["schemaVersion"] = manifest.schemaVersion;
            manifestJson["sources"] = json::array();
            for (const PackageRegistrySource& source : manifest.sources)
            {
                manifestJson["sources"].push_back({
                    { "id", source.id },
                    { "location", source.location },
                    { "enabled", source.enabled },
                    { "preferLocal", source.preferLocal }
                });
            }

            manifestJson["dependencies"] = json::array();
            for (const PackageRequirement& dependency : manifest.dependencies)
            {
                manifestJson["dependencies"].push_back({
                    { "id", dependency.id },
                    { "versionConstraint", dependency.versionConstraint }
                });
            }

            json lockJson;
            lockJson["schemaVersion"] = lock.schemaVersion;
            lockJson["packages"] = json::array();
            for (const InstalledPackageRecord& package : lock.packages)
            {
                json dependencies = json::array();
                for (const PackageDependency& dependency : package.dependencies)
                {
                    dependencies.push_back({
                        { "id", dependency.id },
                        { "constraint", dependency.constraint }
                    });
                }

                lockJson["packages"].push_back({
                    { "id", package.package.id },
                    { "version", package.package.version },
                    { "source", package.source },
                    { "sha256", package.sha256 },
                    { "sizeBytes", package.sizeBytes },
                    { "installPath", package.installPath.generic_string() },
                    { "cachePath", package.cachePath.generic_string() },
                    { "displayName", package.displayName },
                    { "description", package.description },
                    { "restartRequired", package.restartRequired },
                    { "dependencies", dependencies },
                    { "editorTool", {
                        { "enabled", package.editorTool.enabled },
                        { "displayName", package.editorTool.displayName },
                        { "entryRelativePath", package.editorTool.entryRelativePath.generic_string() },
                        { "workingDirectoryRelativePath",
                            package.editorTool.workingDirectoryRelativePath.generic_string() }
                    } }
                });
            }
            lockJson["legacyEntries"] = lock.legacyEntries;

            if (!WriteJsonFile(manifestPath, manifestJson, outError))
            {
                return false;
            }
            if (!WriteJsonFile(lockPath, lockJson, outError))
            {
                return false;
            }

            outError.clear();
            return true;
        }

        bool CompositeRegistryProvider::LoadJsonDocument(
            const PackageRegistrySource& source,
            const std::string& relativePath,
            json& outJson,
            std::string& outError)
        {
            if (source.location.empty())
            {
                outError = "Registry source location is empty for '" + source.id + "'.";
                return false;
            }

            if (IsHttpUrl(source.location))
            {
                std::filesystem::path tempPath =
                    std::filesystem::temp_directory_path() /
                    ("luma_registry_" + ToLower(source.id) + "_" + std::to_string(GetTickCount64()) + ".json");
                const std::string fullUrl = JoinUrl(source.location, relativePath);
                if (!DownloadFile(fullUrl, tempPath, outError))
                {
                    return false;
                }

                const bool readResult = ReadJsonFile(tempPath, outJson, outError);
                std::error_code cleanupEc;
                std::filesystem::remove(tempPath, cleanupEc);
                return readResult;
            }

            const std::filesystem::path root(source.location);
            const std::filesystem::path documentPath = root / std::filesystem::path(relativePath);
            if (!std::filesystem::exists(documentPath))
            {
                outError = "Registry document not found: " + documentPath.string();
                return false;
            }

            return ReadJsonFile(documentPath, outJson, outError);
        }

        std::vector<PackageDependency> ParseDependencies(const json& value)
        {
            std::vector<PackageDependency> dependencies;
            if (value.is_object())
            {
                for (auto it = value.begin(); it != value.end(); ++it)
                {
                    PackageDependency dependency;
                    dependency.id = it.key();
                    if (it.value().is_string())
                    {
                        dependency.constraint = it.value().get<std::string>();
                    }
                    if (!dependency.id.empty())
                    {
                        dependencies.push_back(std::move(dependency));
                    }
                }
            }
            else if (value.is_array())
            {
                for (const auto& item : value)
                {
                    if (!item.is_object())
                    {
                        continue;
                    }

                    PackageDependency dependency;
                    dependency.id = item.value("id", "");
                    dependency.constraint = item.value("constraint", item.value("version", ""));
                    if (!dependency.id.empty())
                    {
                        dependencies.push_back(std::move(dependency));
                    }
                }
            }

            std::sort(
                dependencies.begin(),
                dependencies.end(),
                [](const PackageDependency& lhs, const PackageDependency& rhs)
                {
                    return lhs.id < rhs.id;
                });
            return dependencies;
        }

        bool CompositeRegistryProvider::FetchIndexFromSource(
            const PackageRegistrySource& source,
            std::vector<PackageCatalogEntry>& outEntries,
            std::string& outError)
        {
            json indexJson;
            if (!LoadJsonDocument(source, "index.json", indexJson, outError))
            {
                return false;
            }

            outEntries.clear();
            if (!indexJson.contains("packages") || !indexJson["packages"].is_object())
            {
                outError = "index.json missing object field 'packages'.";
                return false;
            }

            const json& packagesByCategory = indexJson["packages"];
            for (auto categoryIt = packagesByCategory.begin(); categoryIt != packagesByCategory.end(); ++categoryIt)
            {
                const std::string category = categoryIt.key();
                if (!categoryIt.value().is_object())
                {
                    continue;
                }

                for (auto packageIt = categoryIt.value().begin(); packageIt != categoryIt.value().end(); ++packageIt)
                {
                    if (!packageIt.value().is_object())
                    {
                        continue;
                    }

                    PackageCatalogEntry entry;
                    entry.id = packageIt.key();
                    entry.category = category;
                    entry.latest = packageIt.value().value("latest", "");
                    if (packageIt.value().contains("versions") && packageIt.value()["versions"].is_array())
                    {
                        for (const auto& versionNode : packageIt.value()["versions"])
                        {
                            if (versionNode.is_string())
                            {
                                entry.versions.push_back(versionNode.get<std::string>());
                            }
                        }
                    }
                    if (!entry.id.empty())
                    {
                        outEntries.push_back(std::move(entry));
                    }
                }
            }

            outError.clear();
            return true;
        }

        bool CompositeRegistryProvider::FetchManifestFromSource(
            const PackageRegistrySource& source,
            const std::string& packageId,
            std::vector<PackageVersionInfo>& outVersions,
            std::string& outError)
        {
            const std::string relativePath = "manifests/" + packageId + "/index.json";
            json manifestJson;
            if (!LoadJsonDocument(source, relativePath, manifestJson, outError))
            {
                return false;
            }

            outVersions.clear();
            if (!manifestJson.contains("versions") || !manifestJson["versions"].is_array())
            {
                outError = "Manifest for '" + packageId + "' missing versions array.";
                return false;
            }

            for (const auto& versionNode : manifestJson["versions"])
            {
                if (!versionNode.is_object())
                {
                    continue;
                }

                PackageVersionInfo info;
                info.version = versionNode.value("version", "");
                info.url = versionNode.value("url", "");
                info.sha256 = versionNode.value("sha256", versionNode.value("shasum", ""));
                info.sizeBytes = versionNode.value("sizeBytes", versionNode.value("size", static_cast<std::uint64_t>(0)));
                info.description = versionNode.value("description", "");
                info.published = versionNode.value("published", "");
                info.category = versionNode.value("category", "");
                if (versionNode.contains("dependencies"))
                {
                    info.dependencies = ParseDependencies(versionNode["dependencies"]);
                }
                if (versionNode.contains("engineVersion") && versionNode["engineVersion"].is_object())
                {
                    const json& engineNode = versionNode["engineVersion"];
                    info.engineMin = engineNode.value("min", "");
                    info.engineMax = engineNode.value("max", "");
                }
                else if (versionNode.contains("engineVersion") && versionNode["engineVersion"].is_string())
                {
                    info.engineConstraint = versionNode["engineVersion"].get<std::string>();
                }
                if (versionNode.contains("engine") && versionNode["engine"].is_object())
                {
                    info.engineConstraint = versionNode["engine"].value("luma", "");
                }

                const std::string archiveName = BuildArchiveFileName(packageId, info.version);
                const std::filesystem::path archiveRelativePath =
                    std::filesystem::path("packages") / packageId / info.version / archiveName;
                const bool sourceIsHttp = IsHttpUrl(source.location);
                const std::string canonicalSourceArchiveUrl =
                    sourceIsHttp
                        ? JoinUrl(source.location, archiveRelativePath.generic_string())
                        : (std::filesystem::path(source.location) / archiveRelativePath).lexically_normal().string();

                if (!sourceIsHttp)
                {
                    info.url = canonicalSourceArchiveUrl;
                }
                else if (!info.url.empty() && !IsHttpUrl(info.url))
                {
                    if (std::filesystem::path(info.url).is_relative())
                    {
                        info.url = JoinUrl(source.location, info.url);
                    }
                }
                else if (info.url.empty() || !StartsWithInsensitive(info.url, source.location))
                {
                    // Registry source configuration is authoritative. If a manifest carries a stale absolute URL,
                    // rebuild the archive URL from the currently selected source root.
                    info.url = canonicalSourceArchiveUrl;
                }

                if (!info.version.empty())
                {
                    outVersions.push_back(std::move(info));
                }
            }

            std::sort(
                outVersions.begin(),
                outVersions.end(),
                [](const PackageVersionInfo& lhs, const PackageVersionInfo& rhs)
                {
                    return IsVersionGreater(lhs.version, rhs.version);
                });

            outError.clear();
            return !outVersions.empty();
        }

        bool CompositeRegistryProvider::FetchIndex(
            const std::vector<PackageRegistrySource>& sources,
            std::vector<PackageCatalogEntry>& outEntries,
            std::string& outError)
        {
            outEntries.clear();

            std::unordered_map<std::string, PackageCatalogEntry> merged;
            std::vector<std::string> sourceErrors;
            bool anySuccess = false;

            for (const PackageRegistrySource& source : sources)
            {
                if (!source.enabled)
                {
                    continue;
                }

                std::vector<PackageCatalogEntry> sourceEntries;
                std::string sourceError;
                if (!FetchIndexFromSource(source, sourceEntries, sourceError))
                {
                    sourceErrors.push_back(source.id + ": " + sourceError);
                    continue;
                }

                anySuccess = true;
                for (const PackageCatalogEntry& entry : sourceEntries)
                {
                    auto it = merged.find(entry.id);
                    if (it == merged.end())
                    {
                        merged.emplace(entry.id, entry);
                        continue;
                    }

                    PackageCatalogEntry& mergedEntry = it->second;
                    if (mergedEntry.category.empty() && !entry.category.empty())
                    {
                        mergedEntry.category = entry.category;
                    }
                    if (IsVersionGreater(entry.latest, mergedEntry.latest))
                    {
                        mergedEntry.latest = entry.latest;
                    }
                    for (const std::string& version : entry.versions)
                    {
                        if (std::find(mergedEntry.versions.begin(), mergedEntry.versions.end(), version) == mergedEntry.versions.end())
                        {
                            mergedEntry.versions.push_back(version);
                        }
                    }
                    std::sort(
                        mergedEntry.versions.begin(),
                        mergedEntry.versions.end(),
                        [](const std::string& lhs, const std::string& rhs)
                        {
                            return IsVersionGreater(lhs, rhs);
                        });
                }
            }

            if (!anySuccess)
            {
                std::ostringstream stream;
                stream << "Failed to fetch package index from all registry sources.";
                if (!sourceErrors.empty())
                {
                    stream << " Errors: ";
                    for (const std::string& err : sourceErrors)
                    {
                        stream << "[" << err << "] ";
                    }
                }
                outError = stream.str();
                return false;
            }

            outEntries.reserve(merged.size());
            for (auto& [_, entry] : merged)
            {
                outEntries.push_back(std::move(entry));
            }
            std::sort(
                outEntries.begin(),
                outEntries.end(),
                [](const PackageCatalogEntry& lhs, const PackageCatalogEntry& rhs)
                {
                    return lhs.id < rhs.id;
                });

            outError.clear();
            return true;
        }

        bool CompositeRegistryProvider::FetchManifest(
            const std::vector<PackageRegistrySource>& sources,
            const std::string& packageId,
            std::vector<PackageVersionInfo>& outVersions,
            std::string& outError)
        {
            outVersions.clear();
            std::vector<std::string> sourceErrors;

            for (const PackageRegistrySource& source : sources)
            {
                if (!source.enabled)
                {
                    continue;
                }

                std::vector<PackageVersionInfo> sourceVersions;
                std::string sourceError;
                if (!FetchManifestFromSource(source, packageId, sourceVersions, sourceError))
                {
                    sourceErrors.push_back(source.id + ": " + sourceError);
                    continue;
                }

                outVersions = std::move(sourceVersions);
                outError.clear();
                return true;
            }

            std::ostringstream stream;
            stream << "Failed to fetch manifest for '" << packageId << "' from all sources.";
            if (!sourceErrors.empty())
            {
                stream << " Errors: ";
                for (const std::string& err : sourceErrors)
                {
                    stream << "[" << err << "] ";
                }
            }
            outError = stream.str();
            return false;
        }

        bool BasicResolver::IsEngineCompatible(const PackageVersionInfo& version, const std::string& engineVersion)
        {
            if (!version.engineConstraint.empty())
            {
                return SatisfiesVersionConstraint(engineVersion, version.engineConstraint);
            }

            SemVer current {};
            if (!TryParseSemVer(engineVersion, current))
            {
                return false;
            }

            if (!version.engineMin.empty())
            {
                SemVer minVersion {};
                if (!TryParseSemVer(version.engineMin, minVersion) || CompareSemVer(current, minVersion) < 0)
                {
                    return false;
                }
            }
            if (!version.engineMax.empty())
            {
                SemVer maxVersion {};
                if (!TryParseSemVer(version.engineMax, maxVersion) || CompareSemVer(current, maxVersion) > 0)
                {
                    return false;
                }
            }
            return true;
        }

        PackageInstallPlan BasicResolver::ResolveInstallPlan(
            const PackageInstallRequest& request,
            const std::vector<InstalledPackageRecord>& installedPackages,
            const std::vector<PackageCatalogEntry>& catalog,
            const std::unordered_map<std::string, std::vector<PackageVersionInfo>>& manifestCache,
            const std::string& engineVersionNormalized)
        {
            PackageInstallPlan plan;
            plan.success = false;

            if (request.id.empty())
            {
                plan.message = "Install request requires a package id.";
                return plan;
            }

            std::unordered_map<std::string, const PackageVersionInfo*> resolved;
            std::unordered_set<std::string> activeStack;

            auto chooseVersion = [&engineVersionNormalized](
                                     const std::vector<PackageVersionInfo>& versions,
                                     const std::string& constraint,
                                     const std::string& packageId,
                                     std::vector<std::string>& diagnostics) -> const PackageVersionInfo* {
                const PackageVersionInfo* best = nullptr;
                for (const PackageVersionInfo& version : versions)
                {
                    if (!BasicResolver::IsEngineCompatible(version, engineVersionNormalized))
                    {
                        continue;
                    }
                    if (!constraint.empty() && !SatisfiesVersionConstraint(version.version, constraint))
                    {
                        continue;
                    }
                    if (best == nullptr || IsVersionGreater(version.version, best->version))
                    {
                        best = &version;
                    }
                }
                if (best == nullptr)
                {
                    diagnostics.push_back("No compatible version found for '" + packageId + "' with constraint '" + constraint + "'.");
                }
                return best;
            };

            std::function<bool(const std::string&, const std::string&, std::vector<std::string>&)> resolvePackage;
            resolvePackage = [&](const std::string& packageId, const std::string& constraint, std::vector<std::string>& diagnostics) -> bool {
                auto versionsIt = manifestCache.find(packageId);
                if (versionsIt == manifestCache.end())
                {
                    diagnostics.push_back("Missing manifest cache entry for dependency '" + packageId + "'.");
                    return false;
                }

                if (activeStack.contains(packageId))
                {
                    diagnostics.push_back("Dependency cycle detected at package '" + packageId + "'.");
                    return false;
                }

                auto resolvedIt = resolved.find(packageId);
                if (resolvedIt != resolved.end())
                {
                    if (!constraint.empty() && !SatisfiesVersionConstraint(resolvedIt->second->version, constraint))
                    {
                        diagnostics.push_back(
                            "Dependency conflict for '" + packageId + "': already resolved to " + resolvedIt->second->version +
                            " but required " + constraint + ".");
                        return false;
                    }
                    return true;
                }

                const PackageVersionInfo* chosen = chooseVersion(versionsIt->second, constraint, packageId, diagnostics);
                if (chosen == nullptr)
                {
                    return false;
                }

                resolved.emplace(packageId, chosen);
                activeStack.insert(packageId);
                for (const PackageDependency& dependency : chosen->dependencies)
                {
                    if (!resolvePackage(dependency.id, dependency.constraint, diagnostics))
                    {
                        activeStack.erase(packageId);
                        return false;
                    }
                }
                activeStack.erase(packageId);
                return true;
            };

            std::string rootConstraint = request.version;
            if (rootConstraint.empty())
            {
                const auto catalogIt = std::find_if(
                    catalog.begin(),
                    catalog.end(),
                    [&request](const PackageCatalogEntry& entry)
                    {
                        return entry.id == request.id;
                    });
                if (catalogIt != catalog.end() && !catalogIt->latest.empty())
                {
                    rootConstraint = catalogIt->latest;
                }
            }

            if (!resolvePackage(request.id, rootConstraint, plan.diagnostics))
            {
                plan.message = "Dependency resolution failed.";
                return plan;
            }

            std::vector<std::string> packageIds;
            packageIds.reserve(resolved.size());
            for (const auto& [packageId, _] : resolved)
            {
                packageIds.push_back(packageId);
            }
            std::sort(packageIds.begin(), packageIds.end());

            for (const std::string& packageId : packageIds)
            {
                const PackageVersionInfo* versionInfo = resolved[packageId];
                PackageInstallAction action;
                action.package.id = packageId;
                action.package.version = versionInfo->version;
                action.versionInfo = *versionInfo;
                action.alreadyInstalled = std::any_of(
                    installedPackages.begin(),
                    installedPackages.end(),
                    [&action](const InstalledPackageRecord& installed)
                    {
                        return installed.package.id == action.package.id &&
                            installed.package.version == action.package.version;
                    });
                plan.actions.push_back(std::move(action));
            }

            plan.success = true;
            const std::size_t installCount = static_cast<std::size_t>(std::count_if(
                plan.actions.begin(),
                plan.actions.end(),
                [](const PackageInstallAction& action)
                {
                    return !action.alreadyInstalled;
                }));
            if (installCount == 0)
            {
                plan.message = "All resolved packages are already installed.";
            }
            else
            {
                plan.message = "Resolved " + std::to_string(plan.actions.size()) + " package(s), " +
                    std::to_string(installCount) + " require install/update.";
            }

            return plan;
        }

        bool MoveOrCopyDirectory(const std::filesystem::path& source, const std::filesystem::path& destination, std::string& outError)
        {
            std::error_code ec;
            std::filesystem::rename(source, destination, ec);
            if (!ec)
            {
                outError.clear();
                return true;
            }

            std::filesystem::create_directories(destination.parent_path(), ec);
            if (ec)
            {
                outError = "Failed to prepare destination parent: " + destination.parent_path().string();
                return false;
            }

            std::filesystem::copy(
                source,
                destination,
                std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                ec);
            if (ec)
            {
                outError = "Failed to copy directory from " + source.string() + " to " + destination.string();
                return false;
            }

            std::filesystem::remove_all(source, ec);
            outError.clear();
            return true;
        }

        bool AcquireArchiveForAction(
            const PackageInstallAction& action,
            const std::filesystem::path& globalCacheRoot,
            const std::filesystem::path& projectCacheRoot,
            std::filesystem::path& outArchivePath,
            std::string& outError)
        {
            auto validateLocalArchive = [&](const std::filesystem::path& path) -> bool
            {
                if (!VerifyArchiveIntegrity(path, action.versionInfo.sha256, action.versionInfo.sizeBytes, outError))
                {
                    return false;
                }
                outArchivePath = path;
                outError.clear();
                return true;
            };

            if (std::optional<std::filesystem::path> localPath = AsLocalPathIfAny(action.versionInfo.url))
            {
                if (!std::filesystem::exists(*localPath))
                {
                    outError = "Local package archive not found: " + localPath->string();
                    return false;
                }
                return validateLocalArchive(*localPath);
            }

            if (action.versionInfo.url.empty())
            {
                outError = "Package URL is empty for " + action.package.id + " " + action.package.version + ".";
                return false;
            }

            const std::string archiveName = BuildArchiveFileName(action.package.id, action.package.version);
            const std::filesystem::path globalArchivePath =
                globalCacheRoot / action.package.id / action.package.version / archiveName;
            const std::filesystem::path projectArchivePath =
                projectCacheRoot / action.package.id / action.package.version / archiveName;

            if (std::filesystem::exists(globalArchivePath))
            {
                if (validateLocalArchive(globalArchivePath))
                {
                    return true;
                }
                std::error_code removeEc;
                std::filesystem::remove(globalArchivePath, removeEc);
            }

            auto downloadAndValidate = [&](const std::filesystem::path& destination) -> bool
            {
                const std::filesystem::path tmpPath = destination.string() + ".tmp";
                std::string downloadError;
                if (!DownloadFile(action.versionInfo.url, tmpPath, downloadError))
                {
                    outError = downloadError;
                    return false;
                }
                std::string verifyError;
                if (!VerifyArchiveIntegrity(tmpPath, action.versionInfo.sha256, action.versionInfo.sizeBytes, verifyError))
                {
                    std::error_code removeEc;
                    std::filesystem::remove(tmpPath, removeEc);
                    outError = verifyError;
                    return false;
                }

                std::error_code ec;
                std::filesystem::create_directories(destination.parent_path(), ec);
                if (ec)
                {
                    outError = "Failed to prepare cache folder: " + destination.parent_path().string();
                    std::filesystem::remove(tmpPath, ec);
                    return false;
                }

                std::filesystem::rename(tmpPath, destination, ec);
                if (ec)
                {
                    std::filesystem::copy_file(tmpPath, destination, std::filesystem::copy_options::overwrite_existing, ec);
                    std::filesystem::remove(tmpPath, ec);
                    if (ec)
                    {
                        outError = "Failed to store downloaded archive in cache: " + destination.string();
                        return false;
                    }
                }

                outArchivePath = destination;
                outError.clear();
                return true;
            };

            if (downloadAndValidate(globalArchivePath))
            {
                return true;
            }

            const std::string globalError = outError;
            if (downloadAndValidate(projectArchivePath))
            {
                return true;
            }

            outError = "Download failed for " + action.package.id + " " + action.package.version +
                ". Global cache error: " + globalError + " | Project cache error: " + outError;
            return false;
        }

        bool TransactionalInstaller::AcquireArchive(
            const PackageInstallAction& action,
            const std::filesystem::path& globalCacheRoot,
            const std::filesystem::path& projectCacheRoot,
            std::filesystem::path& outArchivePath,
            std::string& outError)
        {
            return AcquireArchiveForAction(action, globalCacheRoot, projectCacheRoot, outArchivePath, outError);
        }

        void TransactionalInstaller::CleanupStaging(const std::filesystem::path& stagingRoot)
        {
            std::error_code ec;
            std::filesystem::remove_all(stagingRoot, ec);
        }

        void TransactionalInstaller::Rollback(
            const std::vector<std::pair<std::filesystem::path, std::filesystem::path>>& movedPairs)
        {
            std::error_code ec;
            for (auto it = movedPairs.rbegin(); it != movedPairs.rend(); ++it)
            {
                if (std::filesystem::exists(it->second))
                {
                    std::filesystem::rename(it->second, it->first, ec);
                    if (ec)
                    {
                        std::filesystem::copy(
                            it->second,
                            it->first,
                            std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                            ec);
                        std::filesystem::remove_all(it->second, ec);
                    }
                }
            }
        }

        PackageOperationResult TransactionalInstaller::Install(
            const PackageInstallPlan& plan,
            const std::filesystem::path& globalCacheRoot,
            const std::filesystem::path& projectCacheRoot,
            const std::filesystem::path& projectInstallRoot,
            ProgressCallback progressCallback)
        {
            PackageOperationResult result;
            if (!plan.success)
            {
                result.success = false;
                result.message = plan.message.empty() ? "Install plan failed." : plan.message;
                result.diagnostics = plan.diagnostics;
                return result;
            }

            const std::size_t totalActions = static_cast<std::size_t>(std::count_if(
                plan.actions.begin(),
                plan.actions.end(),
                [](const PackageInstallAction& action)
                {
                    return !action.alreadyInstalled;
                }));

            if (totalActions == 0)
            {
                result.success = true;
                result.message = "No install actions required.";
                for (const PackageInstallAction& action : plan.actions)
                {
                    result.affectedPackages.push_back(action.package);
                }
                return result;
            }

            std::error_code ec;
            std::filesystem::create_directories(projectInstallRoot, ec);
            if (ec)
            {
                result.success = false;
                result.message = "Failed to create install root: " + projectInstallRoot.string();
                return result;
            }

            std::vector<std::pair<std::filesystem::path, std::filesystem::path>> rollbackMoved;
            std::vector<std::filesystem::path> committedPaths;
            std::vector<std::filesystem::path> stagingRoots;

            std::size_t completed = 0;
            for (const PackageInstallAction& action : plan.actions)
            {
                if (action.alreadyInstalled)
                {
                    result.affectedPackages.push_back(action.package);
                    continue;
                }

                const float progressBase = static_cast<float>(completed) / static_cast<float>(std::max<std::size_t>(totalActions, 1));
                if (progressCallback)
                {
                    progressCallback(PackageProgressEvent {
                        PackageProgressStage::Downloading,
                        progressBase,
                        "Downloading package archive...",
                        action.package.id
                    });
                }

                std::filesystem::path archivePath;
                std::string acquireError;
                if (!AcquireArchive(action, globalCacheRoot, projectCacheRoot, archivePath, acquireError))
                {
                    result.success = false;
                    result.message = "Failed to acquire archive for " + action.package.id + ".";
                    result.diagnostics.push_back(acquireError);
                    for (const std::filesystem::path& committed : committedPaths)
                    {
                        std::filesystem::remove_all(committed, ec);
                    }
                    Rollback(rollbackMoved);
                    for (const std::filesystem::path& staging : stagingRoots)
                    {
                        CleanupStaging(staging);
                    }
                    return result;
                }

                if (progressCallback)
                {
                    progressCallback(PackageProgressEvent {
                        PackageProgressStage::Extracting,
                        progressBase + 0.2f / static_cast<float>(std::max<std::size_t>(totalActions, 1)),
                        "Extracting package...",
                        action.package.id
                    });
                }

                const std::filesystem::path stagingRoot =
                    projectInstallRoot /
                    "_staging" /
                    (action.package.id + "-" + action.package.version + "-" + std::to_string(GetTickCount64()));
                stagingRoots.push_back(stagingRoot);

                std::string extractError;
                if (!ExtractZipArchive(archivePath, stagingRoot, extractError))
                {
                    result.success = false;
                    result.message = "Failed to extract package " + action.package.id + ".";
                    result.diagnostics.push_back(extractError);
                    for (const std::filesystem::path& committed : committedPaths)
                    {
                        std::filesystem::remove_all(committed, ec);
                    }
                    Rollback(rollbackMoved);
                    for (const std::filesystem::path& staging : stagingRoots)
                    {
                        CleanupStaging(staging);
                    }
                    return result;
                }

                if (!std::filesystem::exists(stagingRoot / "package.json"))
                {
                    result.success = false;
                    result.message = "Extracted package is missing package.json: " + action.package.id;
                    for (const std::filesystem::path& committed : committedPaths)
                    {
                        std::filesystem::remove_all(committed, ec);
                    }
                    Rollback(rollbackMoved);
                    for (const std::filesystem::path& staging : stagingRoots)
                    {
                        CleanupStaging(staging);
                    }
                    return result;
                }

                const std::filesystem::path finalInstallPath =
                    projectInstallRoot / action.package.id / action.package.version;
                std::filesystem::create_directories(finalInstallPath.parent_path(), ec);
                if (ec)
                {
                    result.success = false;
                    result.message = "Failed to prepare install directory for " + action.package.id;
                    for (const std::filesystem::path& committed : committedPaths)
                    {
                        std::filesystem::remove_all(committed, ec);
                    }
                    Rollback(rollbackMoved);
                    for (const std::filesystem::path& staging : stagingRoots)
                    {
                        CleanupStaging(staging);
                    }
                    return result;
                }

                if (std::filesystem::exists(finalInstallPath))
                {
                    const std::filesystem::path rollbackPath =
                        projectInstallRoot /
                        "_rollback" /
                        action.package.id /
                        (action.package.version + "-" + std::to_string(GetTickCount64()));
                    std::filesystem::create_directories(rollbackPath.parent_path(), ec);
                    if (ec)
                    {
                        result.success = false;
                        result.message = "Failed to prepare rollback path for " + action.package.id;
                        for (const std::filesystem::path& committed : committedPaths)
                        {
                            std::filesystem::remove_all(committed, ec);
                        }
                        Rollback(rollbackMoved);
                        for (const std::filesystem::path& staging : stagingRoots)
                        {
                            CleanupStaging(staging);
                        }
                        return result;
                    }

                    std::string moveError;
                    if (!MoveOrCopyDirectory(finalInstallPath, rollbackPath, moveError))
                    {
                        result.success = false;
                        result.message = "Failed to stage rollback for " + action.package.id;
                        result.diagnostics.push_back(moveError);
                        for (const std::filesystem::path& committed : committedPaths)
                        {
                            std::filesystem::remove_all(committed, ec);
                        }
                        Rollback(rollbackMoved);
                        for (const std::filesystem::path& staging : stagingRoots)
                        {
                            CleanupStaging(staging);
                        }
                        return result;
                    }
                    rollbackMoved.emplace_back(finalInstallPath, rollbackPath);
                }

                if (progressCallback)
                {
                    progressCallback(PackageProgressEvent {
                        PackageProgressStage::Committing,
                        progressBase + 0.6f / static_cast<float>(std::max<std::size_t>(totalActions, 1)),
                        "Committing package files...",
                        action.package.id
                    });
                }

                std::string commitError;
                if (!MoveOrCopyDirectory(stagingRoot, finalInstallPath, commitError))
                {
                    result.success = false;
                    result.message = "Failed to commit package " + action.package.id;
                    result.diagnostics.push_back(commitError);
                    for (const std::filesystem::path& committed : committedPaths)
                    {
                        std::filesystem::remove_all(committed, ec);
                    }
                    Rollback(rollbackMoved);
                    for (const std::filesystem::path& staging : stagingRoots)
                    {
                        CleanupStaging(staging);
                    }
                    return result;
                }

                committedPaths.push_back(finalInstallPath);
                result.affectedPackages.push_back(action.package);
                ++completed;
            }

            std::filesystem::remove_all(projectInstallRoot / "_rollback", ec);
            std::filesystem::remove_all(projectInstallRoot / "_staging", ec);

            result.success = true;
            result.message = "Installed/updated " + std::to_string(completed) + " package(s).";
            return result;
        }

        PackageOperationResult TransactionalInstaller::Remove(
            const std::string& packageId,
            const std::vector<InstalledPackageRecord>& installed,
            const std::filesystem::path& projectInstallRoot,
            ProgressCallback progressCallback)
        {
            PackageOperationResult result;
            if (packageId.empty())
            {
                result.success = false;
                result.message = "Package id is required for remove.";
                return result;
            }

            std::vector<InstalledPackageRecord> matches;
            for (const InstalledPackageRecord& record : installed)
            {
                if (record.package.id == packageId)
                {
                    matches.push_back(record);
                }
            }

            if (matches.empty())
            {
                result.success = false;
                result.message = "Package '" + packageId + "' is not installed.";
                return result;
            }

            std::error_code ec;
            std::size_t removedCount = 0;
            for (std::size_t i = 0; i < matches.size(); ++i)
            {
                if (progressCallback)
                {
                    progressCallback(PackageProgressEvent {
                        PackageProgressStage::Removing,
                        static_cast<float>(i) / static_cast<float>(std::max<std::size_t>(matches.size(), 1)),
                        "Removing package files...",
                        packageId
                    });
                }

                if (std::filesystem::exists(matches[i].installPath))
                {
                    std::filesystem::remove_all(matches[i].installPath, ec);
                    if (ec)
                    {
                        result.success = false;
                        result.message = "Failed to remove install folder: " + matches[i].installPath.string();
                        return result;
                    }
                }
                ++removedCount;
                result.affectedPackages.push_back(matches[i].package);
            }

            if (std::filesystem::exists(projectInstallRoot / packageId))
            {
                std::filesystem::remove_all(projectInstallRoot / packageId, ec);
            }

            result.success = true;
            result.message = "Removed " + std::to_string(removedCount) + " installed version(s) of '" + packageId + "'.";
            return result;
        }

        PackageOperationResult TransactionalInstaller::VerifyInstalled(
            const std::string& packageIdOrEmpty,
            const std::vector<InstalledPackageRecord>& installed,
            ProgressCallback progressCallback)
        {
            PackageOperationResult result;

            std::vector<InstalledPackageRecord> targets;
            for (const InstalledPackageRecord& record : installed)
            {
                if (packageIdOrEmpty.empty() || record.package.id == packageIdOrEmpty)
                {
                    targets.push_back(record);
                }
            }

            if (targets.empty())
            {
                result.success = true;
                result.message = packageIdOrEmpty.empty()
                    ? "No installed packages to verify."
                    : "Package '" + packageIdOrEmpty + "' is not installed.";
                return result;
            }

            bool hasErrors = false;
            for (std::size_t i = 0; i < targets.size(); ++i)
            {
                const InstalledPackageRecord& record = targets[i];
                if (progressCallback)
                {
                    progressCallback(PackageProgressEvent {
                        PackageProgressStage::VerifyingInstalled,
                        static_cast<float>(i) / static_cast<float>(std::max<std::size_t>(targets.size(), 1)),
                        "Verifying installed package...",
                        record.package.id
                    });
                }

                if (!std::filesystem::exists(record.installPath))
                {
                    hasErrors = true;
                    result.diagnostics.push_back("Missing install path: " + record.installPath.string());
                }
                if (!record.cachePath.empty() && !record.sha256.empty() && std::filesystem::exists(record.cachePath))
                {
                    std::string actualHash;
                    std::string hashError;
                    if (!ComputeFileSHA256(record.cachePath, actualHash, hashError))
                    {
                        hasErrors = true;
                        result.diagnostics.push_back("Failed SHA verification for " + record.cachePath.string() + ": " + hashError);
                    }
                    else if (ToLower(actualHash) != ToLower(record.sha256))
                    {
                        hasErrors = true;
                        result.diagnostics.push_back("SHA mismatch for " + record.cachePath.string());
                    }
                }
                result.affectedPackages.push_back(record.package);
            }

            result.success = !hasErrors;
            result.message = hasErrors
                ? "Package verification completed with errors."
                : "Package verification succeeded.";
            return result;
        }

        void PackageMountService::AppendMount(const InstalledPackageRecord& package, const std::string& kind)
        {
            const std::filesystem::path mountPath = package.installPath / kind;
            std::error_code ec;
            if (!std::filesystem::exists(mountPath, ec) || !std::filesystem::is_directory(mountPath, ec))
            {
                return;
            }

            PackageMountRoot mount;
            mount.displayName = package.package.id + " [" + kind + "]";
            mount.packageId = package.package.id;
            mount.version = package.package.version;
            mount.path = mountPath;
            mount.readOnly = true;
            mount.kind = ToLower(kind);
            m_MountRoots.push_back(std::move(mount));
        }

        void PackageMountService::Refresh(const std::vector<InstalledPackageRecord>& installed)
        {
            m_MountRoots.clear();
            for (const InstalledPackageRecord& package : installed)
            {
                AppendMount(package, "Runtime");
                AppendMount(package, "Editor");
                AppendMount(package, "Docs");
                AppendMount(package, "Samples");
            }
        }

        std::vector<PackageMountRoot> PackageMountService::GetMountRoots() const
        {
            return m_MountRoots;
        }

        void ConventionModuleLoader::CollectModules(const std::filesystem::path& directory, std::vector<std::filesystem::path>& outPaths)
        {
            std::error_code ec;
            if (!std::filesystem::exists(directory, ec))
            {
                return;
            }

            for (const auto& entry : std::filesystem::recursive_directory_iterator(directory, ec))
            {
                if (ec)
                {
                    break;
                }
                if (!entry.is_regular_file(ec))
                {
                    continue;
                }
                const std::filesystem::path extension = entry.path().extension();
#if defined(_WIN32)
                if (ToLower(extension.string()) == ".dll")
#else
                if (ToLower(extension.string()) == ".so" || ToLower(extension.string()) == ".dylib")
#endif
                {
                    outPaths.push_back(entry.path());
                }
            }
        }

        bool ConventionModuleLoader::TryHotLoadPackage(
            const std::string& packageId,
            const std::filesystem::path& installRoot,
            std::vector<std::string>& outWarnings)
        {
            outWarnings.clear();
            std::vector<std::filesystem::path> modules;
            CollectModules(installRoot / "Runtime", modules);
            CollectModules(installRoot / "Editor", modules);

            if (modules.empty())
            {
                return true;
            }

            std::vector<DynamicLibrary> loaded;
            bool success = true;
            for (const std::filesystem::path& modulePath : modules)
            {
                DynamicLibrary library;
                std::string loadError;
                if (!library.Load(modulePath, loadError))
                {
                    success = false;
                    outWarnings.push_back("Failed hot-load '" + modulePath.string() + "': " + loadError);
                    continue;
                }
                loaded.push_back(std::move(library));
            }

            if (success)
            {
                m_LoadedModules[packageId] = std::move(loaded);
                return true;
            }

            for (DynamicLibrary& library : loaded)
            {
                std::string unloadError;
                library.Unload(unloadError);
            }
            return false;
        }

        bool ConventionModuleLoader::UnloadPackage(
            const std::string& packageId,
            std::vector<std::string>& outWarnings)
        {
            outWarnings.clear();
            const auto it = m_LoadedModules.find(packageId);
            if (it == m_LoadedModules.end())
            {
                return true;
            }

            bool success = true;
            for (DynamicLibrary& library : it->second)
            {
                std::string unloadError;
                if (!library.Unload(unloadError))
                {
                    success = false;
                    outWarnings.push_back("Failed to unload '" + library.GetPath().string() + "': " + unloadError);
                }
            }
            m_LoadedModules.erase(it);
            return success;
        }
    }

    PackageManager::PackageManager()
    {
        m_RegistryProvider = std::make_unique<CompositeRegistryProvider>();
        m_Resolver = std::make_unique<BasicResolver>();
        m_Installer = std::make_unique<TransactionalInstaller>();
        m_ManifestStore = std::make_unique<JsonManifestStore>();
        m_MountService = std::make_unique<PackageMountService>();
        m_ModuleLoader = std::make_unique<ConventionModuleLoader>();
    }

    PackageManager::~PackageManager() = default;

    bool PackageManager::Initialize(const std::filesystem::path& projectRoot, std::string& outError)
    {
        if (projectRoot.empty())
        {
            outError = "Project root is empty.";
            return false;
        }

        m_ProjectRoot = projectRoot.lexically_normal();
        m_PackagesRoot = m_ProjectRoot / "Packages";
        m_ProjectCacheRoot = m_PackagesRoot / "Cache";
        m_ProjectInstallRoot = m_PackagesRoot / "Installed";
        m_GlobalCacheRoot = ResolveGlobalCacheRoot();

        std::error_code ec;
        std::filesystem::create_directories(m_PackagesRoot, ec);
        if (ec)
        {
            outError = "Failed to create packages root: " + m_PackagesRoot.string();
            return false;
        }
        std::filesystem::create_directories(m_ProjectCacheRoot, ec);
        std::filesystem::create_directories(m_ProjectInstallRoot, ec);
        std::filesystem::create_directories(m_GlobalCacheRoot, ec);

        bool migratedLegacyLock = false;
        if (!m_ManifestStore->Load(m_PackagesRoot, m_Manifest, m_Lock, migratedLegacyLock, outError))
        {
            return false;
        }

        bool metadataChanged = false;
        bool sourcesChanged = false;
        for (InstalledPackageRecord& record : m_Lock.packages)
        {
            std::string metadataWarning;
            if (HydrateInstalledPackageMetadata(record, metadataWarning))
            {
                metadataChanged = true;
            }
            if (!metadataWarning.empty())
            {
                AppendLog("Package metadata warning for " + record.package.id + ": " + metadataWarning);
            }
        }

        for (PackageRegistrySource& source : m_Manifest.sources)
        {
            if (source.id == "github" && IsLegacyGitHubRegistryLocation(source.location))
            {
                source.location = std::string(kCanonicalGitHubRegistryRoot);
                sourcesChanged = true;
            }
        }
        m_Manifest.sources.erase(
            std::remove_if(
                m_Manifest.sources.begin(),
                m_Manifest.sources.end(),
                [&sourcesChanged](const PackageRegistrySource& source)
                {
                    if (IsLegacyLocalRegistrySource(source))
                    {
                        sourcesChanged = true;
                        return true;
                    }
                    return false;
                }),
            m_Manifest.sources.end());

        if (m_Manifest.sources.empty())
        {
            PackageRegistrySource githubSource;
            githubSource.id = "github";
            githubSource.location = std::string(kCanonicalGitHubRegistryRoot);
            githubSource.enabled = true;
            githubSource.preferLocal = false;
            m_Manifest.sources.push_back(githubSource);
            sourcesChanged = true;
        }

        m_LoadedLegacyLock = migratedLegacyLock;
        m_Initialized = true;
        RebuildMounts();

        if (migratedLegacyLock || metadataChanged || sourcesChanged)
        {
            if (migratedLegacyLock)
            {
                m_Lock.legacyEntries.clear();
            }
            std::string saveError;
            if (!SaveState(saveError))
            {
                outError = "Failed to save package state during initialization: " + saveError;
                return false;
            }
            if (migratedLegacyLock)
            {
                AppendLog("Migrated legacy Packages.lock into Packages/lock.json.");
            }
            if (metadataChanged)
            {
                AppendLog("Refreshed installed package metadata in Packages/lock.json.");
            }
            if (sourcesChanged)
            {
                AppendLog("Updated package registry sources to the canonical GitHub registry.");
            }
        }

        AppendLog("PackageManager initialized at " + m_ProjectRoot.string());
        outError.clear();
        return true;
    }

    bool PackageManager::InstallPackage(const std::filesystem::path& packageSource, std::string& outMessage)
    {
        const PackageOperationResult result = InstallFromArchive(packageSource);
        outMessage = result.message;
        return result.success;
    }

    bool PackageManager::RefreshRegistrySources(std::string& outError)
    {
        if (!EnsureInitialized(outError))
        {
            return false;
        }

        EmitProgress(PackageProgressStage::RefreshingRegistry, 0.0f, "Refreshing registry sources...", {});
        std::vector<PackageCatalogEntry> entries;
        if (!m_RegistryProvider->FetchIndex(m_Manifest.sources, entries, outError))
        {
            AppendLog("Package registry refresh failed: " + outError);
            EmitProgress(PackageProgressStage::RefreshingRegistry, 1.0f, "Registry refresh failed.", {});
            return false;
        }

        m_Catalog = std::move(entries);
        std::sort(
            m_Catalog.begin(),
            m_Catalog.end(),
            [](const PackageCatalogEntry& lhs, const PackageCatalogEntry& rhs)
            {
                return lhs.id < rhs.id;
            });
        AppendLog("Package registry refreshed (" + std::to_string(m_Catalog.size()) + " entries).");
        EmitProgress(PackageProgressStage::RefreshingRegistry, 1.0f, "Registry refresh complete.", {});
        outError.clear();
        return true;
    }

    PackageSearchResult PackageManager::Search(const PackageSearchQuery& query) const
    {
        PackageSearchResult result;
        if (!m_Initialized)
        {
            result.success = false;
            result.message = "Package manager not initialized.";
            return result;
        }

        const std::string text = ToLower(Trim(query.text));
        const std::string category = ToLower(Trim(query.category));

        result.success = true;
        for (const PackageCatalogEntry& entry : m_Catalog)
        {
            if (!category.empty() && category != "all" && ToLower(entry.category) != category)
            {
                continue;
            }

            if (!text.empty())
            {
                const std::string haystack = ToLower(entry.id + " " + entry.latest + " " + entry.category);
                if (haystack.find(text) == std::string::npos)
                {
                    continue;
                }
            }

            result.packages.push_back(entry);
        }
        result.message = "Found " + std::to_string(result.packages.size()) + " package(s).";
        return result;
    }

    bool PackageManager::GetPackage(const std::string& packageId, PackageDetails& outDetails, std::string& outError)
    {
        if (!EnsureInitialized(outError))
        {
            return false;
        }
        if (packageId.empty())
        {
            outError = "Package id is required.";
            return false;
        }

        const auto catalogIt = std::find_if(
            m_Catalog.begin(),
            m_Catalog.end(),
            [&packageId](const PackageCatalogEntry& entry)
            {
                return entry.id == packageId;
            });
        if (catalogIt != m_Catalog.end())
        {
            outDetails.catalog = *catalogIt;
        }
        else
        {
            outDetails.catalog.id = packageId;
        }

        if (!EnsureManifestForPackage(packageId, outError))
        {
            return false;
        }

        outDetails.hasManifest = true;
        outDetails.versions = m_ManifestCache[packageId];
        if (outDetails.catalog.latest.empty() && !outDetails.versions.empty())
        {
            outDetails.catalog.latest = outDetails.versions.front().version;
        }
        outError.clear();
        return true;
    }

    PackageOperationResult PackageManager::PreflightInstall(const PackageInstallRequest& request)
    {
        PackageOperationResult result;
        std::string error;
        if (!EnsureInitialized(error))
        {
            result.success = false;
            result.message = error;
            return result;
        }
        if (request.id.empty())
        {
            result.success = false;
            result.message = "Package id is required.";
            return result;
        }

        try
        {
            if (m_Catalog.empty())
            {
                std::string refreshError;
                RefreshRegistrySources(refreshError);
            }

            if (!EnsureManifestForPackage(request.id, error))
            {
                result.success = false;
                result.message = error;
                return result;
            }

            EmitProgress(PackageProgressStage::ResolvingDependencies, 0.0f, "Preflighting package dependencies...", request.id);
            const std::string engineVersion = NormalizeEngineVersion(
                Project::IsLoaded() ? Project::GetConfig().engineVersion : std::string("0.0.1"));
            const PackageInstallPlan plan = m_Resolver->ResolveInstallPlan(
                request,
                m_Lock.packages,
                m_Catalog,
                m_ManifestCache,
                engineVersion);
            if (!plan.success)
            {
                result.success = false;
                result.message = plan.message;
                result.diagnostics = plan.diagnostics;
                AppendLog("Package preflight plan failed for " + request.id + ": " + plan.message);
                EmitProgress(PackageProgressStage::ResolvingDependencies, 1.0f, "Preflight dependency resolution failed.", request.id);
                return result;
            }
            EmitProgress(PackageProgressStage::ResolvingDependencies, 1.0f, "Preflight dependency resolution complete.", request.id);

            if (plan.actions.empty())
            {
                result.success = true;
                result.message = "No packages resolved for preflight.";
                return result;
            }

            for (const PackageInstallAction& action : plan.actions)
            {
                result.affectedPackages.push_back(action.package);

                if (action.versionInfo.sha256.empty() || action.versionInfo.sizeBytes == 0)
                {
                    result.success = false;
                    result.message = "Registry metadata is incomplete for " + action.package.id + ".";
                    result.diagnostics.push_back(
                        "Package manifests must provide both SHA256 and size metadata before install.");
                    AppendLog("Package preflight failed for " + action.package.id + ": " + result.message);
                    EmitProgress(PackageProgressStage::Verifying, 1.0f, "Registry metadata incomplete.", action.package.id);
                    return result;
                }

                EmitProgress(PackageProgressStage::Downloading, 0.0f, "Checking package archive...", action.package.id);
                std::filesystem::path archivePath;
                std::string acquireError;
                if (!AcquireArchiveForAction(action, m_GlobalCacheRoot, m_ProjectCacheRoot, archivePath, acquireError))
                {
                    result.success = false;
                    result.message = "Registry integrity check failed for " + action.package.id + ".";
                    result.diagnostics.push_back(acquireError);
                    AppendLog("Package preflight failed for " + action.package.id + ": " + acquireError);
                    EmitProgress(PackageProgressStage::Verifying, 1.0f, "Package archive verification failed.", action.package.id);
                    return result;
                }

                EmitProgress(PackageProgressStage::Verifying, 1.0f, "Package archive verified.", action.package.id);
            }

            result.success = true;
            result.message = "Preflight checks passed for " + std::to_string(result.affectedPackages.size()) + " package(s).";
            AppendLog("Package preflight passed for " + request.id + ".");
            return result;
        }
        catch (const std::exception& ex)
        {
            result.success = false;
            result.message = "Package preflight crashed: " + std::string(ex.what());
            result.diagnostics.push_back("Preflight threw an exception instead of returning a package result.");
            AppendLog("Package preflight exception for " + request.id + ": " + ex.what());
            EmitProgress(PackageProgressStage::Verifying, 1.0f, "Package preflight crashed.", request.id);
            return result;
        }
        catch (...)
        {
            result.success = false;
            result.message = "Package preflight crashed with an unknown exception.";
            result.diagnostics.push_back("Unknown exception raised during package preflight.");
            AppendLog("Package preflight unknown exception for " + request.id + ".");
            EmitProgress(PackageProgressStage::Verifying, 1.0f, "Package preflight crashed.", request.id);
            return result;
        }
    }

    PackageOperationResult PackageManager::Install(const PackageInstallRequest& request)
    {
        PackageOperationResult result;
        std::string error;
        if (!EnsureInitialized(error))
        {
            result.success = false;
            result.message = error;
            return result;
        }
        if (request.id.empty())
        {
            result.success = false;
            result.message = "Package id is required.";
            return result;
        }

        if (m_Catalog.empty())
        {
            std::string refreshError;
            RefreshRegistrySources(refreshError);
        }

        if (!EnsureManifestForPackage(request.id, error))
        {
            result.success = false;
            result.message = error;
            return result;
        }

        EmitProgress(PackageProgressStage::ResolvingDependencies, 0.0f, "Resolving package dependencies...", request.id);
        const std::string engineVersion = NormalizeEngineVersion(
            Project::IsLoaded() ? Project::GetConfig().engineVersion : std::string("0.0.1"));
        const PackageInstallPlan plan = m_Resolver->ResolveInstallPlan(
            request,
            m_Lock.packages,
            m_Catalog,
            m_ManifestCache,
            engineVersion);
        if (!plan.success)
        {
            result.success = false;
            result.message = plan.message;
            result.diagnostics = plan.diagnostics;
            AppendLog("Package install plan failed for " + request.id + ": " + plan.message);
            EmitProgress(PackageProgressStage::ResolvingDependencies, 1.0f, "Dependency resolution failed.", request.id);
            return result;
        }
        EmitProgress(PackageProgressStage::ResolvingDependencies, 1.0f, "Dependency resolution complete.", request.id);

        PackageOperationResult installResult = m_Installer->Install(
            plan,
            m_GlobalCacheRoot,
            m_ProjectCacheRoot,
            m_ProjectInstallRoot,
            [this](const PackageProgressEvent& event)
            {
                EmitProgress(event.stage, event.progress01, event.message, event.packageId);
            });

        if (!installResult.success)
        {
            AppendLog("Package install failed for " + request.id + ": " + installResult.message);
            return installResult;
        }

        std::vector<InstalledPackageRecord> previousRecords = m_Lock.packages;
        for (const PackageInstallAction& action : plan.actions)
        {
            auto existingIt = std::remove_if(
                m_Lock.packages.begin(),
                m_Lock.packages.end(),
                [&action](const InstalledPackageRecord& record)
                {
                    return record.package.id == action.package.id;
                });
            m_Lock.packages.erase(existingIt, m_Lock.packages.end());

            InstalledPackageRecord record;
            record.package = action.package;
            record.source = action.versionInfo.url;
            record.sha256 = action.versionInfo.sha256;
            record.sizeBytes = action.versionInfo.sizeBytes;
            record.installPath = m_ProjectInstallRoot / action.package.id / action.package.version;
            record.dependencies = action.versionInfo.dependencies;

            const std::filesystem::path globalArchive =
                m_GlobalCacheRoot / action.package.id / action.package.version / BuildArchiveFileName(action.package.id, action.package.version);
            const std::filesystem::path projectArchive =
                m_ProjectCacheRoot / action.package.id / action.package.version / BuildArchiveFileName(action.package.id, action.package.version);
            if (std::filesystem::exists(globalArchive))
            {
                record.cachePath = globalArchive;
            }
            else if (std::filesystem::exists(projectArchive))
            {
                record.cachePath = projectArchive;
            }

            std::string metadataWarning;
            HydrateInstalledPackageMetadata(record, metadataWarning);
            if (!metadataWarning.empty())
            {
                installResult.diagnostics.push_back(metadataWarning);
                AppendLog("Package metadata warning for " + action.package.id + ": " + metadataWarning);
            }

            if (request.attemptHotLoad)
            {
                std::vector<std::string> moduleWarnings;
                const bool hotLoadSuccess = m_ModuleLoader->TryHotLoadPackage(action.package.id, record.installPath, moduleWarnings);
                if (!hotLoadSuccess)
                {
                    record.restartRequired = true;
                    installResult.restartRequired = true;
                }
                for (const std::string& warning : moduleWarnings)
                {
                    installResult.diagnostics.push_back(warning);
                    AppendLog(warning);
                }
            }

            m_Lock.packages.push_back(std::move(record));

            auto dependencyIt = std::find_if(
                m_Manifest.dependencies.begin(),
                m_Manifest.dependencies.end(),
                [&action](const PackageRequirement& requirement)
                {
                    return requirement.id == action.package.id;
                });
            if (dependencyIt == m_Manifest.dependencies.end())
            {
                m_Manifest.dependencies.push_back(PackageRequirement { action.package.id, action.package.version });
            }
            else
            {
                dependencyIt->versionConstraint = action.package.version;
            }
        }

        std::error_code ec;
        for (const InstalledPackageRecord& previous : previousRecords)
        {
            const bool stillInstalled = std::any_of(
                m_Lock.packages.begin(),
                m_Lock.packages.end(),
                [&previous](const InstalledPackageRecord& current)
                {
                    return current.package.id == previous.package.id &&
                        current.package.version == previous.package.version;
                });
            if (!stillInstalled && !previous.installPath.empty())
            {
                std::filesystem::remove_all(previous.installPath, ec);
            }
        }

        if (request.importSamplesToAssets && Project::IsLoaded())
        {
            for (const InstalledPackageRecord& record : m_Lock.packages)
            {
                const std::filesystem::path sampleSource = record.installPath / "Samples";
                if (!std::filesystem::exists(sampleSource))
                {
                    continue;
                }

                const std::filesystem::path sampleDestination =
                    Project::GetAssetsPath() / "Samples" / record.package.id / record.package.version;
                std::filesystem::create_directories(sampleDestination.parent_path(), ec);
                std::filesystem::copy(
                    sampleSource,
                    sampleDestination,
                    std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                    ec);
                if (ec)
                {
                    installResult.diagnostics.push_back(
                        "Failed to import samples for " + record.package.id + ": " + sampleDestination.string());
                }
            }
        }

        if (!RebuildMounts())
        {
            installResult.success = false;
            installResult.message = "Install succeeded but mount rebuild failed.";
            return installResult;
        }

        std::string saveError;
        if (!SaveState(saveError))
        {
            installResult.success = false;
            installResult.message = "Install succeeded but package state save failed: " + saveError;
            return installResult;
        }

        AppendLog("Installed package request for " + request.id + " completed.");
        return installResult;
    }

    PackageOperationResult PackageManager::Update(const std::string& packageId, const std::string& version)
    {
        PackageInstallRequest request;
        request.id = packageId;
        request.version = version;
        request.importSamplesToAssets = false;
        request.attemptHotLoad = true;
        PackageOperationResult result = Install(request);
        if (result.success)
        {
            result.message = "Package update completed: " + packageId;
        }
        return result;
    }

    PackageOperationResult PackageManager::Remove(const std::string& packageId)
    {
        PackageOperationResult result;
        std::string error;
        if (!EnsureInitialized(error))
        {
            result.success = false;
            result.message = error;
            return result;
        }

        PackageOperationResult removeResult = m_Installer->Remove(
            packageId,
            m_Lock.packages,
            m_ProjectInstallRoot,
            [this](const PackageProgressEvent& event)
            {
                EmitProgress(event.stage, event.progress01, event.message, event.packageId);
            });
        if (!removeResult.success)
        {
            AppendLog("Package removal failed for " + packageId + ": " + removeResult.message);
            return removeResult;
        }

        std::vector<std::string> unloadWarnings;
        if (!m_ModuleLoader->UnloadPackage(packageId, unloadWarnings))
        {
            removeResult.restartRequired = true;
        }
        for (const std::string& warning : unloadWarnings)
        {
            removeResult.diagnostics.push_back(warning);
            AppendLog(warning);
        }

        m_Lock.packages.erase(
            std::remove_if(
                m_Lock.packages.begin(),
                m_Lock.packages.end(),
                [&packageId](const InstalledPackageRecord& record)
                {
                    return record.package.id == packageId;
                }),
            m_Lock.packages.end());

        m_Manifest.dependencies.erase(
            std::remove_if(
                m_Manifest.dependencies.begin(),
                m_Manifest.dependencies.end(),
                [&packageId](const PackageRequirement& requirement)
                {
                    return requirement.id == packageId;
                }),
            m_Manifest.dependencies.end());

        RebuildMounts();

        std::string saveError;
        if (!SaveState(saveError))
        {
            removeResult.success = false;
            removeResult.message = "Removed package but failed to persist package state: " + saveError;
            return removeResult;
        }

        AppendLog("Removed package '" + packageId + "'.");
        return removeResult;
    }

    PackageOperationResult PackageManager::Verify(const std::string& packageIdOrEmpty)
    {
        std::string error;
        if (!EnsureInitialized(error))
        {
            return PackageOperationResult { false, false, error, {}, {} };
        }

        PackageOperationResult verifyResult = m_Installer->VerifyInstalled(
            packageIdOrEmpty,
            m_Lock.packages,
            [this](const PackageProgressEvent& event)
            {
                EmitProgress(event.stage, event.progress01, event.message, event.packageId);
            });
        AppendLog("Verification finished: " + verifyResult.message);
        return verifyResult;
    }

    std::vector<InstalledPackageRecord> PackageManager::GetInstalled() const
    {
        return m_Lock.packages;
    }

    std::vector<PackageMountRoot> PackageManager::GetMountRoots() const
    {
        if (!m_MountService)
        {
            return {};
        }

        return m_MountService->GetMountRoots();
    }

    std::vector<std::string> PackageManager::GetOperationLog() const
    {
        return m_OperationLog;
    }

    const PackageManifest& PackageManager::GetManifest() const
    {
        return m_Manifest;
    }

    const PackageLock& PackageManager::GetLock() const
    {
        return m_Lock;
    }

    void PackageManager::SetProgressCallback(ProgressCallback callback)
    {
        m_ProgressCallback = std::move(callback);
    }

    void PackageManager::AppendLog(const std::string& line)
    {
        if (line.empty())
        {
            return;
        }

        m_OperationLog.push_back(line);
        constexpr std::size_t kMaxLogLines = 1024;
        if (m_OperationLog.size() > kMaxLogLines)
        {
            m_OperationLog.erase(
                m_OperationLog.begin(),
                m_OperationLog.begin() + static_cast<std::ptrdiff_t>(m_OperationLog.size() - kMaxLogLines));
        }
    }

    std::filesystem::path PackageManager::ResolveGlobalCacheRoot() const
    {
#if defined(_WIN32)
        const char* localAppData = std::getenv("LOCALAPPDATA");
        if (localAppData != nullptr && localAppData[0] != '\0')
        {
            return std::filesystem::path(localAppData) / "Luma" / "PackageCache";
        }

        const char* userProfile = std::getenv("USERPROFILE");
        if (userProfile != nullptr && userProfile[0] != '\0')
        {
            return std::filesystem::path(userProfile) / "AppData" / "Local" / "Luma" / "PackageCache";
        }
#else
        const char* home = std::getenv("HOME");
        if (home != nullptr && home[0] != '\0')
        {
            return std::filesystem::path(home) / ".cache" / "Luma" / "PackageCache";
        }
#endif
        return std::filesystem::current_path() / "Cache" / "PackageCache";
    }

    bool PackageManager::EnsureManifestForPackage(const std::string& packageId, std::string& outError)
    {
        if (m_ManifestCache.contains(packageId))
        {
            outError.clear();
            return true;
        }

        std::vector<PackageVersionInfo> versions;
        if (!m_RegistryProvider->FetchManifest(m_Manifest.sources, packageId, versions, outError))
        {
            return false;
        }

        std::sort(
            versions.begin(),
            versions.end(),
            [](const PackageVersionInfo& lhs, const PackageVersionInfo& rhs)
            {
                return IsVersionGreater(lhs.version, rhs.version);
            });
        m_ManifestCache[packageId] = std::move(versions);
        outError.clear();
        return true;
    }

    bool PackageManager::SaveState(std::string& outError)
    {
        m_Manifest.schemaVersion = 1;
        m_Lock.schemaVersion = 1;
        m_Lock.legacyEntries.clear();
        return m_ManifestStore->Save(m_PackagesRoot, m_Manifest, m_Lock, outError);
    }

    void PackageManager::EmitProgress(
        const PackageProgressStage stage,
        const float progress01,
        const std::string& message,
        const std::string& packageId)
    {
        if (!m_ProgressCallback)
        {
            return;
        }

        m_ProgressCallback(PackageProgressEvent {
            stage,
            std::clamp(progress01, 0.0f, 1.0f),
            message,
            packageId
        });
    }

    std::string PackageManager::NormalizeEngineVersion(const std::string& value) const
    {
        return NormalizeSemVerString(value);
    }

    bool PackageManager::RebuildMounts()
    {
        if (!m_MountService)
        {
            return false;
        }

        m_MountService->Refresh(m_Lock.packages);
        return true;
    }

    bool PackageManager::EnsureInitialized(std::string& outError) const
    {
        if (!m_Initialized)
        {
            outError = "Package manager is not initialized.";
            return false;
        }

        outError.clear();
        return true;
    }

    PackageOperationResult PackageManager::InstallFromArchive(const std::filesystem::path& archivePath)
    {
        PackageOperationResult result;
        std::string initError;
        if (!EnsureInitialized(initError))
        {
            result.success = false;
            result.message = initError;
            return result;
        }

        std::string packageId;
        std::string version;
        std::string parseError;
        if (!ReadPackageIdentityFromArchive(archivePath, packageId, version, parseError))
        {
            result.success = false;
            result.message = parseError;
            return result;
        }

        PackageInstallPlan plan;
        plan.success = true;
        plan.message = "Installing local package archive.";

        PackageInstallAction action;
        action.package.id = packageId;
        action.package.version = version;
        action.versionInfo.version = version;
        action.versionInfo.url = archivePath.string();
        action.alreadyInstalled = std::any_of(
            m_Lock.packages.begin(),
            m_Lock.packages.end(),
            [&action](const InstalledPackageRecord& record)
            {
                return record.package.id == action.package.id &&
                    record.package.version == action.package.version;
            });

        std::string localHash;
        std::string hashError;
        if (ComputeFileSHA256(archivePath, localHash, hashError))
        {
            action.versionInfo.sha256 = localHash;
        }

        std::error_code sizeEc;
        const std::uintmax_t fileSize = std::filesystem::file_size(archivePath, sizeEc);
        if (!sizeEc)
        {
            action.versionInfo.sizeBytes = static_cast<std::uint64_t>(fileSize);
        }

        plan.actions.push_back(action);

        PackageOperationResult installResult = m_Installer->Install(
            plan,
            m_GlobalCacheRoot,
            m_ProjectCacheRoot,
            m_ProjectInstallRoot,
            [this](const PackageProgressEvent& event)
            {
                EmitProgress(event.stage, event.progress01, event.message, event.packageId);
            });
        if (!installResult.success)
        {
            return installResult;
        }

        m_Lock.packages.erase(
            std::remove_if(
                m_Lock.packages.begin(),
                m_Lock.packages.end(),
                [&packageId](const InstalledPackageRecord& record)
                {
                    return record.package.id == packageId;
                }),
            m_Lock.packages.end());

        InstalledPackageRecord record;
        record.package.id = packageId;
        record.package.version = version;
        record.source = archivePath.string();
        record.sha256 = localHash;
        record.sizeBytes = action.versionInfo.sizeBytes;
        record.installPath = m_ProjectInstallRoot / packageId / version;
        record.cachePath = archivePath;
        std::string metadataWarning;
        HydrateInstalledPackageMetadata(record, metadataWarning);
        if (!metadataWarning.empty())
        {
            installResult.diagnostics.push_back(metadataWarning);
            AppendLog("Package metadata warning for " + packageId + ": " + metadataWarning);
        }
        m_Lock.packages.push_back(std::move(record));

        auto dependencyIt = std::find_if(
            m_Manifest.dependencies.begin(),
            m_Manifest.dependencies.end(),
            [&packageId](const PackageRequirement& requirement)
            {
                return requirement.id == packageId;
            });
        if (dependencyIt == m_Manifest.dependencies.end())
        {
            m_Manifest.dependencies.push_back(PackageRequirement { packageId, version });
        }
        else
        {
            dependencyIt->versionConstraint = version;
        }

        RebuildMounts();

        std::string saveError;
        if (!SaveState(saveError))
        {
            return PackageOperationResult {
                false,
                false,
                "Package installed but failed to save package state: " + saveError,
                {},
                { { packageId, version } }
            };
        }

        installResult.message = "Installed local package " + packageId + "@" + version + ".";
        AppendLog(installResult.message);
        return installResult;
    }
}
