#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct ProcessInfo {
    std::wstring name;
    std::uint32_t pid;
};

// Returns a snapshot of all running processes (name, pid).
// Returns an empty vector if the snapshot could not be created.
std::vector<ProcessInfo> GetProcessList();