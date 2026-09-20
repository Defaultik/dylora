#pragma once

#include <cstdint>
#include <string>
#include <windows.h>

// RAII-обёртка над HANDLE процесса-цели для последующей работы с ним (инъекция DLL и т.п.).
// Сам механизм инъекции сюда не входит — только поиск процесса и получение хэндла.
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

    // Ищет процесс по PID, открывает хэндл с правами, нужными будущему механизму инъекции.
    // При неудаче (процесс не найден / нет прав) возвращает невалидный объект — проверяй IsValid().
    static ProcessTarget FromPid(std::uint32_t pid);

    // Ищет процесс по имени exe (без учёта регистра). При нескольких совпадениях берёт первое найденное.
    static ProcessTarget FromName(const std::wstring& name);

private:
    ProcessTarget(HANDLE handle, std::uint32_t pid, std::wstring name);

    HANDLE handle_ = nullptr;
    std::uint32_t pid_ = 0;
    std::wstring name_;
};