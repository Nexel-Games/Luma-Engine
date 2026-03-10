#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Luma/RHI/RHIResources.h"
#include "Luma/RHI/RendererAPI.h"

namespace Luma
{
    struct ShaderMacro
    {
        std::string name;
        std::string value;
    };

    struct ShaderVariantOptions
    {
        std::vector<ShaderMacro> macros;
        bool preferSpirv = true;
    };

    struct ShaderProgramRegistration
    {
        std::string programName;
        std::string vertexSource;
        std::string fragmentSource;
    };

    struct ShaderProgramVariant
    {
        ShaderProgramDesc programDesc;
        std::string cacheKey;
        std::vector<ShaderMacro> macros;
    };

    class ShaderSystem
    {
    public:
        bool Initialize(RendererAPI api);
        void Shutdown();

        bool RegisterProgram(const ShaderProgramRegistration& registration);
        bool HasProgram(std::string_view programName) const;

        bool TryResolveProgram(
            std::string_view programName,
            const ShaderVariantOptions& options,
            ShaderProgramVariant& outVariant);

        void InvalidateProgram(std::string_view programName);
        void ClearCache();

        std::size_t GetCachedVariantCount() const;
        RendererAPI GetRendererAPI() const;

    private:
        struct ProgramRecord
        {
            std::string vertexSource;
            std::string fragmentSource;
        };

        struct VariantKey
        {
            std::string programName;
            std::string macroSignature;
            bool spirv = false;
        };

        struct VariantKeyHash
        {
            std::size_t operator()(const VariantKey& key) const;
        };

        struct VariantKeyEqual
        {
            bool operator()(const VariantKey& lhs, const VariantKey& rhs) const;
        };

        static std::string BuildMacroSignature(const std::vector<ShaderMacro>& macros);
        static std::string BuildStagePath(std::string_view source, std::string_view stageExtension, bool spirv);
        static bool HasExtension(std::string_view source);

        bool m_Initialized = false;
        RendererAPI m_RendererAPI = RendererAPI::OpenGL;
        std::unordered_map<std::string, ProgramRecord> m_Programs;
        std::unordered_map<VariantKey, ShaderProgramVariant, VariantKeyHash, VariantKeyEqual> m_VariantCache;
    };
}
