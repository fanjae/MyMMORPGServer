#include "pch.h"
#include "Session.h"
#include "SocketUtils.h"
#include <iostream>

Session::Session(SOCKET socket) : _socket(socket)
{
}

Session::~Session()
{
    SocketUtils::Close(_socket);
}

void Session::Close()
{
    SocketUtils::Close(_socket);
}

bool Session::PostRecv()
{
    if (_socket == INVALID_SOCKET)
        return false;

    _recvEvent.overlapped = {};

    DWORD flags = 0;
    int32_t result = WSARecv(_socket, &_recvEvent.wsaBuf, 1, nullptr, &flags, &_recvEvent.overlapped, nullptr);

    if (result == SOCKET_ERROR)
    {
        int32_t error = WSAGetLastError();

        // 비동기 Recv 요청이 정상적으로 대기 상태에 들어간 경우
        if (error != WSA_IO_PENDING)
            return false;
    }

    return true;
}

bool Session::OnRecv(DWORD bytes)
{
    if (bytes == 0)
    {
        std::cout << "Client Disconnected\n";
        Close();
        return true;
    }

    std::cout << "Recv Event: " << bytes << " bytes, Data: ";
    std::cout.write(_recvEvent.buffer, bytes);
    std::cout << '\n';

    if (!PostRecv())
    {
        Close();
        return false;
    }

    return true;
}