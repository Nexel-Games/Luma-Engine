#pragma once

#include <filesystem>
#include <string>

namespace Luma
{
    class DynamicLibrary
    {
    public:
        DynamicLibrary() = default;
        ~DynamicLibrary();

        DynamicLibrary(const DynamicLibrary&) = delete;
        DynamicLibrary& operator=(const DynamicLibrary&) = delete;
        DynamicLibrary(DynamicLibrary&& other) noexcept;
        DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

        bool Load(const std::filesystem::path& path, std::string& outError);
        bool Unload(std::string& outError);
        void* GetSymbol(const char* symbolName) const;
        bool IsLoaded() const;
        const std::filesystem::path& GetPath() const;

    private:
        void Reset();

        std::filesystem::path m_Path;
        void* m_Handle = nullptr;
    };
}

