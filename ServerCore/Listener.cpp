#include "pch.h"
#include "IocpEvent.h"
#include "Listener.h"
#include "SocketUtils.h"
#include "NetAddress.h"

Listener::~Listener()
{
    Close();
}

bool Listener::Start(const NetAddress& address)
{
    if (_listenSocket != INVALID_SOCKET)
        return false;

    _listenSocket = SocketUtils::CreateSocket();
    if (_listenSocket == INVALID_SOCKET)
        return false;

    if (!SocketUtils::Bind(_listenSocket, address))
    {
        Close();
        return false;
    }

    if (!SocketUtils::Listen(_listenSocket))
    {
        Close();
        return false;
    }

    _acceptEx = SocketUtils::GetAcceptEx(_listenSocket);
    if (_acceptEx == nullptr)
    {
        Close();
        return false;
    }

    return true;
}

bool Listener::PostAccept(AcceptEvent& event)
{
    event.acceptSocket = SocketUtils::CreateSocket();
    if (event.acceptSocket == INVALID_SOCKET)
        return false;

    DWORD bytes = 0;

    BOOL result = _acceptEx(_listenSocket, event.acceptSocket, event.buffer, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &bytes, &event.overlapped);

    if (result == FALSE)
    {
        int32_t error = WSAGetLastError();

        // 비동기 Accept 요청이 정상적으로 대기 상태에 들어간 경우
        if (error != ERROR_IO_PENDING)
        {
            SocketUtils::Close(event.acceptSocket);
            return false;
        }
    }

    return true;
}

void Listener::Close()
{
    // Listener가 소유한 listen socket 해제
    SocketUtils::Close(_listenSocket);
    _acceptEx = nullptr;
}

bool Listener::CompleteAccept(AcceptEvent& event)
{
    return SocketUtils::UpdateAcceptContext(event.acceptSocket, _listenSocket);
}