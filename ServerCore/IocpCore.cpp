#include "pch.h"
#include "IocpCore.h"

IocpCore::IocpCore()
{
    // 새로운 IOCP 객체 생성
    _iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
}

IocpCore::~IocpCore()
{
    if (_iocpHandle == nullptr)
        return;

    CloseHandle(_iocpHandle);
    _iocpHandle = nullptr;
}

bool IocpCore::Register(HANDLE handle, ULONG_PTR key)
{
    // 지정한 핸들을 현재 IOCP에 연결
    return CreateIoCompletionPort(handle, _iocpHandle, key, 0) == _iocpHandle;
}

bool IocpCore::Post(ULONG_PTR key, DWORD bytes, OVERLAPPED* overlapped)
{
    // 실제 I/O 없이 테스트용 완료 패킷을 IOCP 큐에 직접 삽입
    return PostQueuedCompletionStatus(_iocpHandle, bytes, key, overlapped) != FALSE;
}

bool IocpCore::GetCompletion(DWORD& bytes, ULONG_PTR& key, OVERLAPPED*& overlapped, DWORD timeoutMs)
{
    return GetQueuedCompletionStatus(_iocpHandle, &bytes, &key, &overlapped, timeoutMs) != FALSE;
}