#pragma once

#include <windows.h>

class IocpCore
{
public:
    IocpCore();
    ~IocpCore();

    IocpCore(const IocpCore&) = delete;
    IocpCore& operator=(const IocpCore&) = delete;

    bool IsValid() const { return _iocpHandle != nullptr; }
    HANDLE GetHandle() const { return _iocpHandle; }

    bool Register(HANDLE handle, ULONG_PTR key);
    bool Post(ULONG_PTR key, DWORD bytes = 0, OVERLAPPED* overlapped = nullptr);
    bool GetCompletion(DWORD& bytes, ULONG_PTR& key, OVERLAPPED*& overlapped, DWORD timeoutMs = INFINITE);

private:
    HANDLE _iocpHandle = nullptr;
};