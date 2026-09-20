#pragma once

#include <string>

#include "Core/ProcessTarget.h"

bool InjectDll(const ProcessTarget& target, const std::wstring& dllPath, std::wstring* error = nullptr);