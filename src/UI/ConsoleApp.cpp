#include "UI/ConsoleApp.h"

#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <iostream>
#include <string>

namespace {

    void PrintProcesses(const std::vector<ProcessInfo>& processes)
    {
        std::wcout << L"PID\tName\n";
        for (const auto& p : processes)
            std::wcout << p.pid << L"\t" << p.name << L"\n";

        std::wcout << L"\nTotal: " << processes.size() << L"\n";
    }

}

int RunConsoleApp(const std::vector<ProcessInfo>& processes)
{
    // Needed for correct non-ASCII input/output. After this, use only wcin/wcout.
    _setmode(_fileno(stdin), _O_U16TEXT);
    _setmode(_fileno(stdout), _O_U16TEXT);

    PrintProcesses(processes);

    std::wcout << L"Enter Process Name or PID: " << std::flush;
    std::wstring input;
    std::getline(std::wcin, input);

    return 0;
}