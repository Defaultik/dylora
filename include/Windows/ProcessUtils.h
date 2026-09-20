#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ProcessInfo {
    std::wstring name;
    std::uint32_t pid;
};

std::vector<ProcessInfo> GetProcessList();