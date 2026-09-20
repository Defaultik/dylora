#include "UI/ConsoleApp.h"
#include "Windows/ProcessUtils.h"

int main()
{
    return RunConsoleApp(GetProcessList());
}