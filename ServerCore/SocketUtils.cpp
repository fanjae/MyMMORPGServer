#include "pch.h"
#include "SocketUtils.h"
#include "NetAddress.h"

bool SocketUtils::Bind(SOCKET socket, const NetAddress& address)
{
    return bind(socket, reinterpret_cast<const SOCKADDR*>(&address.GetAddress()), sizeof(SOCKADDR_IN)) != SOCKET_ERROR;
}

bool SocketUtils::Init()
{
    WSADATA wsaData;

    // Winsock 2.2 사용을 위해 프로세스의 Winsock 환경 초기화
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

bool SocketUtils::Listen(SOCKET socket, int32_t backlog)
{
    return listen(socket, backlog) != SOCKET_ERROR;
}

bool SocketUtils::UpdateAcceptContext(SOCKET acceptSocket, SOCKET listenSocket)
{
    return setsockopt(acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<const char*>(&listenSocket), sizeof(listenSocket)) != SOCKET_ERROR;
}

void SocketUtils::Clear()
{
    WSACleanup();
}

SOCKET SocketUtils::CreateSocket()
{
    // IOCP 기반 비동기 I/O를 위해 OVERLAPPED 소켓으로 생성
    return WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
}

void SocketUtils::Close(SOCKET& socket)
{
    if (socket == INVALID_SOCKET)
        return;

    closesocket(socket);

    // 닫힌 소켓의 재사용 및 중복 close 방지
    socket = INVALID_SOCKET;
}

// AcceptEx는 Winsock 확장 함수이므로 런타임에 함수 포인터 획득
LPFN_ACCEPTEX SocketUtils::GetAcceptEx(SOCKET socket)
{
    GUID guid = WSAID_ACCEPTEX;
    LPFN_ACCEPTEX acceptEx = nullptr;
    DWORD bytes = 0;

    int32_t result = WSAIoctl(socket, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid), &acceptEx, sizeof(acceptEx), &bytes, nullptr, nullptr);

    if (result == SOCKET_ERROR)
        return nullptr;

    return acceptEx;
}