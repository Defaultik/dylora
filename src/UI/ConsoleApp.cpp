#include "UI/ConsoleApp.h"

#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <string>

namespace
{
    void PrintProcesses(const std::vector<ProcessInfo>& processes)
    {
        std::wcout << L"PID\tName\n";
        for (const auto& process : processes)
            std::wcout << process.pid << L"\t" << process.name << L"\n";

        std::wcout << L"\nTotal: " << processes.size() << L"\n";
    }

    // Switches stdin/stdout to UTF-16 mode so wide process names print correctly.
    void EnableUnicodeConsole()
    {
        _setmode(_fileno(stdin), _O_U16TEXT);
        _setmode(_fileno(stdout), _O_U16TEXT);
    }
}

int RunConsoleApp(const std::vector<ProcessInfo>& processes)
{
    EnableUnicodeConsole();

    PrintProcesses(processes);

    std::wcout << L"\nEnter process name or PID: " << std::flush;
    std::wstring input;
    std::getline(std::wcin, input);

    return 0;
}