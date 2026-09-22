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

bool Listener::PostAccept()
{
    _acceptEvent.overlapped = {};
    _acceptEvent.acceptSocket = SocketUtils::CreateSocket();

    if (_acceptEvent.acceptSocket == INVALID_SOCKET)
        return false;

    DWORD bytes = 0;

    // 최초 데이터 수신은 기다리지 않고 연결 수락만 완료 대상으로 삼는다.
    // AcceptEx의 local/remote address 영역은 sockaddr 크기보다
    // 각각 16바이트 이상 크게 확보해야 한다.
    BOOL result = _acceptEx(_listenSocket, _acceptEvent.acceptSocket, _acceptEvent.buffer, 0, sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16, &bytes, &_acceptEvent.overlapped);

    if (result == FALSE)
    {
        int32_t error = WSAGetLastError();

        // 비동기 Accept 요청이 정상적으로 대기 상태에 들어간 경우
        if (error != ERROR_IO_PENDING)
        {
            SocketUtils::Close(_acceptEvent.acceptSocket);
            return false;
        }
    }

    return true;
}

void Listener::Close()
{
    SocketUtils::Close(_listenSocket);
    _acceptEx = nullptr;
}

// AcceptEx가 완료된 socket에 listen socket의 context를 반영
bool Listener::CompleteAccept(AcceptEvent& event)
{
    return SocketUtils::UpdateAcceptContext(event.acceptSocket, _listenSocket);
}