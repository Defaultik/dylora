#pragma once

#include <cstdint>
#include <string>
#include <windows.h>

class ProcessTarget
{
public:
    ProcessTarget() = default;
    ~ProcessTarget();

    ProcessTarget(const ProcessTarget&) = delete;
    ProcessTarget& operator=(const ProcessTarget&) = delete;

    ProcessTarget(ProcessTarget&& other) noexcept;
    ProcessTarget& operator=(ProcessTarget&& other) noexcept;

    bool IsValid() const { return handle_ != nullptr; }

    HANDLE Handle() const { return handle_; }
    std::uint32_t Pid() const { return pid_; }
    const std::wstring& Name() const { return name_; }

    static ProcessTarget FromPid(std::uint32_t pid);

    static ProcessTarget FromName(const std::wstring& name);

private:
    ProcessTarget(HANDLE handle, std::uint32_t pid, std::wstring name);

    HANDLE handle_ = nullptr;
    std::uint32_t pid_ = 0;
    std::wstring name_;
};