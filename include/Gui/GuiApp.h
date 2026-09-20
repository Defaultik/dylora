#pragma once

#include <windows.h>

// Creates the application window, sets up the Dear ImGui + Direct3D 11
// renderer and runs the message/render loop until the window is closed.
// Returns the process exit code.
int RunGuiApp(HINSTANCE instance, int showCommand);
