#pragma once

#include <winsock2.h>

enum class IocpEventType
{
    None,
    Accept,
    Recv,
    Send
};

// IOCP에서 반환된 OVERLAPPED*를 IocpEvent*로 복원하기 위해
// OVERLAPPED를 구조체의 첫 번째 멤버로 유지한다.
// 비동기 I/O가 완료될 때까지 해당 Event 객체의 수명도 유지되어야 한다.
struct IocpEvent
{
    OVERLAPPED overlapped = {};
    IocpEventType type = IocpEventType::None;
};
struct IocpEvent
{
    OVERLAPPED overlapped = {};
    IocpEventType type = IocpEventType::None;
};

// 하나의 AcceptEx 요청에 필요한 상태를 보관한다.
// acceptSocket과 OVERLAPPED는 Accept 완료 전까지 유지되어야 한다.
struct AcceptEvent : IocpEvent
{
    AcceptEvent()
    {
        type = IocpEventType::Accept;
    }

    SOCKET acceptSocket = INVALID_SOCKET;
    char buffer[1024] = {};
};

struct RecvEvent : IocpEvent
{
    RecvEvent()
    {
        type = IocpEventType::Recv;
        wsaBuf.buf = buffer;
        wsaBuf.len = sizeof(buffer);
    }

    char buffer[1024] = {};
    WSABUF wsaBuf = {};
};

struct SendEvent : IocpEvent
{
    SendEvent()
    {
        type = IocpEventType::Send;
    }

    WSABUF wsaBuf = {};
};