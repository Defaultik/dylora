#include "Core/Injector.h"
#include "Core/IInjectionMethod.h"

#include <windows.h>

namespace
{
    bool FileExists(const std::wstring& path)
    {
        DWORD attrs = GetFileAttributesW(path.c_str());
        return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
    }

    std::wstring ToAbsolutePath(const std::wstring& path)
    {
        wchar_t buffer[MAX_PATH];
        DWORD length = GetFullPathNameW(path.c_str(), MAX_PATH, buffer, nullptr);
        if (length == 0 || length >= MAX_PATH)
            return path;

        return std::wstring(buffer, length);
    }
}

bool InjectDll(const ProcessTarget& target, const std::wstring& dllPath, std::wstring* error)
{
    if (!target.IsValid())
    {
        if (error) *error = L"Process Target is invalid.";
        return false;
    }

    std::wstring absolutePath = ToAbsolutePath(dllPath);

    if (!FileExists(absolutePath))
    {
        if (error) *error = L"DLL not found: " + absolutePath;
        return false;
    }

    LoadLibraryInjection method;
    if (!method.Inject(target.Handle(), absolutePath))
    {
        if (error) *error = L"Injection failed (LoadLibraryW in target process returned 0).";
        return false;
    }

    return true;
}