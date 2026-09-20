#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>

#include "Windows/ProcessUtils.h"

std::vector<ProcessInfo> GetProcessList()
{
    std::vector<ProcessInfo> result;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return result;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);

    if (Process32FirstW(snap, &pe)) {
        do {
            result.push_back({ std::wstring(pe.szExeFile), static_cast<std::uint32_t>(pe.th32ProcessID) });
        } while (Process32NextW(snap, &pe));
    }

    CloseHandle(snap);
    return result;
}