#pragma once

#include <vector>

#include "Windows/ProcessUtils.h"

// Shows the processes to the user, then waits for user input.
int RunConsoleApp(const std::vector<ProcessInfo>& processes);