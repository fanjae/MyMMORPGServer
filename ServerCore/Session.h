#pragma once

#include <winsock2.h>
#include "IocpEvent.h"

class Session
{
public:
    explicit Session(SOCKET socket);
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    SOCKET GetSocket() const { return _socket; }
    bool IsConnected() const { return _socket != INVALID_SOCKET; }

    bool PostRecv();
    bool OnRecv(DWORD bytes);
    void Close();

private:
    SOCKET _socket = INVALID_SOCKET;
    RecvEvent _recvEvent;
};