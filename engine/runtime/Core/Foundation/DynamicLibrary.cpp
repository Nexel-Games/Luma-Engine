#include "Luma/Core/Foundation/DynamicLibrary.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#else
#include <dlfcn.h>
#endif

namespace Luma
{
    DynamicLibrary::~DynamicLibrary()
    {
        std::string error;
        Unload(error);
    }

    DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
    {
        m_Path = std::move(other.m_Path);
        m_Handle = other.m_Handle;
        other.Reset();
    }

    DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
    {
        if (this == &other)
        {
            return *this;
        }

        std::string error;
        Unload(error);

        m_Path = std::move(other.m_Path);
        m_Handle = other.m_Handle;
        other.Reset();
        return *this;
    }

    bool DynamicLibrary::Load(const std::filesystem::path& path, std::string& outError)
    {
        std::string unloadError;
        Unload(unloadError);

#if defined(_WIN32)
        HMODULE module = ::LoadLibraryW(path.wstring().c_str());
        if (module == nullptr)
        {
            outError = "LoadLibraryW failed for " + path.string();
            return false;
        }
        m_Handle = module;
#else
        void* module = ::dlopen(path.string().c_str(), RTLD_NOW);
        if (module == nullptr)
        {
            outError = std::string("dlopen failed: ") + dlerror();
            return false;
        }
        m_Handle = module;
#endif
        m_Path = path;
        outError.clear();
        return true;
    }

    bool DynamicLibrary::Unload(std::string& outError)
    {
        if (m_Handle == nullptr)
        {
            outError.clear();
            return true;
        }

#if defined(_WIN32)
        if (::FreeLibrary(static_cast<HMODULE>(m_Handle)) == 0)
        {
            outError = "FreeLibrary failed for " + m_Path.string();
            return false;
        }
#else
        if (::dlclose(m_Handle) != 0)
        {
            outError = std::string("dlclose failed: ") + dlerror();
            return false;
        }
#endif
        Reset();
        outError.clear();
        return true;
    }

    void* DynamicLibrary::GetSymbol(const char* symbolName) const
    {
        if (m_Handle == nullptr || symbolName == nullptr || *symbolName == '\0')
        {
            return nullptr;
        }

#if defined(_WIN32)
        return reinterpret_cast<void*>(::GetProcAddress(static_cast<HMODULE>(m_Handle), symbolName));
#else
        return ::dlsym(m_Handle, symbolName);
#endif
    }

    bool DynamicLibrary::IsLoaded() const
    {
        return m_Handle != nullptr;
    }

    const std::filesystem::path& DynamicLibrary::GetPath() const
    {
        return m_Path;
    }

    void DynamicLibrary::Reset()
    {
        m_Path.clear();
        m_Handle = nullptr;
    }
}

