#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace Luma::Assets
{
    struct SemVer
    {
        int major = 0;
        int minor = 0;
        int patch = 0;
    };

    bool TryParseSemVer(std::string_view value, SemVer& outVersion);
    int CompareSemVer(const SemVer& left, const SemVer& right);
    bool IsValidVersionConstraint(std::string_view constraint);
    bool SatisfiesVersionConstraint(std::string_view version, std::string_view constraint);
    std::string NormalizeSemVerString(std::string_view value);
}

