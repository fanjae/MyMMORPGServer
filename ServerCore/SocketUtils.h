#pragma once

#include <winsock2.h>
#include <mswsock.h>
#include <cstdint>

class NetAddress;

class SocketUtils
{
public:
    static bool Init();
    static void Clear();

    static SOCKET CreateSocket();
    static void Close(SOCKET& socket);

    static bool Listen(SOCKET socket, int32_t backlog = SOMAXCONN);
    static bool Bind(SOCKET socket, const NetAddress& address);
    static bool Connect(SOCKET socket, const NetAddress& address);
    static bool UpdateAcceptContext(SOCKET acceptSocket, SOCKET listenSocket);

    static LPFN_ACCEPTEX GetAcceptEx(SOCKET socket);
};