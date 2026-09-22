#pragma once

#include <winsock2.h>
#include <mswsock.h>

#include "IocpEvent.h"

class NetAddress;
struct AcceptEvent;

// listen socket과 AcceptEx 상태를 관리한다.
// 현재 구현은 Listener 하나당 하나의 AcceptEvent를 보유해
// 동시에 하나의 Accept 요청만 pending 상태로 유지한다.
class Listener
{
public:
    Listener() = default;
    ~Listener();

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    bool Start(const NetAddress& address);
    bool PostAccept();
    bool CompleteAccept(AcceptEvent& event);
    void Close();

    SOCKET GetSocket() const { return _listenSocket; }

private:
    SOCKET _listenSocket = INVALID_SOCKET;
    LPFN_ACCEPTEX _acceptEx = nullptr;
    AcceptEvent _acceptEvent;
};