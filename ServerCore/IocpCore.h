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

    // IOCP에서 완료 통지를 하나 가져온다.
    // 반환값은 GetQueuedCompletionStatus 호출 자체를 계속 처리할 수 있는지 나타내며,
    // 실제 비동기 I/O의 성공 여부는 ioSuccess로 별도 전달
    // timeout은 오류가 아니므로 timedOut으로 구분
    bool GetCompletion(DWORD& bytes, ULONG_PTR& key, OVERLAPPED*& overlapped, bool& ioSuccess, bool& timedOut, DWORD timeoutMs = INFINITE);

private:
    HANDLE _iocpHandle = nullptr;
};