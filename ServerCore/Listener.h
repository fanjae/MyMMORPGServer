#pragma once

#include <winsock2.h>
#include <mswsock.h>

class NetAddress;
struct AcceptEvent;

class Listener
{
public:
    Listener() = default;
    ~Listener();

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    bool Start(const NetAddress& address);
    bool PostAccept(AcceptEvent& event);
    bool CompleteAccept(AcceptEvent& event);
    void Close();

    SOCKET GetSocket() const { return _listenSocket; }

private:
    SOCKET _listenSocket = INVALID_SOCKET;
    LPFN_ACCEPTEX _acceptEx = nullptr;
};