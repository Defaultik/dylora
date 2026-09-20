#include "Windows/ProcessUtils.h"

#include <windows.h>
#include <tlhelp32.h>

std::vector<ProcessInfo> GetProcessList()
{
    std::vector<ProcessInfo> processes;

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return processes;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            processes.push_back({ entry.szExeFile, static_cast<std::uint32_t>(entry.th32ProcessID) });
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return processes;
}