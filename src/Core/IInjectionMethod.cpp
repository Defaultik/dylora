#include "Core/IInjectionMethod.h"

namespace
{
    FARPROC GetLoadLibraryWAddress()
    {
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        if (!kernel32)
            return nullptr;

        return GetProcAddress(kernel32, "LoadLibraryW");
    }
}

bool LoadLibraryInjection::Inject(HANDLE processHandle, const std::wstring& dllPath) const
{
    FARPROC loadLibraryW = GetLoadLibraryWAddress();
    if (!loadLibraryW)
        return false;

    const SIZE_T bufferSize = (dllPath.size() + 1) * sizeof(wchar_t);

    LPVOID remoteBuffer = VirtualAllocEx(processHandle, nullptr, bufferSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteBuffer)
        return false;

    bool written = WriteProcessMemory(processHandle, remoteBuffer, dllPath.c_str(), bufferSize, nullptr) != 0;
    if (!written)
    {
        VirtualFreeEx(processHandle, remoteBuffer, 0, MEM_RELEASE);
        return false;
    }

    HANDLE remoteThread = CreateRemoteThread(
        processHandle,
        nullptr,
        0,
        reinterpret_cast<LPTHREAD_START_ROUTINE>(loadLibraryW),
        remoteBuffer,
        0,
        nullptr);

    if (!remoteThread)
    {
        VirtualFreeEx(processHandle, remoteBuffer, 0, MEM_RELEASE);
        return false;
    }

    WaitForSingleObject(remoteThread, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeThread(remoteThread, &exitCode);

    CloseHandle(remoteThread);
    VirtualFreeEx(processHandle, remoteBuffer, 0, MEM_RELEASE);

    return exitCode != 0;
}