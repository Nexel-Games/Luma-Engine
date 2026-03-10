#include "Luma/Asset/Core/AssetMetaIO.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "Luma/Asset/Core/AssetTypes.h"

namespace Luma::Assets
{
    namespace
    {
        std::string Trim(std::string value)
        {
            const auto isSpace = [](const char c)
            {
                return c == ' ' || c == '\t' || c == '\r' || c == '\n';
            };

            while (!value.empty() && isSpace(value.front()))
            {
                value.erase(value.begin());
            }
            while (!value.empty() && isSpace(value.back()))
            {
                value.pop_back();
            }
            return value;
        }

        std::string Escape(std::string_view value)
        {
            std::string escaped;
            escaped.reserve(value.size());
            for (const char c : value)
            {
                switch (c)
                {
                case '\\':
                    escaped += "\\\\";
                    break;
                case '|':
                    escaped += "\\|";
                    break;
                case '=':
                    escaped += "\\=";
                    break;
                case '\n':
                    escaped += "\\n";
                    break;
                case '\r':
                    escaped += "\\r";
                    break;
                default:
                    escaped.push_back(c);
                    break;
                }
            }
            return escaped;
        }

        std::string Unescape(std::string_view value)
        {
            std::string unescaped;
            unescaped.reserve(value.size());
            bool escaped = false;
            for (const char c : value)
            {
                if (!escaped)
                {
                    if (c == '\\')
                    {
                        escaped = true;
                        continue;
                    }
                    unescaped.push_back(c);
                    continue;
                }

                switch (c)
                {
                case 'n':
                    unescaped.push_back('\n');
                    break;
                case 'r':
                    unescaped.push_back('\r');
                    break;
                case '\\':
                case '|':
                case '=':
                    unescaped.push_back(c);
                    break;
                default:
                    unescaped.push_back(c);
                    break;
                }
                escaped = false;
            }

            if (escaped)
            {
                unescaped.push_back('\\');
            }
            return unescaped;
        }

        std::string JoinEscaped(const std::vector<std::string>& values)
        {
            std::ostringstream output;
            for (std::size_t i = 0; i < values.size(); ++i)
            {
                if (i > 0)
                {
                    output << '|';
                }
                output << Escape(values[i]);
            }
            return output.str();
        }

        std::vector<std::string> SplitEscaped(const std::string_view value)
        {
            std::vector<std::string> values;
            std::string current;
            bool escaped = false;
            for (const char c : value)
            {
                if (!escaped)
                {
                    if (c == '\\')
                    {
                        escaped = true;
                        current.push_back(c);
                        continue;
                    }
                    if (c == '|')
                    {
                        values.emplace_back(Unescape(current));
                        current.clear();
                        continue;
                    }
                }
                current.push_back(c);
                escaped = false;
            }

            if (!current.empty() || !value.empty())
            {
                values.emplace_back(Unescape(current));
            }

            return values;
        }

        bool ParseUnsigned(const std::string_view value, std::uint64_t& outValue)
        {
            const char* begin = value.data();
            const char* end = begin + value.size();
            std::uint64_t parsed = 0;
            const auto [ptr, ec] = std::from_chars(begin, end, parsed);
            if (ec != std::errc() || ptr != end)
            {
                return false;
            }
            outValue = parsed;
            return true;
        }
    }

    bool WriteMetaFile(const std::filesystem::path& metaPath, const AssetMeta& meta, std::string& outError)
    {
        std::error_code ec;
        std::filesystem::create_directories(metaPath.parent_path(), ec);
        if (ec)
        {
            outError = "Failed to create meta directory: " + metaPath.parent_path().string();
            return false;
        }

        std::ofstream output(metaPath, std::ios::trunc);
        if (!output.is_open())
        {
            outError = "Failed to open meta file for write: " + metaPath.string();
            return false;
        }

        output << "SchemaVersion=" << meta.schemaVersion << '\n';
        output << "AssetID=" << meta.id << '\n';
        output << "AssetType=" << ToString(meta.type) << '\n';
        output << "Name=" << Escape(meta.name) << '\n';
        output << "AssetPath=" << Escape(meta.assetPath.generic_string()) << '\n';

        std::vector<std::string> sourcePaths;
        sourcePaths.reserve(meta.sourcePaths.size());
        for (const auto& sourcePath : meta.sourcePaths)
        {
            sourcePaths.push_back(sourcePath.generic_string());
        }
        output << "SourcePaths=" << JoinEscaped(sourcePaths) << '\n';

        output << "ImporterID=" << Escape(meta.importerID) << '\n';
        output << "ImporterVersion=" << meta.importerVersion << '\n';
        output << "SourceHash=" << meta.sourceHash << '\n';
        output << "SettingsHash=" << meta.settingsHash << '\n';
        output << "BuildHash=" << meta.buildHash << '\n';

        std::vector<std::string> dependencyStrings;
        dependencyStrings.reserve(meta.dependencies.size());
        for (const AssetID dependency : meta.dependencies)
        {
            dependencyStrings.push_back(std::to_string(dependency));
        }
        output << "Dependencies=" << JoinEscaped(dependencyStrings) << '\n';
        output << "Tags=" << JoinEscaped(meta.tags) << '\n';
        output << "ThumbnailID=" << Escape(meta.thumbnailID) << '\n';
        output << "LastImportUtc=" << Escape(meta.lastImportUtc) << '\n';

        std::vector<std::pair<std::string, std::string>> sortedSettings;
        sortedSettings.reserve(meta.importSettings.size());
        for (const auto& [key, value] : meta.importSettings)
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

        for (const auto& [key, value] : sortedSettings)
        {
            output << "Setting." << Escape(key) << '=' << Escape(value) << '\n';
        }

        if (!output.good())
        {
            outError = "Failed writing meta file: " + metaPath.string();
            return false;
        }

        return true;
    }

    bool ReadMetaFile(const std::filesystem::path& metaPath, AssetMeta& outMeta, std::string& outError)
    {
        std::ifstream input(metaPath);
        if (!input.is_open())
        {
            outError = "Failed to open meta file: " + metaPath.string();
            return false;
        }

        AssetMeta meta {};
        meta.metaPath = metaPath;

        std::string line;
        while (std::getline(input, line))
        {
            const std::size_t separator = line.find('=');
            if (separator == std::string::npos)
            {
                continue;
            }

            const std::string key = Trim(line.substr(0, separator));
            const std::string value = Trim(line.substr(separator + 1));

            if (key == "SchemaVersion")
            {
                std::uint64_t schemaVersion = 0;
                if (ParseUnsigned(value, schemaVersion))
                {
                    meta.schemaVersion = static_cast<std::uint32_t>(schemaVersion);
                }
            }
            else if (key == "AssetID")
            {
                std::uint64_t id = 0;
                if (ParseUnsigned(value, id))
                {
                    meta.id = id;
                }
            }
            else if (key == "AssetType")
            {
                TryParseAssetType(value, meta.type);
            }
            else if (key == "Name")
            {
                meta.name = Unescape(value);
            }
            else if (key == "AssetPath")
            {
                meta.assetPath = Unescape(value);
            }
            else if (key == "SourcePaths")
            {
                meta.sourcePaths.clear();
                for (const std::string& source : SplitEscaped(value))
                {
                    if (!source.empty())
                    {
                        meta.sourcePaths.emplace_back(source);
                    }
                }
            }
            else if (key == "ImporterID")
            {
                meta.importerID = Unescape(value);
            }
            else if (key == "ImporterVersion")
            {
                std::uint64_t version = 0;
                if (ParseUnsigned(value, version))
                {
                    meta.importerVersion = static_cast<std::uint32_t>(version);
                }
            }
            else if (key == "SourceHash")
            {
                ParseUnsigned(value, meta.sourceHash);
            }
            else if (key == "SettingsHash")
            {
                ParseUnsigned(value, meta.settingsHash);
            }
            else if (key == "BuildHash")
            {
                ParseUnsigned(value, meta.buildHash);
            }
            else if (key == "Dependencies")
            {
                meta.dependencies.clear();
                for (const std::string& dependency : SplitEscaped(value))
                {
                    std::uint64_t parsedDependency = 0;
                    if (ParseUnsigned(dependency, parsedDependency))
                    {
                        meta.dependencies.push_back(parsedDependency);
                    }
                }
            }
            else if (key == "Tags")
            {
                meta.tags = SplitEscaped(value);
            }
            else if (key == "ThumbnailID")
            {
                meta.thumbnailID = Unescape(value);
            }
            else if (key == "LastImportUtc")
            {
                meta.lastImportUtc = Unescape(value);
            }
            else if (key.rfind("Setting.", 0) == 0)
            {
                const std::string settingKey = Unescape(key.substr(8));
                meta.importSettings[settingKey] = Unescape(value);
            }
        }

        if (meta.id == 0)
        {
            outError = "Meta file missing AssetID: " + metaPath.string();
            return false;
        }

        if (meta.assetPath.empty())
        {
            outError = "Meta file missing AssetPath: " + metaPath.string();
            return false;
        }

        outMeta = std::move(meta);
        return true;
    }

    std::string MakeUtcTimestampString()
    {
        const auto now = std::chrono::system_clock::now();
        const std::time_t nowTime = std::chrono::system_clock::to_time_t(now);

        std::tm utcTime {};
#if defined(_WIN32)
        gmtime_s(&utcTime, &nowTime);
#else
        gmtime_r(&nowTime, &utcTime);
#endif

        char buffer[64] = {};
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utcTime);
        return std::string(buffer);
    }
}

