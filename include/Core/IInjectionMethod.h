#pragma once

#include <string>
#include <windows.h>

class IInjectionMethod
{
public:
    virtual ~IInjectionMethod() = default;

    virtual bool Inject(HANDLE processHandle, const std::wstring& dllPath) const = 0;
};

class LoadLibraryInjection final : public IInjectionMethod
{
public:
    bool Inject(HANDLE processHandle, const std::wstring& dllPath) const override;
};