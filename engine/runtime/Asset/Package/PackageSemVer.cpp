#include "Luma/Asset/Package/PackageSemVer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <sstream>
#include <vector>

namespace Luma::Assets
{
    namespace
    {
        bool IsDigitsOnly(std::string_view value)
        {
            if (value.empty())
            {
                return false;
            }

            return std::all_of(
                value.begin(),
                value.end(),
                [](const unsigned char c)
                {
                    return std::isdigit(c) != 0;
                });
        }

        bool ParseConstraintToken(
            std::string_view token,
            std::string_view& outOp,
            SemVer& outVersion)
        {
            token = std::string_view(
                token.data(),
                token.size());

            static constexpr std::array<std::string_view, 7> kOperators = {
                ">=",
                "<=",
                "^",
                "~",
                ">",
                "<",
                "="
            };

            outOp = {};
            std::string_view rhs = token;

            for (const std::string_view op : kOperators)
            {
                if (token.rfind(op, 0) == 0)
                {
                    outOp = op;
                    rhs = token.substr(op.size());
                    break;
                }
            }

            if (!TryParseSemVer(rhs, outVersion))
            {
                return false;
            }

            return true;
        }

        SemVer CaretUpperBound(const SemVer& value)
        {
            if (value.major > 0)
            {
                return SemVer { value.major + 1, 0, 0 };
            }
            if (value.minor > 0)
            {
                return SemVer { 0, value.minor + 1, 0 };
            }
            return SemVer { 0, 0, value.patch + 1 };
        }

        SemVer TildeUpperBound(const SemVer& value)
        {
            return SemVer { value.major, value.minor + 1, 0 };
        }

        bool SatisfiesToken(const SemVer& version, std::string_view op, const SemVer& rhs)
        {
            const int cmp = CompareSemVer(version, rhs);
            if (op.empty() || op == "=")
            {
                return cmp == 0;
            }
            if (op == ">")
            {
                return cmp > 0;
            }
            if (op == ">=")
            {
                return cmp >= 0;
            }
            if (op == "<")
            {
                return cmp < 0;
            }
            if (op == "<=")
            {
                return cmp <= 0;
            }
            if (op == "^")
            {
                return cmp >= 0 && CompareSemVer(version, CaretUpperBound(rhs)) < 0;
            }
            if (op == "~")
            {
                return cmp >= 0 && CompareSemVer(version, TildeUpperBound(rhs)) < 0;
            }
            return false;
        }
    }

    bool TryParseSemVer(const std::string_view value, SemVer& outVersion)
    {
        std::stringstream stream { std::string(value) };
        std::string major;
        std::string minor;
        std::string patch;

        if (!std::getline(stream, major, '.'))
        {
            return false;
        }
        if (!std::getline(stream, minor, '.'))
        {
            return false;
        }
        if (!std::getline(stream, patch, '.'))
        {
            return false;
        }
        if (stream.rdbuf()->in_avail() != 0)
        {
            return false;
        }

        if (!IsDigitsOnly(major) || !IsDigitsOnly(minor) || !IsDigitsOnly(patch))
        {
            return false;
        }

        outVersion.major = std::stoi(major);
        outVersion.minor = std::stoi(minor);
        outVersion.patch = std::stoi(patch);
        return true;
    }

    int CompareSemVer(const SemVer& left, const SemVer& right)
    {
        if (left.major != right.major)
        {
            return left.major < right.major ? -1 : 1;
        }
        if (left.minor != right.minor)
        {
            return left.minor < right.minor ? -1 : 1;
        }
        if (left.patch != right.patch)
        {
            return left.patch < right.patch ? -1 : 1;
        }
        return 0;
    }

    bool IsValidVersionConstraint(const std::string_view constraint)
    {
        std::stringstream stream { std::string(constraint) };
        std::string token;
        bool sawToken = false;
        while (stream >> token)
        {
            std::string_view op;
            SemVer version {};
            if (!ParseConstraintToken(token, op, version))
            {
                return false;
            }
            sawToken = true;
        }
        return sawToken;
    }

    bool SatisfiesVersionConstraint(const std::string_view version, const std::string_view constraint)
    {
        SemVer parsedVersion {};
        if (!TryParseSemVer(version, parsedVersion))
        {
            return false;
        }

        std::stringstream stream { std::string(constraint) };
        std::string token;
        bool sawToken = false;
        while (stream >> token)
        {
            std::string_view op;
            SemVer rhs {};
            if (!ParseConstraintToken(token, op, rhs))
            {
                return false;
            }
            if (!SatisfiesToken(parsedVersion, op, rhs))
            {
                return false;
            }
            sawToken = true;
        }
        return sawToken;
    }

    std::string NormalizeSemVerString(const std::string_view value)
    {
        std::string normalized(value);
        normalized.erase(
            std::remove_if(
                normalized.begin(),
                normalized.end(),
                [](const unsigned char c)
                {
                    return std::isspace(c) != 0;
                }),
            normalized.end());
        if (normalized.empty())
        {
            return "0.0.1";
        }

        if (normalized == "0.1" || normalized == "0.1.0")
        {
            return "0.0.1";
        }

        std::vector<std::string> tokens;
        std::stringstream stream(normalized);
        std::string token;
        while (std::getline(stream, token, '.'))
        {
            tokens.push_back(token);
        }

        if (tokens.size() == 1 && IsDigitsOnly(tokens[0]))
        {
            return tokens[0] + ".0.0";
        }

        if (tokens.size() == 2 && IsDigitsOnly(tokens[0]) && IsDigitsOnly(tokens[1]))
        {
            return tokens[0] + "." + tokens[1] + ".0";
        }

        if (tokens.size() == 3 && IsDigitsOnly(tokens[0]) && IsDigitsOnly(tokens[1]) && IsDigitsOnly(tokens[2]))
        {
            return normalized;
        }

        return "0.0.1";
    }
}
