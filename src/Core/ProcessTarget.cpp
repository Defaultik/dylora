#include "Core/ProcessTarget.h"

#include <tlhelp32.h>
#include <algorithm>
#include <cwctype>
#include <utility>

namespace
{
    // Права, которых достаточно будущему механизму инъекции (запись/чтение памяти + создание потока).
    constexpr DWORD kTargetAccess =
        PROCESS_QUERY_INFORMATION |
        PROCESS_CREATE_THREAD |
        PROCESS_VM_OPERATION |
        PROCESS_VM_READ |
        PROCESS_VM_WRITE;

    bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b)
    {
        if (a.size() != b.size())
            return false;

        return std::equal(a.begin(), a.end(), b.begin(), [](wchar_t l, wchar_t r) {
            return std::towlower(l) == std::towlower(r);
            });
    }

    // pid == 0, если процесс с таким именем не найден.
    std::pair<std::uint32_t, std::wstring> FindByName(const std::wstring& name)
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return { 0, L"" };

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);

        std::pair<std::uint32_t, std::wstring> result{ 0, L"" };

        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (EqualsIgnoreCase(entry.szExeFile, name))
                {
                    result = { static_cast<std::uint32_t>(entry.th32ProcessID), entry.szExeFile };
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return result;
    }

    // Пустая строка, если процесс с таким pid не найден.
    std::wstring FindNameByPid(std::uint32_t pid)
    {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
            return L"";

        PROCESSENTRY32W entry{};
        entry.dwSize = sizeof(entry);

        std::wstring name;

        if (Process32FirstW(snapshot, &entry))
        {
            do
            {
                if (entry.th32ProcessID == pid)
                {
                    name = entry.szExeFile;
                    break;
                }
            } while (Process32NextW(snapshot, &entry));
        }

        CloseHandle(snapshot);
        return name;
    }
}

ProcessTarget::ProcessTarget(HANDLE handle, std::uint32_t pid, std::wstring name)
    : handle_(handle), pid_(pid), name_(std::move(name))
{
}

ProcessTarget::~ProcessTarget()
{
    if (handle_)
        CloseHandle(handle_);
}

ProcessTarget::ProcessTarget(ProcessTarget&& other) noexcept
    : handle_(other.handle_), pid_(other.pid_), name_(std::move(other.name_))
{
    other.handle_ = nullptr;
    other.pid_ = 0;
}

ProcessTarget& ProcessTarget::operator=(ProcessTarget&& other) noexcept
{
    if (this != &other)
    {
        if (handle_)
            CloseHandle(handle_);

        handle_ = other.handle_;
        pid_ = other.pid_;
        name_ = std::move(other.name_);

        other.handle_ = nullptr;
        other.pid_ = 0;
    }
    return *this;
}

ProcessTarget ProcessTarget::FromPid(std::uint32_t pid)
{
    std::wstring name = FindNameByPid(pid);
    if (name.empty())
        return {};

    HANDLE handle = OpenProcess(kTargetAccess, FALSE, pid);
    if (!handle)
        return {};

    return ProcessTarget(handle, pid, std::move(name));
}

ProcessTarget ProcessTarget::FromName(const std::wstring& name)
{
    auto [pid, exactName] = FindByName(name);
    if (pid == 0)
        return {};

    return FromPid(pid);
}