#include "Luma/RHI/ShaderSystem.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <utility>

namespace Luma
{
    bool ShaderSystem::Initialize(const RendererAPI api)
    {
        if (m_Initialized && m_RendererAPI != api)
        {
            ClearCache();
        }

        m_RendererAPI = api;
        m_Initialized = true;
        return true;
    }

    void ShaderSystem::Shutdown()
    {
        ClearCache();
        m_Programs.clear();
        m_Initialized = false;
    }

    bool ShaderSystem::RegisterProgram(const ShaderProgramRegistration& registration)
    {
        if (registration.programName.empty() || registration.vertexSource.empty() || registration.fragmentSource.empty())
        {
            return false;
        }

        ProgramRecord record;
        record.vertexSource = registration.vertexSource;
        record.fragmentSource = registration.fragmentSource;
        m_Programs[registration.programName] = std::move(record);
        InvalidateProgram(registration.programName);
        return true;
    }

    bool ShaderSystem::HasProgram(const std::string_view programName) const
    {
        return m_Programs.find(std::string(programName)) != m_Programs.end();
    }

    bool ShaderSystem::TryResolveProgram(
        const std::string_view programName,
        const ShaderVariantOptions& options,
        ShaderProgramVariant& outVariant)
    {
        if (!m_Initialized)
        {
            return false;
        }

        const auto programIt = m_Programs.find(std::string(programName));
        if (programIt == m_Programs.end())
        {
            return false;
        }

        const bool useSpirv = false;
        const std::string macroSignature = BuildMacroSignature(options.macros);

        VariantKey key;
        key.programName = std::string(programName);
        key.macroSignature = macroSignature;
        key.spirv = useSpirv;

        const auto cachedVariantIt = m_VariantCache.find(key);
        if (cachedVariantIt != m_VariantCache.end())
        {
            outVariant = cachedVariantIt->second;
            return true;
        }

        ShaderProgramVariant variant;
        variant.programDesc.vertexPath = BuildStagePath(programIt->second.vertexSource, "vert", useSpirv);
        variant.programDesc.fragmentPath = BuildStagePath(programIt->second.fragmentSource, "frag", useSpirv);
        variant.programDesc.vertexIsSpirv = useSpirv;
        variant.programDesc.fragmentIsSpirv = useSpirv;
        variant.cacheKey = std::string(ToString(m_RendererAPI)) + ":" + key.programName + "|" +
            (key.spirv ? "spv" : "src") + "|" + key.macroSignature;
        variant.macros = options.macros;

        auto insertResult = m_VariantCache.emplace(std::move(key), variant);
        outVariant = insertResult.first->second;
        return true;
    }

    void ShaderSystem::InvalidateProgram(const std::string_view programName)
    {
        if (m_VariantCache.empty())
        {
            return;
        }

        const std::string keyName(programName);
        for (auto it = m_VariantCache.begin(); it != m_VariantCache.end();)
        {
            if (it->first.programName == keyName)
            {
                it = m_VariantCache.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void ShaderSystem::ClearCache()
    {
        m_VariantCache.clear();
    }

    std::size_t ShaderSystem::GetCachedVariantCount() const
    {
        return m_VariantCache.size();
    }

    RendererAPI ShaderSystem::GetRendererAPI() const
    {
        return m_RendererAPI;
    }

    std::size_t ShaderSystem::VariantKeyHash::operator()(const VariantKey& key) const
    {
        const std::size_t h0 = std::hash<std::string>()(key.programName);
        const std::size_t h1 = std::hash<std::string>()(key.macroSignature);
        const std::size_t h2 = std::hash<bool>()(key.spirv);
        return h0 ^ (h1 << 1U) ^ (h2 << 2U);
    }

    bool ShaderSystem::VariantKeyEqual::operator()(const VariantKey& lhs, const VariantKey& rhs) const
    {
        return lhs.spirv == rhs.spirv &&
            lhs.programName == rhs.programName &&
            lhs.macroSignature == rhs.macroSignature;
    }

    std::string ShaderSystem::BuildMacroSignature(const std::vector<ShaderMacro>& macros)
    {
        if (macros.empty())
        {
            return "none";
        }

        std::vector<ShaderMacro> normalizedMacros;
        normalizedMacros.reserve(macros.size());

        for (const ShaderMacro& macro : macros)
        {
            if (macro.name.empty())
            {
                continue;
            }
            normalizedMacros.push_back(macro);
        }

        std::sort(
            normalizedMacros.begin(),
            normalizedMacros.end(),
            [](const ShaderMacro& lhs, const ShaderMacro& rhs)
            {
                if (lhs.name == rhs.name)
                {
                    return lhs.value < rhs.value;
                }
                return lhs.name < rhs.name;
            });

        if (normalizedMacros.empty())
        {
            return "none";
        }

        std::string signature;
        for (std::size_t i = 0; i < normalizedMacros.size(); ++i)
        {
            if (i > 0)
            {
                signature += ";";
            }
            signature += normalizedMacros[i].name;
            signature += "=";
            signature += normalizedMacros[i].value.empty() ? "1" : normalizedMacros[i].value;
        }
        return signature;
    }

    std::string ShaderSystem::BuildStagePath(
        const std::string_view source,
        const std::string_view stageExtension,
        const bool spirv)
    {
        std::string path(source);
        if (!HasExtension(source))
        {
            path += ".";
            path += stageExtension;
        }

        if (spirv && !path.ends_with(".spv"))
        {
            path += ".spv";
        }

        return path;
    }

    bool ShaderSystem::HasExtension(const std::string_view source)
    {
        const std::filesystem::path path(source);
        return path.has_extension();
    }
}
