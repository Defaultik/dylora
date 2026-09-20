#include "UI/ConsoleApp.h"
#include "Core/ProcessTarget.h"
#include "Core/Injector.h"

#include <fcntl.h>
#include <io.h>
#include <conio.h>
#include <iostream>
#include <string>
#include <cwctype>
#include <algorithm>

namespace
{
    void PrintProcesses(const std::vector<ProcessInfo>& processes)
    {
        std::wcout << L"PID\tName\n";
        for (const auto& process : processes)
            std::wcout << process.pid << L"\t" << process.name << L"\n";

        std::wcout << L"\nTotal: " << processes.size() << L"\n";
    }

    void EnableUnicodeConsole()
    {
        _setmode(_fileno(stdin), _O_U16TEXT);
        _setmode(_fileno(stdout), _O_U16TEXT);
    }

    bool IsNumeric(const std::wstring& s)
    {
        return !s.empty() && std::all_of(s.begin(), s.end(), [](wchar_t c) {
            return std::iswdigit(static_cast<wint_t>(c)) != 0;
            });
    }

    void PrintColored(const std::wstring& text, WORD color)
    {
        HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);

        CONSOLE_SCREEN_BUFFER_INFO info;
        GetConsoleScreenBufferInfo(console, &info);
        WORD originalAttributes = info.wAttributes;

        SetConsoleTextAttribute(console, color);
        std::wcout << text << L"\n";
        SetConsoleTextAttribute(console, originalAttributes);
    }

    void WaitForAnyKey()
    {
        std::wcout << L"\nPress any key to exit..." << std::flush;
        _getwch();
    }
}

int RunConsoleApp(const std::vector<ProcessInfo>& processes)
{
    EnableUnicodeConsole();

    PrintProcesses(processes);

    std::wcout << L"\nEnter process name or PID: " << std::flush;
    std::wstring input;
    std::getline(std::wcin, input);

    ProcessTarget target = IsNumeric(input)
        ? ProcessTarget::FromPid(static_cast<std::uint32_t>(std::stoul(input)))
        : ProcessTarget::FromName(input);

    if (!target.IsValid())
    {
        std::wcout << L"Process not found or access denied.\n";
        return 1;
    }

    std::wcout << L"Found: [" << target.Pid() << L"] " << target.Name() << L"\n";

    std::wcout << L"\nEnter DLL path to inject: " << std::flush;
    std::wstring dllPath;
    std::getline(std::wcin, dllPath);

    std::wstring error;
    bool success = InjectDll(target, dllPath, &error);

    if (success)
        PrintColored(L"DLL injected successfully.", FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    else
        PrintColored(L"Injection failed: " + error, FOREGROUND_RED | FOREGROUND_INTENSITY);

    WaitForAnyKey();

    return success ? 0 : 1;
}