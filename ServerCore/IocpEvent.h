#pragma once

#include <winsock2.h>

enum class IocpEventType
{
    None,
    Accept,
    Recv,
    Send
};

struct IocpEvent
{
    OVERLAPPED overlapped = {};
    IocpEventType type = IocpEventType::None;
};

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